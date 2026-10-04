#include "commands.h"

#include <stdio.h>

Command parse_command(char input)
{
    Command command;

    command.type = COMMAND_NONE;
    command.session_id = -1;

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

        case '[':
            command.type = COMMAND_SCROLLBACK;
            break;

        case 2:
            command.type =
                COMMAND_LITERAL_PREFIX;
            break;

        default:

            if (input >= '0' &&
                input <= '9') {

                command.type =
                    COMMAND_SWITCH;

                command.session_id =
                    input - '0';
            }

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
        return -1;

    switch (command.type) {

        case COMMAND_CREATE:
        {
            char name[64];

            snprintf(
                name,
                sizeof(name),
                "bash-%d",
                mux->session_count
            );

            int id =
                multiplexer_create_session(
                    mux,
                    name
                );

            if (id == -1)
                return 0;

            printf(
                "\r\033[2K[Created session %d]\n",
                id
            );

            break;
        }

        case COMMAND_LIST:

            multiplexer_list_sessions(
                mux
            );

            break;

        case COMMAND_SWITCH:

            if (multiplexer_switch_session(
                    mux,
                    command.session_id
                ) == 0) {

                printf(
                    "\r\033[2K[Switched to session %d]\n",
                    command.session_id
                );

            } else {

                printf(
                    "\r\033[2K[Invalid session]\n"
                );
            }

            break;

        case COMMAND_SCROLLBACK:

            return 1;

        case COMMAND_QUIT:

            return -1;

        case COMMAND_LITERAL_PREFIX:
        case COMMAND_NONE:
        default:

            break;
    }

    return 0;
}