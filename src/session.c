#include "session.h"

#include <string.h>
#include <unistd.h>

void session_init(
    Session *session,
    int id,
    const char *name
)
{
    if (session == NULL)
        return;

    memset(
        session,
        0,
        sizeof(Session)
    );

    session->id = id;
    session->pid = -1;
    session->master_fd = -1;
    session->state = SESSION_DEAD;

    session->buffer_start = 0;
    session->buffer_end = 0;
    session->has_unread_output = 0;

    if (name != NULL) {
        strncpy(
            session->name,
            name,
            SESSION_NAME_SIZE - 1
        );

        session->name[
            SESSION_NAME_SIZE - 1
        ] = '\0';
    }
}

void session_reset(
    Session *session
)
{
    if (session == NULL)
        return;

    if (session->master_fd != -1) {
        close(session->master_fd);
    }

    session->pid = -1;
    session->master_fd = -1;
    session->state = SESSION_DEAD;

    session->buffer_start = 0;
    session->buffer_end = 0;
    session->has_unread_output = 0;
}

int session_is_alive(
    const Session *session
)
{
    if (session == NULL)
        return 0;

    return session->state == SESSION_RUNNING;
}

int session_buffer_write(
    Session *session,
    const char *data,
    size_t size
)
{
    if (session == NULL ||
        data == NULL)
        return -1;

    for (size_t i = 0; i < size; i++) {

        session->output_buffer[
            session->buffer_end
        ] = data[i];

        session->buffer_end =
            (session->buffer_end + 1)
            % OUTPUT_BUFFER_SIZE;

        if (session->buffer_end ==
            session->buffer_start) {

            session->buffer_start =
                (session->buffer_start + 1)
                % OUTPUT_BUFFER_SIZE;
        }
    }

    session->has_unread_output = 1;

    return 0;
}

size_t session_buffer_read(
    Session *session,
    char *data,
    size_t size
)
{
    size_t count = 0;

    if (session == NULL ||
        data == NULL ||
        size == 0)
        return 0;

    while (
        session->buffer_start !=
            session->buffer_end &&
        count < size
    ) {
        data[count++] =
            session->output_buffer[
                session->buffer_start
            ];

        session->buffer_start =
            (session->buffer_start + 1)
            % OUTPUT_BUFFER_SIZE;
    }

    if (session->buffer_start ==
        session->buffer_end) {

        session->has_unread_output = 0;
    }

    return count;
}

size_t session_buffer_size(
    const Session *session
)
{
    if (session == NULL)
        return 0;

    if (session->buffer_end >=
        session->buffer_start) {

        return session->buffer_end -
               session->buffer_start;
    }

    return OUTPUT_BUFFER_SIZE -
           session->buffer_start +
           session->buffer_end;
}