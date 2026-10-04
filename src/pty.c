#define _XOPEN_SOURCE 600

#include "pty.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>

int create_pty_shell(void)
{
    int master_fd;
    pid_t pid;
    char *slave_name;

    master_fd = posix_openpt(O_RDWR | O_NOCTTY);

    if (master_fd == -1) {
        perror("posix_openpt");
        return -1;
    }

    if (grantpt(master_fd) == -1) {
        perror("grantpt");
        close(master_fd);
        return -1;
    }

    if (unlockpt(master_fd) == -1) {
        perror("unlockpt");
        close(master_fd);
        return -1;
    }

    slave_name = ptsname(master_fd);

    if (slave_name == NULL) {
        perror("ptsname");
        close(master_fd);
        return -1;
    }

    pid = fork();

    if (pid == -1) {
        perror("fork");
        close(master_fd);
        return -1;
    }

    if (pid == 0) {
        int slave_fd;

        close(master_fd);

        if (setsid() == -1) {
            perror("setsid");
            exit(EXIT_FAILURE);
        }

        slave_fd = open(slave_name, O_RDWR);

        if (slave_fd == -1) {
            perror("open slave PTY");
            exit(EXIT_FAILURE);
        }

        if (ioctl(slave_fd, TIOCSCTTY, 0) == -1) {
            perror("TIOCSCTTY");
            close(slave_fd);
            exit(EXIT_FAILURE);
        }

        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);

        if (slave_fd > STDERR_FILENO)
            close(slave_fd);

        execlp("/bin/bash", "bash", "--noprofile", "--norc", NULL);

        perror("exec");
        exit(EXIT_FAILURE);
    }

    return master_fd;
}