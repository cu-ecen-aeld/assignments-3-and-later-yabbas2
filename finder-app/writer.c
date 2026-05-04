#include <syslog.h>
#include <stdio.h>


int main(int argc, char *argv[]) {
    openlog(NULL, 0, LOG_USER);

    // check number of arguments
    if (argc != 3) {
        syslog(LOG_ERR, "Usage: writer <path> <string>");
        return 1;
    }

    const char* path = argv[1];
    const char* string = argv[2];

    // assuming path already exists
    FILE* file = fopen(path, "w");
    if (file == NULL) {
        syslog(LOG_ERR, "Failed to open file %s", path);
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", string, path);
    if (fputs(string, file) == EOF) {
        syslog(LOG_ERR, "Failed to write to file %s", path);
        fclose(file);
        return 1;
    }

    fclose(file);
    closelog();
    return 0;
}
