#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define LISTEN_BACKLOG 10
#define TMP_FILE_PATH "/var/tmp/aesdsocketdata"
#define RECV_DATA_MAX_LEN 1024

static bool running = true;

static void signal_handler(int signum) {
    printf("Received signal %d\n", signum);
    running = false;
}

int main(int argc, char *argv[]) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigfillset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction");
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("sigaction");
        return -1;
    }

    struct addrinfo hints;
    struct addrinfo *result;
    int socket_fd;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int status = getaddrinfo(NULL, "9000", &hints, &result);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }

    struct addrinfo *p;
    for (p = result; p != NULL; p = p->ai_next) {
        socket_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (socket_fd < 0) {
            perror("socket");
            continue;
        }
        int opt = 1;
        if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            perror("setsockopt");
            close(socket_fd);
            continue;
        }
        if (bind(socket_fd, p->ai_addr, p->ai_addrlen) != 0) {
            perror("bind");
            close(socket_fd);
            continue;
        }
        break;
    }

    freeaddrinfo(result);

    if (p == NULL) {
        fprintf(stderr, "cannot create socket\n");
        return -1;
    }

    bool is_daemon = false;
    if (argc > 1) {
        if (strcmp(argv[1], "-d") == 0) {
            is_daemon = true;
        }
    }

    if (is_daemon) {
        if (daemon(0, 0) != 0) {
            perror("daemon");
            return -1;
        }
    }

    if (listen(socket_fd, LISTEN_BACKLOG) != 0) {
        perror("listen");
        close(socket_fd);
        return -1;
    }

    char *buffer = malloc(RECV_DATA_MAX_LEN);
    if (buffer == NULL) {
        perror("malloc");
        return -1;
    }

    int f_fd = open(TMP_FILE_PATH, O_APPEND | O_CREAT | O_RDWR, 0644);
    if (f_fd < 0) {
        perror("open");
        return -1;
    }

    printf("aesdsocket: listening on port 9000\n");
    openlog("aesdsocket", 0, LOG_USER);

    while (running) {
        struct sockaddr client_addr;
        socklen_t client_addr_len = sizeof(struct sockaddr);
        int client_fd = accept(socket_fd, &client_addr, &client_addr_len);
        if (client_fd < 0) {
            perror("accept");
            close(client_fd);
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &((struct sockaddr_in *)&client_addr)->sin_addr, client_ip, sizeof(client_ip)) != NULL) {
            syslog(LOG_INFO, "Accepted connection from %s", client_ip);
        }
        printf("aesdsocket: accepted connection from %s\n", client_ip);

        ssize_t bytes_read;
        bool complete_packet = false;
        do {
            bytes_read = recv(client_fd, buffer, RECV_DATA_MAX_LEN, 0);
            if (bytes_read == 0) {
                break;
            } else if (bytes_read < 0) {
                perror("recv");
                break;
            } else {
                printf("aesdsocket: received %d bytes\n", (int)bytes_read);
            }

            ssize_t f_bytes_written = write(f_fd, buffer, bytes_read);
            if (f_bytes_written < 0) {
                perror("write");
                break;
            }

            if (buffer[bytes_read - 1] == '\n') {
                complete_packet = true;
            }
        } while (!complete_packet && running);

        if (!complete_packet) {
            syslog(LOG_INFO, "Connection closed from %s", client_ip);
            close(client_fd);
            continue;
        }

        ssize_t total_bytes_read = 0;
        bool complete_file = false;
        do {
            if (lseek(f_fd, total_bytes_read, SEEK_SET) < 0) {
                perror("lseek");
                break;
            }

            ssize_t f_bytes_read = read(f_fd, buffer, RECV_DATA_MAX_LEN);
            if (f_bytes_read < 0) {
                perror("read");
                break;
            }

            if (f_bytes_read < RECV_DATA_MAX_LEN) {
                complete_file = true;
            }

            total_bytes_read += f_bytes_read;

            printf("aesdsocket: sending %d bytes\n", (int)f_bytes_read);
            ssize_t bytes_written = send(client_fd, buffer, f_bytes_read, 0);
            if (bytes_written < 0) {
                perror("send");
                break;
            }
        } while (!complete_file && running);

        syslog(LOG_INFO, "Connection closed from %s", client_ip);
        close(client_fd);
    }

    printf("aesdsocket: exiting\n");
    closelog();
    close(socket_fd);
    close(f_fd);
    free(buffer);
    if (unlink(TMP_FILE_PATH) < 0) {
        perror("unlink");
    }

    return 0;
}
