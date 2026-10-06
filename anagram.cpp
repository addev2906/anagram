#include <asm-generic/ioctls.h>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <sys/ioctl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <cstdlib>
#include<unistd.h>
#include <termios.h>

#define CTRL_KEY(k) ((k)&0x1f)
struct editorConfig{
    int editorRows;
    int editorCols;
    struct termios original_term;
};

struct editorConfig E;



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
        return -1;
    }
    else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void drawRows(){
    for(size_t i = 0;i<24;i++){
        write(STDOUT_FILENO,"~\r\n",3);
    }
}
void refreshScreen(){
    write(STDOUT_FILENO,"\x1b[2J", 4); // we use 4 here because \x1b[2J is 4 bytes in size
    write(STDOUT_FILENO,"\x1b[H", 3); // using VT100 codes

    drawRows();
    write(STDOUT_FILENO,"\x1b[H", 3); // using VT100 codes
}


void editorProcessKeyboard(){
    char c = editorReadKey();
    switch(c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}


int main(){
    enableRawMode();
    while(1){
        refreshScreen();
        editorProcessKeyboard();
    }
    return 0;
}
