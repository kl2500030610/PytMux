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

static void update_terminal_size(
    Session *session
)
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

static void display_scrollback(
    Session *session
)
{
    char buffer[4096];

    size_t total =
        session_buffer_size(
            session
        );

    if (total == 0) {

        printf(
            "[No scrollback available]\r\n"
        );

        return;
    }

    size_t position = 0;

    while (position < total) {

        size_t remaining =
            total - position;

        size_t chunk =
            remaining < sizeof(buffer)
                ? remaining
                : sizeof(buffer);

        size_t n =
            session_buffer_peek(
                session,
                buffer,
                total
            );

        if (n == 0)
            break;

        if (chunk > n - position)
            chunk = n - position;

        if (write(
                STDOUT_FILENO,
                buffer + position,
                chunk
            ) == -1) {

            break;
        }

        position += chunk;
    }
}

static void display_unread_output(
    Session *session
)
{
    char buffer[4096];

    if (session == NULL)
        return;

    size_t unread =
        session->unread_count;

    if (unread == 0)
        return;

    size_t position = 0;

    while (position < unread) {

        size_t remaining =
            unread - position;

        size_t chunk =
            remaining < sizeof(buffer)
                ? remaining
                : sizeof(buffer);

        size_t n =
            session_buffer_peek_unread(
                session,
                buffer,
                sizeof(buffer)
            );

        if (n == 0)
            break;

        if (chunk > n - position)
            chunk = n - position;

        if (write(
                STDOUT_FILENO,
                buffer + position,
                chunk
            ) == -1) {

            break;
        }

        position += chunk;
    }

    session_mark_displayed(
        session
    );
}

static void enter_scrollback(
    Session *session
)
{
    if (session == NULL)
        return;

    printf(
        "\r\n"
        "[PtyMux] Scrollback mode\r\n"
        "[PtyMux] Press q to exit, r to refresh\r\n"
        "\r\n"
    );

    display_scrollback(
        session
    );

    printf(
        "\r\n"
        "[PtyMux] End of scrollback\r\n"
    );

    fflush(stdout);

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

        if (ch == 'r' ||
            ch == 'R') {

            printf(
                "\033[2J\033[H"
            );

            display_scrollback(
                session
            );

            printf(
                "\r\n"
                "[PtyMux] End of scrollback\r\n"
            );

            fflush(stdout);
        }
    }

    terminal_raw_mode();

    update_terminal_size(
        session
    );
}

static int handle_command_input(
    Multiplexer *mux,
    char ch,
    int *command_mode,
    int *running
)
{
    if (!*command_mode &&
        ch == 2) {

        *command_mode = 1;

        const char *message =
            "\r\033[2K[PtyMux]";

        write(
            STDOUT_FILENO,
            message,
            13
        );

        return 1;
    }

    if (!*command_mode)
        return 0;

    Command command =
        parse_command(ch);

    *command_mode = 0;

    if (command.type ==
        COMMAND_LITERAL_PREFIX) {

        Session *active =
            multiplexer_get_active(
                mux
            );

        if (active != NULL) {

            char prefix = 2;

            write(
                active->master_fd,
                &prefix,
                1
            );
        }

        return 1;
    }

    if (command.type ==
        COMMAND_SCROLLBACK) {

        Session *active =
            multiplexer_get_active(
                mux
            );

        enter_scrollback(
            active
        );

        return 1;
    }

    int old_session =
        mux->active_session;

    int result =
        execute_command(
            mux,
            command
        );

    if (result == -1) {

        *running = 0;

        return 1;
    }

    if (mux->active_session !=
        old_session) {

        Session *active =
            multiplexer_get_active(
                mux
            );

        update_terminal_size(
            active
        );

        /*
         * When returning to a session,
         * display only the output that
         * arrived while that session
         * was inactive.
         */
        display_unread_output(
            active
        );
    }

    return 1;
}

int main(void)
{
    Multiplexer mux;

    int command_mode = 0;
    int running = 1;

    multiplexer_init(
        &mux
    );

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
        multiplexer_get_active(
            &mux
        )
    );

    while (running) {

        fd_set read_fds;

        int max_fd =
            STDIN_FILENO;

        int session_switched = 0;

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

        FD_ZERO(
            &read_fds
        );

        FD_SET(
            STDIN_FILENO,
            &read_fds
        );

        for (int i = 0;
             i < mux.session_count;
             i++) {

            Session *session =
                &mux.sessions[i];

            if (!session_is_alive(
                    session
                ))
                continue;

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

        if (FD_ISSET(
                STDIN_FILENO,
                &read_fds
            )) {

            char input[4096];

            ssize_t n =
                read(
                    STDIN_FILENO,
                    input,
                    sizeof(input)
                );

            if (n <= 0)
                break;

            Session *active =
                multiplexer_get_active(
                    &mux
                );

            for (ssize_t i = 0;
                 i < n;
                 i++) {

                char ch =
                    input[i];

                int previous_session =
                    mux.active_session;

                if (handle_command_input(
                        &mux,
                        ch,
                        &command_mode,
                        &running
                    )) {

                    if (!running)
                        break;

                    if (mux.active_session !=
                        previous_session) {

                        session_switched = 1;
                    }

                    active =
                        multiplexer_get_active(
                            &mux
                        );

                    continue;
                }

                active =
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
         * If the user switched sessions during
         * this select cycle, don't print stale
         * output from the old active session.
         */
        if (session_switched)
            continue;

        for (int i = 0;
             i < mux.session_count;
             i++) {

            Session *session =
                &mux.sessions[i];

            if (!session_is_alive(
                    session
                ))
                continue;

            if (!FD_ISSET(
                    session->master_fd,
                    &read_fds
                ))
                continue;

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

            session_buffer_write(
                session,
                buffer,
                n
            );

            if (session->id ==
                mux.active_session) {

                if (write(
                        STDOUT_FILENO,
                        buffer,
                        n
                    ) == -1) {

                    running = 0;

                    break;
                }

                /*
                 * This output was immediately
                 * displayed, so it is not unread.
                 */
                session_mark_displayed(
                    session
                );
            }
        }
    }

    terminal_restore();

    multiplexer_cleanup(
        &mux
    );

    return EXIT_SUCCESS;
}