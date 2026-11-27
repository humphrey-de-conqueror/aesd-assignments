
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

#define SERVER_PORT 9000
#define LISTEN_BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define RECV_BUFFER_SIZE 1024

static volatile sig_atomic_t caught_signal = 0;

static void signal_handler(int signal_number)
{
    caught_signal = signal_number;
}

static int write_all(int fd, const char *buffer, size_t length)
{
    size_t total_written = 0;

    while (total_written < length) {
        ssize_t written = write(fd,
                                buffer + total_written,
                                length - total_written);

        if (written == -1) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        total_written += (size_t)written;
    }

    return 0;
}

static int append_packet(const char *buffer, size_t length)
{
    int fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (fd == -1) {
        syslog(LOG_ERR, "open %s: %s", DATA_FILE, strerror(errno));
        return -1;
    }

    int result = write_all(fd, buffer, length);

    if (result == -1) {
        syslog(LOG_ERR, "write %s: %s", DATA_FILE, strerror(errno));
    }

    if (close(fd) == -1) {
        syslog(LOG_ERR, "close %s: %s", DATA_FILE, strerror(errno));
        result = -1;
    }

    return result;
}

static int send_all(int fd, const char *buffer, size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t sent = send(fd,
                            buffer + total_sent,
                            length - total_sent,
                            MSG_NOSIGNAL);

        if (sent == -1) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (sent == 0) {
            return -1;
        }

        total_sent += (size_t)sent;
    }

    return 0;
}

static int send_file_to_client(int client_fd)
{
    char buffer[RECV_BUFFER_SIZE];

    int fd = open(DATA_FILE, O_RDONLY);

    if (fd == -1) {
        syslog(LOG_ERR, "open %s: %s", DATA_FILE, strerror(errno));
        return -1;
    }

    int result = 0;
    ssize_t bytes_read;

    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        if (send_all(client_fd, buffer, (size_t)bytes_read) == -1) {
            syslog(LOG_ERR, "send: %s", strerror(errno));
            result = -1;
            break;
        }
    }

    if (bytes_read == -1) {
        syslog(LOG_ERR, "read %s: %s", DATA_FILE, strerror(errno));
        result = -1;
    }

    if (close(fd) == -1) {
        syslog(LOG_ERR, "close %s: %s", DATA_FILE, strerror(errno));
        result = -1;
    }

    return result;
}

static int handle_client(int client_fd)
{
    char recv_buffer[RECV_BUFFER_SIZE];
    char *packet = NULL;
    size_t packet_length = 0;
    int result = 0;

    while (1) {
        ssize_t bytes_received = recv(client_fd,
                                      recv_buffer,
                                      sizeof(recv_buffer),
                                      0);

        if (bytes_received == -1) {
            if (errno == EINTR && caught_signal != 0) {
                break;
            }

            if (errno == EINTR) {
                continue;
            }

            syslog(LOG_ERR, "recv: %s", strerror(errno));
            result = -1;
            break;
        }

        if (bytes_received == 0) {
            break;
        }

        size_t offset = 0;
        size_t received_length = (size_t)bytes_received;

        while (offset < received_length) {
            char *newline = memchr(recv_buffer + offset,
                                   '\n',
                                   received_length - offset);

            size_t segment_length;

            if (newline != NULL) {
                segment_length = (size_t)(newline -
                                  (recv_buffer + offset)) + 1;
            } else {
                segment_length = received_length - offset;
            }

            char *new_packet = realloc(packet,
                                       packet_length + segment_length);

            if (new_packet == NULL) {
                syslog(LOG_ERR, "realloc failed");
                result = -1;
                goto cleanup;
            }

            packet = new_packet;

            memcpy(packet + packet_length,
                   recv_buffer + offset,
                   segment_length);

            packet_length += segment_length;
            offset += segment_length;

            if (newline != NULL) {
                if (append_packet(packet, packet_length) == -1) {
                    result = -1;
                    goto cleanup;
                }

                if (send_file_to_client(client_fd) == -1) {
                    result = -1;
                    goto cleanup;
                }

                free(packet);
                packet = NULL;
                packet_length = 0;
            }
        }
    }

cleanup:
    free(packet);
    return result;
}

int main(int argc, char *argv[])
{
    int daemon_mode = 0;

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    } else if (argc != 1) {
        fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
        return EXIT_FAILURE;
    }

    int server_fd = -1;
    int client_fd = -1;
    int ret = EXIT_FAILURE;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1 ||
        sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "sigaction: %s", strerror(errno));
        goto cleanup;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "socket: %s", strerror(errno));
        goto cleanup;
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) == -1) {
        syslog(LOG_ERR, "setsockopt: %s", strerror(errno));
        goto cleanup;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "bind: %s", strerror(errno));
        goto cleanup;
    }

    if (daemon_mode) {
        pid_t pid = fork();

        if (pid == -1) {
            syslog(LOG_ERR, "fork: %s", strerror(errno));
            goto cleanup;
        }

        if (pid > 0) {
            close(server_fd);
            closelog();
            return EXIT_SUCCESS;
        }

        if (setsid() == -1) {
            syslog(LOG_ERR, "setsid: %s", strerror(errno));
            goto cleanup;
        }

        if (chdir("/") == -1) {
            syslog(LOG_ERR, "chdir: %s", strerror(errno));
            goto cleanup;
        }

        umask(0);

        int devnull = open("/dev/null", O_RDWR);
        if (devnull != -1) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) {
                close(devnull);
            }
        }
    }

    if (listen(server_fd, LISTEN_BACKLOG) == -1) {
        syslog(LOG_ERR, "listen: %s", strerror(errno));
        goto cleanup;
    }

    printf("Listening on port %d\n", SERVER_PORT);

    ret = EXIT_SUCCESS;

    while (caught_signal == 0) {
        socklen_t client_addr_len = sizeof(client_addr);

        client_fd = accept(server_fd,
                           (struct sockaddr *)&client_addr,
                           &client_addr_len);

        if (client_fd == -1) {
            if (errno == EINTR && caught_signal != 0) {
                break;
            }

            if (errno == EINTR) {
                continue;
            }

            syslog(LOG_ERR, "accept: %s", strerror(errno));
            ret = EXIT_FAILURE;
            break;
        }

        char client_ip[INET_ADDRSTRLEN];

        if (inet_ntop(AF_INET, &client_addr.sin_addr,
                      client_ip, sizeof(client_ip)) == NULL) {
            syslog(LOG_ERR, "inet_ntop: %s", strerror(errno));
            close(client_fd);
            client_fd = -1;
            continue;
        }

        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        handle_client(client_fd);

        close(client_fd);
        client_fd = -1;

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

cleanup:
    if (caught_signal != 0) {
        syslog(LOG_INFO, "Caught signal, exiting");
        ret = EXIT_SUCCESS;
    }

    if (client_fd != -1) {
        close(client_fd);
    }

    if (server_fd != -1) {
        close(server_fd);
    }

    if (caught_signal != 0) {
        if (unlink(DATA_FILE) == -1 && errno != ENOENT) {
            syslog(LOG_ERR, "unlink %s: %s",
                DATA_FILE, strerror(errno));
            ret = EXIT_FAILURE;
        }
    }

    syslog(LOG_INFO, "Server exiting");
    closelog();

    return ret;
}
