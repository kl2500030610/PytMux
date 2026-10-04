#include "multiplexer.h"
#include "terminal.h"
#include "commands.h"
#include "pty.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <signal.h>
#include <errno.h>

static volatile sig_atomic_t resize_pending = 0;

static void handle_sigwinch(int signal)
{
    (void)signal;
    resize_pending = 1;
}

static void update_terminal_size(Session *session)
{
    int rows;
    int cols;

    if (session == NULL)
        return;

    if (terminal_get_size(&rows, &cols) == -1)
        return;

    pty_set_size(
        session->master_fd,
        rows,
        cols
    );
}

int main(void)
{
    Multiplexer mux;
    int command_mode = 0;
    int running = 1;

    multiplexer_init(&mux);

    if (terminal_raw_mode() == -1)
        return EXIT_FAILURE;

    signal(
        SIGWINCH,
        handle_sigwinch
    );

    if (multiplexer_create_session(
            &mux,
            "bash-0"
        ) == -1) {

        terminal_restore();
        return EXIT_FAILURE;
    }

    /*
     * Set the initial PTY size.
     */
    update_terminal_size(
        multiplexer_get_active(&mux)
    );

    while (running) {
        Session *active;
        fd_set read_fds;

        active = multiplexer_get_active(&mux);

        if (active == NULL)
            break;

        /*
         * Handle terminal resize.
         */
        if (resize_pending) {
            resize_pending = 0;

            update_terminal_size(active);
        }

        FD_ZERO(&read_fds);

        FD_SET(
            STDIN_FILENO,
            &read_fds
        );

        FD_SET(
            active->master_fd,
            &read_fds
        );

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
        if (FD_ISSET(
                STDIN_FILENO,
                &read_fds
            )) {

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
                        "\r\033[2K[PtyMux]";

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
                     * Quit.
                     */
                    if (ch == 'q') {
                        running = 0;
                        break;
                    }

                    /*
                     * Switch session.
                     */
                    if (ch >= '0' && ch <= '9') {
                        int session_id =
                            ch - '0';

                        if (multiplexer_switch_session(
                                &mux,
                                session_id
                            ) == 0) {

                            active =
                                multiplexer_get_active(
                                    &mux
                                );

                            update_terminal_size(
                                active
                            );

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
                        handle_command(
                            &mux,
                            'c'
                        );

                        /*
                         * Apply terminal size to the
                         * newly active session.
                         */
                        active =
                            multiplexer_get_active(
                                &mux
                            );

                        update_terminal_size(
                            active
                        );

                        continue;
                    }

                    /*
                     * List sessions.
                     */
                    if (ch == 'l') {
                        handle_command(
                            &mux,
                            'l'
                        );

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
                active->state =
                    SESSION_DEAD;

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