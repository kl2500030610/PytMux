#include "multiplexer.h"
#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>

int main(void)
{
    Multiplexer mux;

    multiplexer_init(&mux);

    if (terminal_raw_mode() == -1)
        return EXIT_FAILURE;

    if (multiplexer_create_session(&mux, "bash-0") == -1) {
        terminal_restore();
        return EXIT_FAILURE;
    }

    while (1) {
        Session *active;
        fd_set read_fds;
        int max_fd;

        active = multiplexer_get_active(&mux);

        if (active == NULL)
            break;

        FD_ZERO(&read_fds);

        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(active->master_fd, &read_fds);

        max_fd = active->master_fd;

        if (select(
                max_fd + 1,
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

        /* Keyboard -> active session */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char buffer[4096];

            ssize_t n = read(
                STDIN_FILENO,
                buffer,
                sizeof(buffer)
            );

            if (n <= 0)
                break;

            /*
             * W3 session switching:
             *
             * Ctrl-B followed by a number will
             * switch to that session.
             */

            if (n == 2 &&
                buffer[0] == 2 &&
                buffer[1] >= '0' &&
                buffer[1] <= '9') {

                int session_id = buffer[1] - '0';

                if (multiplexer_switch_session(
                        &mux,
                        session_id
                    ) == 0) {

                    char message[128];

                    int len = snprintf(
                        message,
                        sizeof(message),
                        "\r\n[Switched to session %d]\r\n",
                        session_id
                    );

                    write(
                        STDOUT_FILENO,
                        message,
                        len
                    );
                }

                continue;
            }

            if (write(
                    active->master_fd,
                    buffer,
                    n
                ) == -1) {
                break;
            }
        }

        /* Active PTY -> terminal */
        if (FD_ISSET(
                active->master_fd,
                &read_fds
            )) {

            char buffer[4096];

            ssize_t n = read(
                active->master_fd,
                buffer,
                sizeof(buffer)
            );

            if (n <= 0) {
                active->state = SESSION_DEAD;
                continue;
            }

            session_buffer_write(
                active,
                buffer,
                n
            );

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

    multiplexer_cleanup(&mux);

    return EXIT_SUCCESS;
}