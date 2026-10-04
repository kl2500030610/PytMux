#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static struct termios original_termios;
static int terminal_configured = 0;

void terminal_restore(void)
{
    if (terminal_configured)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}

int terminal_raw_mode(void)
{
    struct termios raw;

    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "stdin is not a terminal\n");
        return -1;
    }

    if (tcgetattr(STDIN_FILENO, &original_termios) == -1) {
        perror("tcgetattr");
        return -1;
    }

    raw = original_termios;

    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);

    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        return -1;
    }

    terminal_configured = 1;

    atexit(terminal_restore);

    return 0;
}