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
#define BACKSPACE 127
constexpr bool kDebug = false;
enum EditorKey {
    ARROW_UP=1000,
    ARROW_DOWN,
    ARROW_RIGHT,
    ARROW_LEFT,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};
struct editorConfig{
    int screenrows;
    int screencols;
    const char* filename;
    int col_offset = 0;
    int row_offset = 0;
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

void debugLog(const std::string& message,int c=-1){
    if constexpr (kDebug) {
        std::ofstream logfile("log.txt",std::ios::app);
        if(logfile.is_open()){
            logfile<<message<<' '<<c<<"\n";
            logfile.close();
        }
    }
}
void printCoords(){
    debugLog(std::to_string(E.cx)+';'+std::to_string(E.cy)+';'
        +std::to_string(E.col_offset)+';'
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
    if((nread = read(STDIN_FILENO,&c,1)) != 1){
        if(nread == -1 && errno != EAGAIN) die("read");
        return -1;
    }

    if(c == '\x1b'){
        char seq[3];
        if(read(STDIN_FILENO,&seq[0],1) != 1) return '\x1b';
        if(read(STDIN_FILENO,&seq[1],1) != 1) return '\x1b';

        if(seq[0] == '['){
            if(seq[1] >= '0' && seq[1] <= '9'){
                if(read(STDIN_FILENO,&seq[2],1) != 1) return '\x1b';
                if(seq[2] == '~'){
                    switch(seq[1]){
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch(seq[1]){
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if(seq[0] == 'O'){
            switch(seq[1]){
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
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
    for(int i = 0;i<E.screenrows;i++){
        int filerow = E.row_offset+i;
        if(filerow>=E.lines.size()) *buf+="~";
        else if(E.col_offset<E.lines[filerow].size())*buf += E.lines[filerow].substr(E.col_offset, E.screencols);
        if(i<E.screenrows-1) *buf+="\r\n";
    }
}

void scrollEditor(){
    if(E.cx<E.col_offset) E.col_offset=E.cx;
    if(E.cx>=E.col_offset+E.screencols) E.col_offset = E.cx-E.screencols+1;
    if(E.cy<E.row_offset) E.row_offset = E.cy;
    if(E.cy>=E.screenrows+E.row_offset) E.row_offset = E.cy-E.screenrows+1;
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
    buf+="\x1b["+std::to_string(E.cy-E.row_offset+1)+";"+std::to_string(E.cx - E.col_offset +1)+"H";
    write(STDOUT_FILENO,buf.data(),buf.length());
}

void editorMoveCursor(int dir){
    switch(dir){
        case ARROW_DOWN:
            if(E.cy<E.lines.size()-1) {
                E.cx = std::min(E.maxcx,(int)E.lines[E.cy+1].size());
                E.cy++;
            }
            printCoords();
            break;
        case ARROW_UP:
            if(E.cy>0){
                E.cx = std::min(E.maxcx,(int)E.lines[E.cy-1].size());
                E.cy--;
            }
            printCoords();
            break;
        case ARROW_RIGHT:
            if(E.cx<=getCurrentLineSize()-1) {E.cx++;}
            else if(E.cx==getCurrentLineSize() && E.cy<E.lines.size()-1){
                E.cx=0;
                E.cy++;
            }
            E.maxcx=E.cx;
            printCoords();
            break;
        case ARROW_LEFT:
            if(E.cx>0) {E.cx--;}
            else if(E.cx==0 && E.cy>0){
                E.cx = E.lines[E.cy-1].size();
                E.cy--;
            }
            E.maxcx = E.cx;
            printCoords();
            break;

    }
}
void insertCharcter(int c){
    // debugLog((char)c+" ");
    E.lines[E.cy].insert(E.cx,1,(char)c);
    editorMoveCursor(ARROW_RIGHT);
}
void deleteCharacter(){
    // debugLog("Backspace");
    if(E.cx>0){
        E.lines[E.cy].erase(E.cx-1,1);
        editorMoveCursor(ARROW_LEFT);
    }
    else if(E.cy>0){
        E.cx = E.lines[E.cy-1].length();
        E.lines[E.cy-1]+=E.lines[E.cy];
        E.lines.erase(E.lines.begin()+E.cy);
        E.cy--;
    }
}
void editorProcessKeyboard(){
    int c = editorReadKey();
    debugLog(" ",c);
    switch(c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case ARROW_DOWN:
        case ARROW_RIGHT:
        case ARROW_LEFT:
        case ARROW_UP:
            editorMoveCursor(c);
            break;
        case BACKSPACE:
            deleteCharacter();
            break;
        case DEL_KEY:
            editorMoveCursor(ARROW_RIGHT);
            deleteCharacter();
            break;
        default:
            if(c>=32 && c<=126) insertCharcter(c);
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
