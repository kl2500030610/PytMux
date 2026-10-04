#include "commands.h"

#include <stdio.h>
#include <unistd.h>

Command parse_command(char input)
{
    Command command;

    command.type = COMMAND_NONE;
    command.session_id = -1;

    if (input >= '0' && input <= '9') {
        command.type = COMMAND_SWITCH;
        command.session_id = input - '0';

        return command;
    }

    switch (input) {

    case 'c':
        command.type = COMMAND_CREATE;
        break;

    case 'l':
        command.type = COMMAND_LIST;
        break;

    case 'q':
        command.type = COMMAND_QUIT;
        break;

    case 2:
        command.type = COMMAND_LITERAL_PREFIX;
        break;

    default:
        command.type = COMMAND_NONE;
        break;
    }

    return command;
}

int execute_command(
    Multiplexer *mux,
    Command command
)
{
    if (mux == NULL)
        return 0;

    switch (command.type) {

    case COMMAND_CREATE:
        {
            char name[SESSION_NAME_SIZE];

            snprintf(
                name,
                sizeof(name),
                "bash-%d",
                mux->session_count
            );

            int id = multiplexer_create_session(
                mux,
                name
            );

            if (id == -1) {
                const char *message =
                    "\r\033[2K[Failed to create session]\r\n";

                write(
                    STDOUT_FILENO,
                    message,
                    31
                );

                return 1;
            }

            char message[128];

            int len = snprintf(
                message,
                sizeof(message),
                "\r\033[2K[Created session %d]\r\n",
                id
            );

            write(
                STDOUT_FILENO,
                message,
                len
            );

            return 1;
        }

    case COMMAND_LIST:
        multiplexer_list_sessions(mux);
        return 1;

    case COMMAND_QUIT:
        return -1;

    case COMMAND_SWITCH:
        if (multiplexer_switch_session(
                mux,
                command.session_id
            ) == 0) {

            char message[128];

            int len = snprintf(
                message,
                sizeof(message),
                "\r\033[2K[Switched to session %d]\r\n",
                command.session_id
            );

            write(
                STDOUT_FILENO,
                message,
                len
            );

            return 1;
        }

        {
            const char *message =
                "\r\033[2K[Invalid session]\r\n";

            write(
                STDOUT_FILENO,
                message,
                24
            );
        }

        return 1;

    case COMMAND_LITERAL_PREFIX:
        return 2;

    case COMMAND_NONE:
    default:
        {
            const char *message =
                "\r\033[2K[Unknown command]\r\n";

            write(
                STDOUT_FILENO,
                message,
                23
            );
        }

        return 1;
    }
}