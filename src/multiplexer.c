#define _POSIX_C_SOURCE 200809L

#include "multiplexer.h"
#include "pty.h"

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

void multiplexer_init(
    Multiplexer *mux
)
{
    if (mux == NULL)
        return;

    memset(
        mux,
        0,
        sizeof(Multiplexer)
    );

    mux->session_count = 0;
    mux->active_session = -1;

    for (int i = 0;
         i < MAX_SESSIONS;
         i++) {

        session_init(
            &mux->sessions[i],
            i,
            NULL
        );
    }
}

int multiplexer_create_session(
    Multiplexer *mux,
    const char *name
)
{
    if (mux == NULL)
        return -1;

    if (mux->session_count >=
        MAX_SESSIONS)
        return -1;

    int id =
        mux->session_count;

    Session *session =
        &mux->sessions[id];

    session_init(
        session,
        id,
        name
    );

    session->master_fd =
        create_pty_shell(
            &session->pid
        );

    if (session->master_fd == -1)
        return -1;

    session->state =
        SESSION_RUNNING;

    mux->session_count++;

    if (mux->active_session == -1)
        mux->active_session = id;

    return id;
}

int multiplexer_switch_session(
    Multiplexer *mux,
    int session_id
)
{
    if (mux == NULL)
        return -1;

    if (session_id < 0 ||
        session_id >=
            mux->session_count)
        return -1;

    Session *session =
        &mux->sessions[session_id];

    if (!session_is_alive(session))
        return -1;

    mux->active_session =
        session_id;

    return 0;
}

void multiplexer_list_sessions(
    const Multiplexer *mux
)
{
    if (mux == NULL)
        return;

    printf(
        "\r\nSessions:\r\n"
    );

    for (int i = 0;
         i < mux->session_count;
         i++) {

        const Session *session =
            &mux->sessions[i];

        printf(
            "  [%d] %-16s %s%s%s\r\n",
            session->id,
            session->name,

            session_is_alive(session)
                ? "RUNNING"
                : "DEAD",

            session->id ==
                mux->active_session
                ? "  <active>"
                : "",

            session->has_unread_output
                ? "  *"
                : ""
        );
    }

    printf(
        "\r\n"
    );
}

Session *multiplexer_get_active(
    Multiplexer *mux
)
{
    if (mux == NULL)
        return NULL;

    if (mux->active_session < 0 ||
        mux->active_session >=
            mux->session_count)
        return NULL;

    return &mux->sessions[
        mux->active_session
    ];
}

void multiplexer_cleanup(
    Multiplexer *mux
)
{
    if (mux == NULL)
        return;

    for (int i = 0;
         i < mux->session_count;
         i++) {

        Session *session =
            &mux->sessions[i];

        if (session->pid > 0 &&
            session_is_alive(session)) {

            kill(
                session->pid,
                SIGHUP
            );

            kill(
                session->pid,
                SIGTERM
            );

            waitpid(
                session->pid,
                NULL,
                0
            );
        }

        session_reset(
            session
        );
    }

    mux->session_count = 0;
    mux->active_session = -1;
}