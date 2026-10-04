#ifndef MULTIPLEXER_H
#define MULTIPLEXER_H

#include "session.h"

typedef struct {
    Session sessions[MAX_SESSIONS];
    int session_count;
    int active_session;
} Multiplexer;

void multiplexer_init(Multiplexer *mux);

int multiplexer_create_session(
    Multiplexer *mux,
    const char *name
);

int multiplexer_switch_session(
    Multiplexer *mux,
    int session_id
);

void multiplexer_list_sessions(
    const Multiplexer *mux
);

Session *multiplexer_get_active(
    Multiplexer *mux
);

void multiplexer_cleanup(
    Multiplexer *mux
);

#endif