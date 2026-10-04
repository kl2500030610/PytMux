#ifndef SESSION_H
#define SESSION_H

#include <stddef.h>
#include <sys/types.h>

#define MAX_SESSIONS 16
#define SESSION_NAME_SIZE 64
#define OUTPUT_BUFFER_SIZE 8192

typedef enum {
    SESSION_DEAD = 0,
    SESSION_RUNNING
} SessionState;

typedef struct {
    int id;
    pid_t pid;
    int master_fd;

    char name[SESSION_NAME_SIZE];

    SessionState state;

    char output_buffer[OUTPUT_BUFFER_SIZE];
    size_t buffer_start;
    size_t buffer_end;
} Session;

void session_init(Session *session, int id, const char *name);

void session_reset(Session *session);

int session_is_alive(const Session *session);

int session_buffer_write(
    Session *session,
    const char *data,
    size_t size
);

size_t session_buffer_read(
    Session *session,
    char *data,
    size_t size
);

#endif