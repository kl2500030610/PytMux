#ifndef PTY_H
#define PTY_H

#include <sys/types.h>

int create_pty_shell(pid_t *child_pid);

int pty_set_size(
    int master_fd,
    int rows,
    int cols
);

#endif