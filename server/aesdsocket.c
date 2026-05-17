#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define LISTEN_BACKLOG 10
#define TMP_FILE_PATH "/var/tmp/aesdsocketdata"
#define RECV_DATA_MAX_LEN 5000

static int recvCompletePacket(int client_fd, char *recv_buffer, ssize_t bytes_left) {
    ssize_t bytes_read;

    do {
        if (bytes_left <= 0) {
            return -1; // no enough space to receive more data
        }

        bytes_read = recv(client_fd, recv_buffer, bytes_left, 0);
        if (bytes_read == 0) {
            return 0; // connection closed
        } else if (bytes_read < 0) {
            perror("recv");
            return -1;
        } else {
            recv_buffer += bytes_read;
            bytes_left -= bytes_read;
            printf("aesdsocket: received %d bytes\n", (int)bytes_read);
        }

    } while (recv_buffer[bytes_read - 1] != '\n');

    return bytes_read;
}

int main(int argc, char *argv[]) {
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

    if (listen(socket_fd, LISTEN_BACKLOG) != 0) {
        perror("listen");
        close(socket_fd);
        return -1;
    }

    printf("aesdsocket: listening on port 9000\n");
    openlog("aesdsocket", 0, LOG_USER);

    while (1) {
        struct sockaddr client_addr;
        socklen_t client_addr_len = sizeof(struct sockaddr);
        int client_fd = accept(socket_fd, &client_addr, &client_addr_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &((struct sockaddr_in *)&client_addr)->sin_addr, client_ip, sizeof(client_ip)) != NULL) {
            syslog(LOG_INFO, "Accepted connection from %s", client_ip);
        }
        printf("aesdsocket: accepted connection from %s\n", client_ip);

        char *recv_buffer = malloc(RECV_DATA_MAX_LEN);
        if (recv_buffer == NULL) {
            perror("malloc");
            close(client_fd);
            continue;
        }

        ssize_t bytes_left = RECV_DATA_MAX_LEN;
        ssize_t bytes_read = recvCompletePacket(client_fd, recv_buffer, bytes_left);
        if (bytes_read == 0) {
            syslog(LOG_INFO, "Connection closed from %s", client_ip);
            free(recv_buffer);
            close(client_fd);
            continue;
        } else if (bytes_read < 0) {
            free(recv_buffer);
            close(client_fd);
            continue;
        } else {
            // continue processing
        }

        FILE *f = fopen(TMP_FILE_PATH, "a");
        if (f == NULL) {
            perror("fopen");
            free(recv_buffer);
            close(client_fd);
            continue;
        }

        fwrite(recv_buffer, 1, bytes_read, f);
        fclose(f);

        // send response to client
        ssize_t bytes_written = send(client_fd, recv_buffer, bytes_read, 0);
        if (bytes_written < 0) {
            perror("send");
        }

        free(recv_buffer);
        close(client_fd);
    }

    closelog();
    close(socket_fd);

    return 0;
}
