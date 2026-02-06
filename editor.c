/*** 
 * MINIMAL WINDOWS TEXT EDITOR
 * Based on 'Build Your Own Text Editor' (Kilo), adapted for Windows Console API.
 ***/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <conio.h>
#include <windows.h>
#include <time.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

// --- DEFINES ---
#define KILO_VERSION "0.0.1"
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

// --- DATA ---
typedef struct erow {
    int size;
    char *chars;
    unsigned char *hl; // Highlight array
} erow;

enum editorHighlight {
    HL_NORMAL = 0,
    HL_NUMBER,
    HL_STRING,
    HL_MATCH // For search
};

struct editorConfig {
    int cx, cy;     // Cursor X, Y
    int rowoff;     // Row Offset (scrolling)
    int coloff;     // Column Offset (scrolling)
    int screenrows;
    int screencols;
    int numrows;    // Number of rows in file
    erow *row;      // Array of rows
    int dirty;      // File modified?
    char *filename; // Filename
    char statusmsg[80];
    time_t statusmsg_time;
    HANDLE hOut;    // Console Handle
    DWORD origMode; // Original Console Mode
};

struct editorConfig E;

struct editorConfig E;

struct abuf {
    char *b;
    int len;
};

// --- PROTOTYPES ---
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt);
int editorReadKey();
void editorScroll();

// --- TERMINAL ---

void die(const char *s) {
    // Restore original mode on exit
    SetConsoleMode(E.hOut, E.origMode);
    
    // Clear screen and position cursor top-left
    fwrite("\x1b[2J", 1, 4, stdout);
    fwrite("\x1b[H", 1, 3, stdout);

    perror(s);
    exit(1);
}

void enableRawMode() {
    E.hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    
    if (E.hOut == INVALID_HANDLE_VALUE) die("GetStdHandle");

    if (!GetConsoleMode(E.hOut, &E.origMode)) die("GetConsoleMode");

    DWORD rawMode = E.origMode;
    
    // Disable processing that handles input on its own (like Ctrl-C)
    // ENABLE_ECHO_INPUT: No echo characters
    // ENABLE_LINE_INPUT: Reading char by char, not line by line
    // ENABLE_PROCESSED_INPUT: No processing of Ctrl-C etc. (except optional)
    rawMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT); 

    // Enable Virtual Terminal sequences (ANSI colors/cursor)
    rawMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

    if (!SetConsoleMode(E.hOut, rawMode)) die("SetConsoleMode");
}

int getWindowSize(int *rows, int *cols) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        return 0;
    } else {
        return -1;
    }
}

// --- SYNTAX HIGHLIGHTING ---

void editorUpdateSyntax(erow *row) {
    row->hl = realloc(row->hl, row->size);
    memset(row->hl, HL_NORMAL, row->size);
    
    int i = 0;
    int prev_sep = 1; // Previous char was a separator?
    int in_string = 0;
    char quote_char = 0;

    while (i < row->size) {
        char c = row->chars[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

        // Strings
        if (in_string) {
            row->hl[i] = HL_STRING;
            if (c == '\\' && i + 1 < row->size) {
                row->hl[i + 1] = HL_STRING;
                i += 2;
                continue;
            }
            if (c == quote_char) in_string = 0;
            prev_sep = 1;
            i++;
            continue;
        } else {
            if (c == '"' || c == '\'') {
                in_string = 1;
                quote_char = c;
                row->hl[i] = HL_STRING;
                i++;
                continue;
            }
        }

        // Numbers
        if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || 
            (c == '.' && prev_hl == HL_NUMBER)) {
            row->hl[i] = HL_NUMBER;
            i++;
            prev_sep = 0;
            continue;
        }

        prev_sep = isspace(c) || ispunct(c);
        i++;
    }
}

int editorSyntaxToColor(int hl) {
    switch (hl) {
        case HL_NUMBER: return 31; // Red
        case HL_STRING: return 35; // Magenta
        default: return 37; // White
    }
}

// --- SEARCH ---

char *editorPrompt(char *prompt) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);
    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();
        if (c == 8 || c == 127 || c == CTRL_KEY('h')) { // Backspace
            if (buflen != 0) buf[--buflen] = '\0';
        } else if (c == '\x1b') { // Escape
            editorSetStatusMessage("");
            free(buf);
            return NULL;
        } else if (c == '\r') {
            if (buflen != 0) {
                editorSetStatusMessage("");
                return buf; // User pressed Enter
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }
    }
}

void editorFind() {
    char *query = editorPrompt("Search: %s (ESC to cancel)");
    if (query == NULL) return;

    int i;
    for (i = 0; i < E.numrows; i++) {
        erow *row = &E.row[i];
        char *match = strstr(row->chars, query);
        if (match) {
            E.cy = i;
            E.cx = match - row->chars;
            E.rowoff = E.numrows; // Force scroll to bottom then back
            break;
        }
    }
    free(query);
}

// --- ROW OPERATIONS ---

void editorRowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateSyntax(row);
}

void editorRowDelChar(erow *row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > E.numrows) return;

    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    
    E.row[at].hl = NULL; 
    editorUpdateSyntax(&E.row[at]);
    
    E.numrows++;
}

void editorAppendRow(char *s, size_t len) {
    editorInsertRow(E.numrows, s, len);
}


// --- EDITOR OPERATIONS ---

void editorInsertChar(int c) {
    if (E.cy == E.numrows) {
        editorAppendRow("", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
    E.dirty++;
}

void editorDelChar() {
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;

    erow *row = &E.row[E.cy];
    if (E.cx > 0) {
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    } else {
        // Join line with previous
        E.cx = E.row[E.cy - 1].size;
        // Optimization: simplistic join (append current to prev)
        // For MVP, we'll just handle simple backspace within line first
        // If we want full join support:
        erow *prev = &E.row[E.cy - 1];
        prev->chars = realloc(prev->chars, prev->size + row->size + 1);
        memcpy(&prev->chars[prev->size], row->chars, row->size);
        prev->size += row->size;
        prev->chars[prev->size] = '\0';
        
        // Update Syntax for full line
        editorUpdateSyntax(prev);

        // Remove current row
        free(row->chars);
        free(row->hl);
        memmove(&E.row[E.cy], &E.row[E.cy + 1], sizeof(erow) * (E.numrows - E.cy - 1));
        E.numrows--;
        E.cy--;
    }
    E.dirty++;
}

void editorInsertNewline() {
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        // Truncate current row
        row = &E.row[E.cy]; // Re-pointer after realloc
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateSyntax(row);
    }
    E.cy++;
    E.cx = 0;
    E.dirty++;
}

// --- FILE I/O ---

char *editorRowsToString(int *buflen) {
    int totlen = 0;
    int j;
    for (j = 0; j < E.numrows; j++)
        totlen += E.row[j].size + 1; // +1 for newline
    
    *buflen = totlen;
    char *buf = malloc(totlen);
    char *p = buf;
    for (j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }
    return buf;
}

void editorSave() {
    if (!E.filename) {
        E.filename = editorPrompt("Save as: %s");
        if (!E.filename) {
            editorSetStatusMessage("Save aborted.");
            return;
        }
    }

    int len;
    char *buf = editorRowsToString(&len);

    FILE *fp = fopen(E.filename, "w");
    if (fp) {
        if (fwrite(buf, 1, len, fp) == len) {
            fclose(fp);
            free(buf);
            E.dirty = 0;
            editorSetStatusMessage("%d bytes written to disk", len);
            return;
        }
        fclose(fp);
    }
    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

void editorOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    // Windows doesn't typically have getline, so we use a simple loop
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp)) {
        // Strip newline
        int len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
            len--;
        }
        editorAppendRow(buf, len);
    }
    fclose(fp);
    E.dirty = 0;
}

// --- INPUT ---

int editorReadKey() {
    int c = _getch(); // Windows implementation of reading 1 char

    // Handle Arrow Keys
    // On Windows, arrows return 0 or 0xE0, followed by a code
    if (c == 0 || c == 0xE0) {
        int seq = _getch();
        switch (seq) {
            case 72: return ARROW_UP;    // 'H'
            case 80: return ARROW_DOWN;  // 'P'
            case 75: return ARROW_LEFT;  // 'K'
            case 77: return ARROW_RIGHT; // 'M'
            case 71: return HOME_KEY;
            case 79: return END_KEY;
            case 73: return PAGE_UP;
            case 81: return PAGE_DOWN;
        }
    }
    return c;
}


// --- OUTPUT ---

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);
    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

void editorScroll() {
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
}

void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "Minimal Editor -- version %s", KILO_VERSION);
                if (welcomelen > E.screencols) welcomelen = E.screencols;
                
                // Center it
                int padding = (E.screencols - welcomelen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);
                
                abAppend(ab, welcome, welcomelen);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            // Draw File Content with Highlighting
            int len = E.row[filerow].size;
            if (len > E.screencols) len = E.screencols;
            
            char *c = E.row[filerow].chars;
            unsigned char *hl = E.row[filerow].hl;
            int current_color = -1;
            
            int j;
            for (j = 0; j < len; j++) {
                if (iscntrl(c[j])) {
                    char sym = (c[j] <= 26) ? '@' + c[j] : '?';
                    abAppend(ab, "\x1b[7m", 4);
                    abAppend(ab, &sym, 1);
                    abAppend(ab, "\x1b[m", 3);
                } else {
                    int color = editorSyntaxToColor(hl[j]);
                    if (color != current_color) {
                        current_color = color;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        abAppend(ab, buf, clen);
                    }
                    abAppend(ab, &c[j], 1);
                }
            }
            abAppend(ab, "\x1b[39m", 5); // Reset color
        }

        // Clear rest of line to right
        abAppend(ab, "\x1b[K", 3);
        
        if (y < E.screenrows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorDrawStatusBar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4); // Invert colors
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
        E.filename ? E.filename : "[No Name]", E.numrows,
        E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
        E.cy + 1, E.numrows);
    if (len > E.screencols) len = E.screencols;
    abAppend(ab, status, len);
    while (len < E.screencols) {
        if (E.screencols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3); // Normal colors
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3); // Clear line
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;

    // Hide cursor
    abAppend(&ab, "\x1b[?25l", 6);
    
    // Move to top-left
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    // Move cursor to saved E.cx, E.cy (adjusted for scroll)
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    // Show cursor
    abAppend(&ab, "\x1b[?25h", 6);

    fwrite(ab.b, 1, ab.len, stdout);

    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

// --- LOGIC ---

void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    
    switch (key) {
        case ARROW_LEFT:
            if (E.cx > 0) E.cx--;
            break;
        case ARROW_RIGHT:
             // Limit right move to existing text length (needs logic, simplistic for now)
             if (row && E.cx < row->size) E.cx++;
             else if (!row && E.cx < E.screencols - 1) E.cx++;
            break;
        case ARROW_UP:
            if (E.cy > 0) E.cy--;
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) E.cy++;
            break;
    }
}

void editorProcessKeypress() {
    static int quit_times = 3;

    int c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            if (E.dirty && quit_times > 0) {
                editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                    "Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times--;
                return;
            }
            fwrite("\x1b[2J", 1, 4, stdout);
            fwrite("\x1b[H", 1, 3, stdout);
            exit(0);
            break;
        
        case CTRL_KEY('s'):
            editorSave();
            break;
            
        case CTRL_KEY('f'):
            editorFind();
            break;

        case '\r':
        case '\n':
            editorInsertNewline();
            break;
            
        case 8: // Backspace
        case 127:
            editorDelChar();
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case PAGE_UP:
        case PAGE_DOWN:
        case HOME_KEY:
        case END_KEY:
            editorMoveCursor(c);
            break;
            
        case CTRL_KEY('l'):
        case '\x1b':
            break;

        default:
            if (!iscntrl(c)) {
                editorInsertChar(c);
            }
            break;
    }
    
    quit_times = 3; // Reset if any other key pressed
}

void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    E.screenrows -= 2; // Make room for status bars
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    
    if (argc >= 2) {
        editorOpen(argv[1]);
    }
    
    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
