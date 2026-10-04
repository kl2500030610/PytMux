#include "pty.h"
#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <errno.h>

int main(void)
{
    int master_fd;

    if (terminal_raw_mode() == -1)
        return EXIT_FAILURE;

    master_fd = create_pty_shell();

    if (master_fd == -1) {
        terminal_restore();
        return EXIT_FAILURE;
    }

    while (1) {
        fd_set read_fds;

        FD_ZERO(&read_fds);

        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(master_fd, &read_fds);

        if (select(master_fd + 1, &read_fds, NULL, NULL, NULL) == -1) {
            if (errno == EINTR)
                continue;

            perror("select");
            break;
        }

        /* Keyboard → PTY */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char buffer[4096];

            ssize_t n = read(
                STDIN_FILENO,
                buffer,
                sizeof(buffer)
            );

            if (n <= 0)
                break;

            ssize_t written = write(
                master_fd,
                buffer,
                n
            );

            if (written == -1)
                break;
        }

        /* PTY → Terminal */
        if (FD_ISSET(master_fd, &read_fds)) {
            char buffer[4096];

            ssize_t n = read(
                master_fd,
                buffer,
                sizeof(buffer)
            );

            if (n <= 0)
                break;

            ssize_t written = write(
                STDOUT_FILENO,
                buffer,
                n
            );

            if (written == -1)
                break;
        }
    }

    terminal_restore();

    close(master_fd);

    wait(NULL);

    return EXIT_SUCCESS;
}