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

    if (terminal_get_size(
            &rows,
            &cols
        ) == -1)
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

    update_terminal_size(
        multiplexer_get_active(&mux)
    );

    while (running) {
        Session *active;
        fd_set read_fds;

        active = multiplexer_get_active(&mux);

        if (active == NULL)
            break;

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
         * Keyboard -> PtyMux / shell
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
                 * Start command mode.
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
                 * Command mode.
                 */
                if (command_mode) {
                    Command command =
                        parse_command(ch);

                    command_mode = 0;

                    /*
                     * Ctrl+B Ctrl+B
                     *
                     * Send literal Ctrl+B
                     * to the shell.
                     */
                    if (command.type ==
                        COMMAND_LITERAL_PREFIX) {

                        char prefix = 2;

                        write(
                            active->master_fd,
                            &prefix,
                            1
                        );

                        continue;
                    }

                    int result =
                        execute_command(
                            &mux,
                            command
                        );

                    if (result == -1) {
                        running = 0;
                        break;
                    }

                    /*
                     * Session may have changed.
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
                 * Normal keyboard input.
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
         * PTY -> terminal
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