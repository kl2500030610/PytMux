#include "multiplexer.h"
#include "terminal.h"
#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>

int main(void)
{
    Multiplexer mux;
    int command_mode = 0;

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

        active = multiplexer_get_active(&mux);

        if (active == NULL)
            break;

        FD_ZERO(&read_fds);

        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(active->master_fd, &read_fds);

        if (select(
                active->master_fd + 1,
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

        /*
         * Keyboard input
         */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char buffer[4096];

            ssize_t n = read(
                STDIN_FILENO,
                buffer,
                sizeof(buffer)
            );

            if (n <= 0)
                break;

            for (ssize_t i = 0; i < n; i++) {
                char ch = buffer[i];

                /*
                 * Ctrl-B starts command mode.
                 */
                if (!command_mode && ch == 2) {
                    command_mode = 1;

                    const char *message =
                        "\r\n[PtyMux] ";

                    write(
                        STDOUT_FILENO,
                        message,
                        11
                    );

                    continue;
                }

                /*
                 * Handle command after Ctrl-B.
                 */
                if (command_mode) {
                    command_mode = 0;

                    /*
                     * 0-9 -> switch session
                     */
                    if (ch >= '0' && ch <= '9') {
                        int session_id = ch - '0';

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
                        } else {
                            const char *message =
                                "\r\n[Invalid session]\r\n";

                            write(
                                STDOUT_FILENO,
                                message,
                                21
                            );
                        }

                        continue;
                    }

                    /*
                     * c -> create session
                     */
                    if (ch == 'c') {
                        if (handle_command(
                                &mux,
                                'c'
                            ) == 1) {
                            continue;
                        }
                    }

                    /*
                     * l -> list sessions
                     */
                    if (ch == 'l') {
                        if (handle_command(
                                &mux,
                                'l'
                            ) == 1) {
                            continue;
                        }
                    }

                    /*
                     * q -> quit
                     */
                    if (ch == 'q') {
                        if (handle_command(
                                &mux,
                                'q'
                            ) == -1) {
                            goto cleanup;
                        }
                    }

                    /*
                     * Unknown command.
                     */
                    const char *message =
                        "\r\n[Unknown command]\r\n";

                    write(
                        STDOUT_FILENO,
                        message,
                        21
                    );

                    continue;
                }

                /*
                 * Normal keyboard input -> active PTY
                 */
                if (write(
                        active->master_fd,
                        &ch,
                        1
                    ) == -1) {
                    goto cleanup;
                }
            }
        }

        /*
         * Active PTY -> terminal
         */
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
                goto cleanup;
            }
        }
    }

cleanup:

    terminal_restore();

    multiplexer_cleanup(&mux);

    return EXIT_SUCCESS;
}