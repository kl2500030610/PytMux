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

/*
 * Display buffered output when returning
 * to a session.
 */
static void display_session_buffer(
    Session *session
)
{
    char buffer[4096];
    size_t n;

    if (session == NULL)
        return;

    while ((n = session_buffer_read(
                session,
                buffer,
                sizeof(buffer)
            )) > 0) {

        if (write(
                STDOUT_FILENO,
                buffer,
                n
            ) == -1) {

            return;
        }
    }

    session->has_unread_output = 0;
}

/*
 * Simple W7 scrollback mode.
 *
 * The current buffer is displayed from
 * oldest to newest. Press q to leave.
 */
static void enter_scrollback(
    Session *session
)
{
    if (session == NULL)
        return;

    terminal_restore();

    printf(
        "\n[PtyMux] Scrollback mode - "
        "press q to exit\n"
    );

    display_session_buffer(session);

    printf(
        "\n[PtyMux] End of scrollback\n"
    );

    fflush(stdout);

    /*
     * Wait for q.
     */
    char ch;

    while (read(
               STDIN_FILENO,
               &ch,
               1
           ) == 1) {

        if (ch == 'q' ||
            ch == 'Q') {

            break;
        }
    }

    terminal_raw_mode();

    update_terminal_size(session);
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

        fd_set read_fds;

        int max_fd =
            STDIN_FILENO;

        int session_switched = 0;

        /*
         * Handle terminal resize.
         */
        if (resize_pending) {

            resize_pending = 0;

            for (int i = 0;
                 i < mux.session_count;
                 i++) {

                Session *session =
                    &mux.sessions[i];

                if (session_is_alive(
                        session
                    )) {

                    update_terminal_size(
                        session
                    );
                }
            }
        }

        FD_ZERO(&read_fds);

        /*
         * Keyboard.
         */
        FD_SET(
            STDIN_FILENO,
            &read_fds
        );

        /*
         * All session PTYs.
         */
        for (int i = 0;
             i < mux.session_count;
             i++) {

            Session *session =
                &mux.sessions[i];

            if (!session_is_alive(
                    session
                )) {

                continue;
            }

            FD_SET(
                session->master_fd,
                &read_fds
            );

            if (session->master_fd >
                max_fd) {

                max_fd =
                    session->master_fd;
            }
        }

        /*
         * Wait for input/output.
         */
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

        /*
         * Keyboard input.
         */
        if (FD_ISSET(
                STDIN_FILENO,
                &read_fds
            )) {

            char buffer[4096];

            ssize_t n =
                read(
                    STDIN_FILENO,
                    buffer,
                    sizeof(buffer)
                );

            if (n <= 0)
                break;

            for (ssize_t i = 0;
                 i < n;
                 i++) {

                char ch =
                    buffer[i];

                /*
                 * Ctrl-B.
                 */
                if (!command_mode &&
                    ch == 2) {

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
                     * Literal Ctrl-B.
                     */
                    if (command.type ==
                        COMMAND_LITERAL_PREFIX) {

                        Session *active =
                            multiplexer_get_active(
                                &mux
                            );

                        if (active != NULL) {

                            char prefix = 2;

                            write(
                                active->master_fd,
                                &prefix,
                                1
                            );
                        }

                        continue;
                    }

                    /*
                     * Save current session.
                     */
                    int old_session =
                        mux.active_session;

                    /*
                     * Scrollback.
                     */
                    if (command.type ==
                        COMMAND_SCROLLBACK) {

                        Session *active =
                            multiplexer_get_active(
                                &mux
                            );

                        enter_scrollback(
                            active
                        );

                        continue;
                    }

                    /*
                     * Execute normal command.
                     */
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
                     * Detect session switch.
                     */
                    if (mux.active_session !=
                        old_session) {

                        session_switched = 1;

                        Session *active =
                            multiplexer_get_active(
                                &mux
                            );

                        update_terminal_size(
                            active
                        );

                        /*
                         * Display buffered output
                         * from the newly active
                         * session.
                         */
                        display_session_buffer(
                            active
                        );
                    }

                    continue;
                }

                /*
                 * Normal keyboard input.
                 */
                Session *active =
                    multiplexer_get_active(
                        &mux
                    );

                if (active == NULL)
                    continue;

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
         * Read output from all PTYs.
         */
        for (int i = 0;
             i < mux.session_count;
             i++) {

            Session *session =
                &mux.sessions[i];

            if (!session_is_alive(
                    session
                )) {

                continue;
            }

            if (!FD_ISSET(
                    session->master_fd,
                    &read_fds
                )) {

                continue;
            }

            char buffer[4096];

            ssize_t n =
                read(
                    session->master_fd,
                    buffer,
                    sizeof(buffer)
                );

            if (n <= 0) {

                session->state =
                    SESSION_DEAD;

                continue;
            }

            /*
             * Always buffer output.
             */
            session_buffer_write(
                session,
                buffer,
                n
            );

            /*
             * Only display output from
             * the active session.
             */
            if (!session_switched &&
                session->id ==
                    mux.active_session) {

                session->has_unread_output =
                    0;

                if (write(
                        STDOUT_FILENO,
                        buffer,
                        n
                    ) == -1) {

                    running = 0;
                    break;
                }
            }
        }
    }

    terminal_restore();

    multiplexer_cleanup(&mux);

    return EXIT_SUCCESS;
}