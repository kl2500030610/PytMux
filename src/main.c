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
    int running = 1;

    multiplexer_init(&mux);

    if (terminal_raw_mode() == -1)
        return EXIT_FAILURE;

    if (multiplexer_create_session(&mux, "bash-0") == -1) {
        terminal_restore();
        return EXIT_FAILURE;
    }

    while (running) {
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
                 * Ctrl-B starts PtyMux command mode.
                 */
                if (!command_mode && ch == 2) {
                    command_mode = 1;

                    /*
                     * Clear the current shell input line.
                     */
                    const char *message =
                        "\r\033[2K[PtyMux] ";

                    write(
                        STDOUT_FILENO,
                        message,
                        13
                    );

                    continue;
                }

                /*
                 * Handle command after Ctrl-B.
                 */
                if (command_mode) {
                    command_mode = 0;

                    /*
                     * Switch session: 0-9
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
                                "\r\033[2K[Switched to session %d]\r\n",
                                session_id
                            );

                            write(
                                STDOUT_FILENO,
                                message,
                                len
                            );
                        } else {
                            const char *message =
                                "\r\033[2K[Invalid session]\r\n";

                            write(
                                STDOUT_FILENO,
                                message,
                                24
                            );
                        }

                        continue;
                    }

                    /*
                     * Create session.
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
                     * List sessions.
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
                     * Quit PtyMux.
                     */
                    if (ch == 'q') {
                        running = 0;
                        continue;
                    }

                    /*
                     * Unknown command.
                     */
                    {
                        const char *message =
                            "\r\033[2K[Unknown command]\r\n";

                        write(
                            STDOUT_FILENO,
                            message,
                            23
                        );
                    }

                    continue;
                }

                /*
                 * Normal keyboard input -> active PTY.
                 */
                if (write(
                        active->master_fd,
                        &ch,
                        1
                    ) == -1) {
                    running = 0;
                    break;
                }
            }
        }

        /*
         * Active PTY -> terminal.
         */
        if (running &&
            FD_ISSET(
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
                running = 0;
            }
        }
    }

    terminal_restore();

    multiplexer_cleanup(&mux);

    return EXIT_SUCCESS;
}