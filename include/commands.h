#ifndef COMMANDS_H
#define COMMANDS_H

#include "multiplexer.h"

typedef enum {
    COMMAND_NONE = 0,
    COMMAND_CREATE,
    COMMAND_LIST,
    COMMAND_QUIT,
    COMMAND_SWITCH,
    COMMAND_LITERAL_PREFIX
} CommandType;

typedef struct {
    CommandType type;
    int session_id;
} Command;

Command parse_command(char input);

int execute_command(
    Multiplexer *mux,
    Command command
);

#endif