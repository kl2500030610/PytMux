#include "commands.h"

#include <stdio.h>
#include <unistd.h>

int handle_command(
    Multiplexer *mux,
    char command
)
{
    if (mux == NULL)
        return 0;

    switch (command) {

    case 'c':
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

    case 'l':
        multiplexer_list_sessions(mux);
        return 1;

    default:
        return 0;
    }
}