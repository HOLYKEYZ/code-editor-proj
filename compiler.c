#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>



typedef enum {
    TOK_INT, TOK_PLUS, TOK_MINUS, TOK_PRINT, TOK_SEMI, TOK_EOF
} TokenType;

typedef struct {
    TokenType type;
    int value; 
} Token;

const char *src; // Pointer to source code string
Token current_token;

// --- 2. LEXER (Tokenizer) ---
// Converts "print 1 + 2;" into tokens

void next_token() {
    while (isspace(*src)) src++; // Skip whitespace

    if (*src == '\0') {
        current_token.type = TOK_EOF;
        return;
    }

    if (isdigit(*src)) {
        current_token.type = TOK_INT;
        current_token.value = 0;
        while (isdigit(*src)) {
            current_token.value = current_token.value * 10 + (*src - '0');
            src++;
        }
        return;
    }

    if (strncmp(src, "print", 5) == 0 && !isalnum(src[5])) {
        current_token.type = TOK_PRINT;
        src += 5;
        return;
    }

    switch (*src) {
        case '+': current_token.type = TOK_PLUS; src++; return;
        case '-': current_token.type = TOK_MINUS; src++; return;
        case ';': current_token.type = TOK_SEMI; src++; return;
    }

    fprintf(stderr, "Unexpected character: %c\n", *src);
    exit(1);
}

void match(TokenType type) {
    if (current_token.type == type) {
        next_token();
    } else {
        fprintf(stderr, "Syntax error: Expected token type %d\n", type);
        exit(1);
    }
}

// --- 3. CODE GENERATOR ---
// We simply print Assembly instructions to stdout

void parse_expression() {
    // We expect a number first
    if (current_token.type == TOK_INT) {
        printf("  mov rax, %d\n", current_token.value);
        next_token();
    } else {
        fprintf(stderr, "Expected integer\n");
        exit(1);
    }

    // Handle operations (e.g., + 5, - 2)
    while (current_token.type == TOK_PLUS || current_token.type == TOK_MINUS) {
        TokenType op = current_token.type;
        next_token();
        
        if (current_token.type == TOK_INT) {
            if (op == TOK_PLUS) {
                printf("  add rax, %d\n", current_token.value);
            } else {
                printf("  sub rax, %d\n", current_token.value);
            }
            next_token();
        } else {
            fprintf(stderr, "Expected integer after operator\n");
            exit(1);
        }
    }
}

// --- 4. PARSER ---
// Analyzes grammar: statement -> print expression ;

void parse_statement() {
    if (current_token.type == TOK_PRINT) {
        next_token(); // consume 'print'
        
        parse_expression(); // generates code to put result in RAX
        
        // Code Gen: Print the integer in RAX using printf
        // We assume a 'fmt' string exists in the assembly data section
        printf("  mov rsi, rax\n");      // Move result to 2nd argument
        printf("  mov rdi, fmt\n");      // Move format string to 1st argument
        printf("  xor rax, rax\n");      // Clear RAX (for variadic function convention)
        printf("  call printf\n");       // Call C standard library printf
        
        match(TOK_SEMI);
    } else {
        fprintf(stderr, "Syntax error: Expected 'print'\n");
        exit(1);
    }
}

void compile() {
    // Emit Assembly Header
    printf("global main\n");
    printf("extern printf\n");
    printf("section .text\n");
    printf("main:\n");
    printf("  sub rsp, 8\n"); // Align stack

    next_token(); // Prime the lexer
    while (current_token.type != TOK_EOF) {
        parse_statement();
    }

    // Emit Assembly Footer
    printf("  add rsp, 8\n"); // Restore stack
    printf("  mov rax, 0\n"); // Return 0
    printf("  ret\n");
    
    // Data Section
    printf("section .data\n");
    printf("fmt: db \"Output: %%d\", 10, 0\n"); // Format string with newline
}

// --- 5. DRIVER ---

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s \"code to compile\"\n", argv[0]);
        return 1;
    }

    src = argv[1];
    compile();
    
    return 0;
}

