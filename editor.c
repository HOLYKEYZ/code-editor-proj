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