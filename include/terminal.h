#ifndef TERMINAL_H
#define TERMINAL_H

int terminal_raw_mode(void);
void terminal_restore(void);

int terminal_get_size(
    int *rows,
    int *cols
);

#endif