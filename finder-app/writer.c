
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>

/*
 * Write all bytes to a file descriptor.
 * Handles partial writes and interrupted system calls.
 */
static int write_all(int fd, const char *buffer, size_t count)
{
    size_t total_written = 0;

    while (total_written < count) {
        ssize_t bytes_written = write(fd,
                                      buffer + total_written,
                                      count - total_written);

        if (bytes_written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (bytes_written == 0) {
            errno = EIO;
            return -1;
        }

        total_written += (size_t)bytes_written;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int fd;

    /* Configure syslog using the LOG_USER facility. */
    openlog("writer", LOG_PID, LOG_USER);

    /* Validate arguments. */
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <writefile> <writestr>\n", argv[0]);
        closelog();
        return 1;
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];

    if (strlen(writefile) == 0 || strlen(writestr) == 0) {
        fprintf(stderr, "Error: Parameters cannot be empty.\n");
        closelog();
        return 1;
    }

    /* Create or overwrite the destination file. */
    fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd == -1) {
        syslog(LOG_ERR, "Failed to open file %s: %s",
               writefile, strerror(errno));
        closelog();
        return 1;
    }

    /* Write the string followed by a newline. */
    if (write_all(fd, writestr, strlen(writestr)) == -1 ||
        write_all(fd, "\n", 1) == -1) {

        int saved_errno = errno;

        syslog(LOG_ERR, "Failed to write to file %s: %s",
               writefile, strerror(saved_errno));

        close(fd);
        closelog();
        return 1;
    }

    /* Close the file and check for errors. */
    if (close(fd) == -1) {
        syslog(LOG_ERR, "Failed to close file %s: %s",
               writefile, strerror(errno));
        closelog();
        return 1;
    }

    /* Log successful write operation. */
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    closelog();
    return 0;
}
