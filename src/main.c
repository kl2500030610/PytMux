#include "pty.h"
#include "terminal.h"
#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <errno.h>

int main(void)
{
    Session session;

    session_init(&session, 0, "bash");

    if (terminal_raw_mode() == -1)
        return EXIT_FAILURE;

    session.master_fd = create_pty_shell(&session.pid);

    if (session.master_fd == -1) {
        terminal_restore();
        return EXIT_FAILURE;
    }

    session.state = SESSION_RUNNING;

    while (session_is_alive(&session)) {
        fd_set read_fds;

        FD_ZERO(&read_fds);

        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(session.master_fd, &read_fds);

        if (select(
                session.master_fd + 1,
                &read_fds,
                NULL,
                NULL,
                NULL
            ) == -1) {

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

            if (write(
                    session.master_fd,
                    buffer,
                    n
                ) == -1) {
                break;
            }
        }

        /* PTY → Terminal */
        if (FD_ISSET(session.master_fd, &read_fds)) {
            char buffer[4096];

            ssize_t n = read(
                session.master_fd,
                buffer,
                sizeof(buffer)
            );

            if (n <= 0) {
                session.state = SESSION_DEAD;
                break;
            }

            /* Store output in session buffer */
            session_buffer_write(
                &session,
                buffer,
                n
            );

            /* Display output */
            if (write(
                    STDOUT_FILENO,
                    buffer,
                    n
                ) == -1) {
                break;
            }
        }
    }

    terminal_restore();

    if (session.master_fd != -1) {
        close(session.master_fd);
        session.master_fd = -1;
    }

    waitpid(session.pid, NULL, 0);

    session.state = SESSION_DEAD;

    return EXIT_SUCCESS;
}