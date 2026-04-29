#pragma region includes

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#pragma endregion

#pragma region defines

#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"

enum editorKey{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

#pragma endregion

#pragma region data

typedef struct erow{
    // Editor Row
    int size;
    char *chars;
} erow;

struct editorConfig{
    int cx, cy;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    struct termios orig_termios;
};

struct editorConfig E;

#pragma endregion

#pragma region terminal

void die(const char *s){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    // prints error message and exits the program
    perror(s);
    exit(1);
}

void disableRawMode(){
    //Setting previous(orig_termios) terminal attributes
    
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode(){
    //Disables echo mode by getting the
    //terminal attributes, on the local
    //flags, disabling echo

    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    /*
    --- input flags ---
    BRKINT -> break condition cause a SIGINT signal to be sent
    ICRNL -> fix Ctrl+m and enter
    INPCK -> parity checking
    ISTRIP -> 8th bit of each input byte to be stripped
    IXON -> turn off Ctrl+s and Ctrl+q

    --- output flags ---
    OPOST -> turn off post-processing of output

    --- control flags ---
    CS8 -> not a flag, sets the character size to 8bits per byte.

    --- local flags ---
    ECHO -> turn off printing values
    ICANON -> turn off canonical mode
    IEXTEN -> turn off Ctrl+v
    ISIG -> turn off Ctrl+c and Ctrl+z

    --- control characters ---
    VMIN -> sets the minimum number of bytes of input needed before read can return
    VTIME -> sets the maximum amount of time to wait before read returns
    */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey(){
    // wait for on keypress and return it.
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b'){
        char seq[3];
        
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '['){
            if (seq[1] >= '0' && seq[1]<= '9'){
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~'){
                    switch (seq[1]){
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }else{
                switch (seq[1]){
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
            }
        }else if(seq[0] == 'O'){
            switch (seq[1]){
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    }else{
        return c;
    }
}

int getCursorPosition(int *rows, int *cols){
    /*
    Gets the cursor position via escape sequence with
    the command n and argument 6
    */
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n",4) != 4) return -1;
    // n -> device status report
    // 6 -> cursor position

    while (i < sizeof(buf) - 1 ){
        if (read(STDIN_FILENO, &buf[i],1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols){
    // Gets window size
    /*  1st, trys via ioctl with the TIOCGWINSZ request
            Terminal Input/Output Control Get WINdow SiZe
        2nd, if 1st doenst work
            position the cursor at the bottom-right of the screen,
            then use escape sequences that let us query the position
            of the cursor. That tells us how many rows and columns
            there must be on the screen.
        */
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B",12) != 12) return -1;
        // C -> Cursor Foward command
        // B -> Cursor Down command
        // 999 -> how much
        return getCursorPosition(rows, cols);
    } else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

#pragma endregion

#pragma region row operations

void editorAppendRow(char *s, size_t len){
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    
    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
}

#pragma endregion

#pragma region file i/o

void editorOpen(char *filename){
    /*
    Opens a file to read only
    */
    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while((linelen = getline(&line, &linecap, fp)) != -1){
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
            linelen--;
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
}

#pragma endregion

#pragma region append buffer

struct abuf{
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len){
    /*
    Realocates memory, copies the string s after the end of
    the content data in the buffer and updates the pointer
    and lenght of the abuf to the new values.
    */
    char *new = realloc(ab->b, ab->len + len);

    if(new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab){
    // deallocates the dinamic memory used by an abuf
    free(ab->b);
}

#pragma endregion

#pragma region output

void editorDrawRows(struct abuf *ab){
    // draws each row of the buffer of text being edited
    // rows is not part ot he file and can't contain any text
    // Also prints out a welcome message
    int y;
    for (y = 0; y<E.screenrows; y++){
        if (y >= E.numrows){
            if (E.numrows == 0 && y == E.screenrows/3 ){
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "Kilo editor -- version %s", KILO_VERSION);
                if (welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if (padding){
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            }else{
                abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row[y].size;
            if (len > E.screencols) len = E.screencols;
            abAppend(ab, E.row[y].chars, len);
        }
        abAppend(ab, "\x1b[K", 3);
        if (y < E.screenrows -1){
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen(){
    struct abuf ab = ABUF_INIT;
    
    abAppend(&ab, "\x1b[?25l", 6);
    // hides the cursor
    abAppend(&ab, "\x1b[H", 3);
    // fixes cursor position
    // H -> cursor position(row;collum) by default(1;1)

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));
    
    abAppend(&ab, "\x1b[?25h", 6);
    // makes the cursor visible

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

#pragma endregion

#pragma region input

void editorMoveCursor(int key){
    // moves the cursor
    switch (key){
        case ARROW_LEFT:
            if (E.cx != 0){
                E.cx--;
            }
            break;
        case ARROW_RIGHT:
            if (E.cx != E.screencols -1){
                E.cx++;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0){
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy != E.screenrows -1){
                E.cy++;    
            }
            break;
    }
}

void editorProcessKeyPress(){
    // waits for a keypress and the handles it.
    int c = editorReadKey();

    switch (c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        
        case HOME_KEY:
            E.cx = 0;
            break;
        
        case END_KEY:
            E.cx = E.screencols - 1;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenrows;
                while(times--){
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;

        
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

#pragma endregion

#pragma region init

void initEditor(){
    E.cx = 0;
    E.cy = 0;
    E.numrows = 0;
    E.row = NULL;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]){
    enableRawMode();
    initEditor();
    if (argc >= 2){
        editorOpen(argv[1]);
    }

    while(1){
        editorRefreshScreen();
        editorProcessKeyPress();
    }
    return 0;
}

#pragma endregion