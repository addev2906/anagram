#include <asm-generic/ioctls.h>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <ios>
#include <sys/ioctl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <cstdlib>
#include<unistd.h>
#include <termios.h>
#include <string>
#include <vector>


#define CTRL_KEY(k) ((k)&0x1f)
// #define ESC(k) ("\x1b["+std::to_string(k))
struct editorConfig{
    int screenrows;
    int screencols;
    const char* filename;
    int cx;
    int cy;
    std::string line;
    std::vector<std::string> lines;
    struct termios original_term;
};

struct editorConfig E;

// struct screenbuffer{
//     const char* start;
//     int length;
// };
// screenbuffer abuf;

void debugLog(std::string message){
    std::ofstream logfile("log.txt");
    if(logfile.is_open()){
        logfile<<message<<"\n";
        logfile.close();
    }
}


void die(const char* s){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode(){
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.original_term) == -1) die("tcsetattr");
}
void enableRawMode(){
    if (tcgetattr(STDIN_FILENO, &E.original_term) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.original_term;
    raw.c_lflag = raw.c_lflag & ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

char editorReadKey(){
    int nread;
    char c;
    if((nread = read(STDIN_FILENO,&c,1))!=1){
        if(nread==-1 && errno!=EAGAIN) die("read");
    }
    return c;
}

int getWindowSize(int* rows,int* cols){
    struct winsize ws;
    if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws)== -1 || ws.ws_col == 0){
        if(write(STDIN_FILENO,"\x1b[999C\x1b[999B",12)!=12) return -1;
        editorReadKey();
    }
    else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
    return 0;
}


int getCursorPosition(int* rows,int* cols){
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    return 0;
}
void readFile(){
    debugLog(E.filename);
    std::ifstream file(E.filename);

    if(file.is_open()){
        while(std::getline(file,E.line)) E.lines.push_back(E.line);
    }
}
void appendRows(std::string* buf){
    for(int i = 0;i<E.screenrows-1;i++){
        if(i<E.lines.size()) *buf+= E.lines[i].substr(0,E.screencols)+"\r\n";
        else *buf+="~\r\n";
    }
    *buf+='~';
}

void refreshScreen(){
    std::string buf;
    buf+="\x1b[2J"; //Clear Screen
    buf+="\x1b[H";  //Move cursor to upper left
    buf+="";
    int rows;
    int cols;
    // getCursorPosition(&rows,&cols);
    appendRows(&buf);
    buf+="\x1b["+std::to_string(E.cy)+";"+std::to_string(E.cx)+"H";
    write(STDOUT_FILENO,buf.data(),buf.length());
}

void editorProcessKeyboard(){
    char c = editorReadKey();
    switch(c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        // case 'B':
        //     printf("down");
        //     write(STDOUT_FILENO,"\x1b[B",3);
        //     break;
        // case 'A':
        //     printf("up");
        //     write(STDOUT_FILENO,"\x1b[A",3);
        //     break;

    }
}

void initEditor(const char* filename){
    if(getWindowSize(&E.screenrows,&E.screencols)==-1) die("getWindowSize");
    E.cx=0;
    E.cy=0;
    E.filename = filename;
    readFile();
}
int main(int argc,char** argv){
    enableRawMode();
    initEditor(argv[1]);
    while(1){
        refreshScreen();
        editorProcessKeyboard();
    }
    return 0;
}
