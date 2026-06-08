#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#define LISTEN_BACKLOG 10
#define RECV_DATA_MAX_LEN 1024
#define TIMESTAMP_PREFIX_LEN 10

#if USE_AESD_CHAR_DEVICE
#define OUTPUT_PATH "/dev/aesdchar"
#else
#define OUTPUT_PATH "/var/tmp/aesdsocketdata"
#endif

struct entry {
	pthread_t thread;
	SLIST_ENTRY(entry) entries;
};

SLIST_HEAD(slisthead, entry);

static bool running = true;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static int f_fd = -1;
static struct entry *last_entry = NULL;

static void signal_handler(int signum)
{
	printf("Received signal %d\n", signum);
	running = false;
}

static void *handle_thread(void *arg)
{
	int client_fd = *(int *)arg;
	ssize_t bytes_read;
	bool complete_packet = false;
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	char client_ip[INET_ADDRSTRLEN] = { 0 };
	pthread_t tid = pthread_self();

	if (getpeername(client_fd, (struct sockaddr *)&client_addr, &client_addr_len) == 0) {
		if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip)) != NULL) {
			syslog(LOG_INFO, "Accepted connection from %s", client_ip);
			printf("aesdsocket: thread %ld accepted connection from %s\n", (unsigned long)tid, client_ip);
		}
	}

	char *buffer = malloc(RECV_DATA_MAX_LEN);
	if (buffer == NULL) {
		perror("malloc");
		free(arg);
		return NULL;
	}

	do {
		bytes_read = recv(client_fd, buffer, RECV_DATA_MAX_LEN, 0);
		if (bytes_read == 0) {
			break;
		} else if (bytes_read < 0) {
			perror("recv");
			break;
		} else {
			printf("aesdsocket: thread %ld received %d bytes\n", (unsigned long)tid, (int)bytes_read);
		}

		(void)pthread_mutex_lock(&mutex);
		f_fd = open(OUTPUT_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (f_fd < 0) {
            perror("open");
            (void)pthread_mutex_unlock(&mutex);
            break;
        }
		ssize_t f_bytes_written = write(f_fd, buffer, bytes_read);
		close(f_fd);
		(void)pthread_mutex_unlock(&mutex);
		if (f_bytes_written < 0) {
			perror("write");
			break;
		}

		if (buffer[bytes_read - 1] == '\n') {
			complete_packet = true;
		}
	} while (!complete_packet);

	if (!complete_packet) {
		syslog(LOG_INFO, "Connection closed from %s", client_ip);
		printf("aesdsocket: thread %ld closed connection\n", (unsigned long)tid);
		close(client_fd);
		free(buffer);
		free(arg);
		return NULL;
	}

	ssize_t total_bytes_read = 0;
	do {
		(void)pthread_mutex_lock(&mutex);
		f_fd = open(OUTPUT_PATH, O_RDONLY, 0666);
        if (f_fd < 0) {
            perror("open");
            (void)pthread_mutex_unlock(&mutex);
            break;
        }
		if (lseek(f_fd, total_bytes_read, SEEK_SET) < 0) {
			perror("lseek");
			close(f_fd);
			(void)pthread_mutex_unlock(&mutex);
			break;
		}
		ssize_t f_bytes_read = read(f_fd, buffer, RECV_DATA_MAX_LEN);
		close(f_fd);
		(void)pthread_mutex_unlock(&mutex);
		if (f_bytes_read < 0) {
			perror("read");
			break;
		}

		if (f_bytes_read == 0) {
			break;
		}

		total_bytes_read += f_bytes_read;

		printf("aesdsocket: thread %ld sending %d bytes\n", (unsigned long)tid, (int)f_bytes_read);
		ssize_t bytes_written = send(client_fd, buffer, f_bytes_read, 0);
		if (bytes_written < 0) {
			perror("send");
			break;
		}

	} while (true);

	syslog(LOG_INFO, "Connection closed from %s", client_ip);
	printf("aesdsocket: thread %ld closed connection\n", (unsigned long)tid);
	close(client_fd);
	free(buffer);
	free(arg);
	return NULL;
}

#if !USE_AESD_CHAR_DEVICE
static void itimer_handler(union sigval value)
{
	printf("Received signal %d\n", value.sival_int);
	time_t t;
	struct tm *tm;
	char buffer[256] = { 't', 'i', 'm', 'e', 's', 't', 'a', 'm', 'p', ':' };
	int bytes_read;

	t = time(NULL);
	tm = localtime(&t);
	bytes_read = strftime(&buffer[TIMESTAMP_PREFIX_LEN], (sizeof(buffer) - TIMESTAMP_PREFIX_LEN),
			      "%Y-%m-%d %H:%M:%S", tm);
	if (bytes_read == 0) {
		perror("strftime");
		return;
	}
	bytes_read += TIMESTAMP_PREFIX_LEN;
	buffer[bytes_read] = '\n';
	bytes_read++;

	(void)pthread_mutex_lock(&mutex);
	f_fd = open(OUTPUT_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
	ssize_t f_bytes_written = write(f_fd, buffer, bytes_read);
	close(f_fd);
	(void)pthread_mutex_unlock(&mutex);
	if (f_bytes_written < 0) {
		perror("write");
	}
}
#endif

int main(int argc, char *argv[])
{
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

#if !USE_AESD_CHAR_DEVICE
	timer_t timerid;
	struct itimerspec itimer = {
		.it_interval = { .tv_sec = 10, .tv_nsec = 0 },
		.it_value = { .tv_sec = 10, .tv_nsec = 0 },
	};
	struct sigevent sev = {
		.sigev_notify = SIGEV_THREAD,
		._sigev_un._sigev_thread._function = itimer_handler,
		.sigev_value.sival_ptr = &timerid,
	};
	if (timer_create(CLOCK_REALTIME, &sev, &timerid) < 0) {
		perror("timer_create");
		return -1;
	}
	if (timer_settime(timerid, 0, &itimer, NULL) < 0) {
		perror("timer_settime");
		return -1;
	}
#endif

	printf("aesdsocket: listening on port 9000\n");
	openlog("aesdsocket", 0, LOG_USER);

	struct slisthead head;
	SLIST_INIT(&head);

	while (running) {
		int client_fd = accept(socket_fd, NULL, NULL);
		if (client_fd < 0) {
			perror("accept");
			close(client_fd);
			continue;
		}

		pthread_t thread;
		int *client_fd_ptr = malloc(sizeof(int));
		*client_fd_ptr = client_fd;
		int ret = pthread_create(&thread, NULL, handle_thread, client_fd_ptr);
		if (ret == 0) {
			struct entry *entry = malloc(sizeof(struct entry));
			entry->thread = thread;
			if (SLIST_EMPTY(&head)) {
				SLIST_INSERT_HEAD(&head, entry, entries);
			} else {
				SLIST_INSERT_AFTER(last_entry, entry, entries);
			}
			last_entry = entry;
		}
	}

	printf("aesdsocket: exiting\n");

	// clean up
	struct entry *entry;
	SLIST_FOREACH(entry, &head, entries)
	{
		pthread_join(entry->thread, NULL);
		printf("aesdsocket: thread %ld joined\n", (unsigned long)entry->thread);
	}
	while (!SLIST_EMPTY(&head)) {
		entry = SLIST_FIRST(&head);
		SLIST_REMOVE_HEAD(&head, entries);
		free(entry);
	}
	closelog();
	close(socket_fd);
#if !USE_AESD_CHAR_DEVICE
	if (unlink(OUTPUT_PATH) < 0) {
		perror("unlink");
	}
#endif

	return 0;
}
