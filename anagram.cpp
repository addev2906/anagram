#include <asm-generic/ioctls.h>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <ios>
#include <sys/ioctl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <cstdlib>
#include <unistd.h>
#include <termios.h>
#include <string>
#include <vector>


#define CTRL_KEY(k) ((k)&0x1f)
constexpr bool kDebug = false;
enum EditorKey {
    ARROW_UP=1000,
    ARROW_DOWN,
    ARROW_RIGHT,
    ARROW_LEFT
};
struct editorConfig{
    int screenrows;
    int screencols;
    const char* filename;
    int view_offset = 0;
    int cx;
    int cy;
    int maxcx;
    std::string line;
    std::vector<std::string> lines;
    struct termios original_term;
};

struct editorConfig E;

int getCurrentLineSize(){
    return E.lines[E.cy].size();
}

int getCursorPosition(int* rows,int* cols);
// struct screenbuffer{
//     const char* start;
//     int length;
// };
// screenbuffer abuf;

void debugLog(const std::string& message){
    if constexpr (kDebug) {
        std::ofstream logfile("log.txt",std::ios::app);
        if(logfile.is_open()){
            logfile<<message<<"\n";
            logfile.close();
        }
    }
}
void printCoords(){
    debugLog(std::to_string(E.cx)+';'+std::to_string(E.cy)+';'
        +std::to_string(E.view_offset)+';'
        +std::to_string(getCurrentLineSize()));
}


void die(const char* s){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    write(STDOUT_FILENO,"\x1b[?1049l",8);
    perror(s);
    exit(1);
}

void disableRawMode(){
    std::ofstream file("log.txt");
    if(file.is_open()){
        file.clear();
        file.close();
    }
    write(STDOUT_FILENO,"\x1b[?1049l",8);
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
    write(STDOUT_FILENO,"\x1b[?1049h",8);
}

int editorReadKey(){
    int nread;
    char c;
    if((nread = read(STDIN_FILENO,&c,1))!=1){
        if(nread==-1 && errno!=EAGAIN) die("read");
    }
    if(c=='\x1b'){
        if(read(STDIN_FILENO,&c,1)==0) c='\x1b';
        else{
            read(STDIN_FILENO,&c,1);
            return 1000+(c-'A');
        }
    }
    return c;
}

int getWindowSize(int* rows,int* cols){
    struct winsize ws;
    if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws)== -1 || ws.ws_col == 0){
        if(write(STDIN_FILENO,"\x1b[999C\x1b[999B",12)!=12) return -1;
        getCursorPosition(rows,cols);
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
        if(i<E.lines.size() && E.view_offset < E.lines[i].size()) *buf+= E.lines[i].substr(E.view_offset,E.screencols)+"\r\n";
        else if(i<E.lines.size() && E.view_offset >= E.lines[i].size()) *buf+="\r\n";
        else *buf+="~\r\n";
    }
    *buf+='~';
}

void scrollEditor(){
    if(E.cx<E.view_offset) E.view_offset=E.cx;
    if(E.cx>=E.view_offset+E.screencols) E.view_offset = E.cx-E.screencols+1;
    if(E.cx>=E.screencols+E.view_offset){
        E.view_offset = std::min(E.view_offset+1,(int)getCurrentLineSize());
    }
    if(E.cx<E.view_offset){
        E.view_offset = std::max(E.view_offset-1,0);
    }
}
void refreshScreen(){
    scrollEditor();
    std::string buf;
    buf+="\x1b[2J"; //Clear Screen
    buf+="\x1b[H";  //Move cursor to upper left
    buf+="";
    int rows;
    int cols;
    // getCursorPosition(&rows,&cols);
    appendRows(&buf);
    buf+="\x1b["+std::to_string(E.cy+1)+";"+std::to_string(E.cx - E.view_offset +1)+"H";
    write(STDOUT_FILENO,buf.data(),buf.length());
}


void editorProcessKeyboard(){
    int c = editorReadKey();
    switch(c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case ARROW_DOWN:
            // if(E.lines[E.cy+1].size()-1<E.cx) E.cx = E.lines[E.cy+1].size();
            if(E.cy<E.lines.size()-1) {
                E.cx = std::min(E.maxcx,(int)E.lines[E.cy+1].size());
                E.cy++;
            }
            break;
        case ARROW_UP:
            if(E.cy>0){
                E.cx = std::min(E.maxcx,(int)E.lines[E.cy-1].size());
                E.cy--;
            }
            break;
        case ARROW_RIGHT:
            if(E.cx<=getCurrentLineSize()-1) {E.cx++;}
            else if(E.cx==getCurrentLineSize() && E.cy<E.lines.size()-1){
                E.cx=0;
                E.cy++;
            }
            E.maxcx=E.cx;
            break;
        case ARROW_LEFT:
            if(E.cx>0) {E.cx--;}
            else if(E.cx==0 && E.cy>0){
                E.cx = E.lines[E.cy-1].size();
                E.cy--;
            }
            E.maxcx = E.cx;
            break;
    }
    printCoords();
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
