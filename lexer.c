/*
 * lexer.c - tokenizer for the RPN language.
 *
 * Tokens are separated by white space and are lower-cased, which makes the
 * language case-insensitive.  '#' starts a comment that runs to the end of
 * the line and also ends a token (so "3#x" is the token "3").  '{' and '}'
 * are always tokens of their own: "goto{loop}" is goto, {, loop, }.
 */
#include <ctype.h>
#include "intern.h"

int rpn_next_token(const char **pp, char *buf, size_t bufsz)
{
    const char *p = *pp;
    size_t n = 0;
    int too_long = 0;

    for (;;) {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '#') {
            while (*p != '\0' && *p != '\n') {
                p++;
            }
            continue;
        }
        break;
    }
    if (*p == '\0') {
        *pp = p;
        buf[0] = '\0';
        return 0;
    }
    if (*p == '{' || *p == '}') {
        buf[0] = *p;
        buf[1] = '\0';
        *pp = p + 1;
        return 1;
    }
    while (*p != '\0' && *p != '#' && *p != '{' && *p != '}' &&
           !isspace((unsigned char)*p)) {
        if (n + 1 < bufsz) {
            buf[n++] = (char)tolower((unsigned char)*p);
        } else {
            too_long = 1;
        }
        p++;
    }
    buf[n] = '\0';
    *pp = p;
    return too_long ? -1 : 1;
}
