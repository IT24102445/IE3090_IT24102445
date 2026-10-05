#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>

#define PORT 9410
#define SID_TAG "SID:5442"
#define AUTH_TOKEN "OPS-2445"
#define LOG_FILE "remoteops_IT24102445.log"
#define STORAGE_DIR "./agentfiles/IT24102445"

#define BUFFER_SIZE 4096

pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_event(const char *message) {
    pthread_mutex_lock(&log_mutex);
    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        time_t now = time(NULL);
        char *t_str = ctime(&now);
        t_str[strlen(t_str) - 1] = '\0';
        fprintf(f, "[%s] %s\n", t_str, message);
        fclose(f);
    }
    pthread_mutex_unlock(&log_mutex);
}

typedef struct {
    int active;
    int udp_port;
    struct sockaddr_in client_addr;
    pthread_t thread_id;
} udp_monitor_t;

typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
} client_args_t;

void *udp_monitor_thread(void *arg) {
    udp_monitor_t *mon = (udp_monitor_t *)arg;
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) return NULL;

    struct sockaddr_in dest_addr = mon->client_addr;
    dest_addr.sin_port = htons(mon->udp_port);

    while (mon->active) {
        char packet[256];
        snprintf(packet, sizeof(packet), "SYSINFO 0.15 512MB 3600 %s\n", SID_TAG);
        sendto(udp_fd, packet, strlen(packet), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        sleep(2);
    }
    close(udp_fd);
    return NULL;
}

int read_line(int socket_fd, char *buffer, size_t max_len) {
    size_t count = 0;
    while (count < max_len - 1) {
        char c;
        ssize_t res = recv(socket_fd, &c, 1, 0);
        if (res <= 0) return res;
        if (c == '\n') break;
        buffer[count++] = c;
    }
    buffer[count] = '\0';
    return count;
}

void *handle_client(void *arg) {
    client_args_t *args = (client_args_t *)arg;
    int client_fd = args->client_fd;
    struct sockaddr_in client_addr = args->client_addr;
    free(args);

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);

    char log_buf[256];
    snprintf(log_buf, sizeof(log_buf), "New TCP connection from %s", ip_str);
    log_event(log_buf);

    int authenticated = 0;
    udp_monitor_t monitor = {0, 0, client_addr, 0};
    char buffer[BUFFER_SIZE];

    while (1) {
        int bytes_read = read_line(client_fd, buffer, sizeof(buffer));
        if (bytes_read <= 0) break;

        snprintf(log_buf, sizeof(log_buf), "Received command from %s: %s", ip_str, buffer);
        log_event(log_buf);

        if (!authenticated) {
            char token[128];
            if (sscanf(buffer, "AUTH %127s", token) == 1) {
                if (strcmp(token, AUTH_TOKEN) == 0) {
                    authenticated = 1;
                    dprintf(client_fd, "OK AUTHENTICATED %s\n", SID_TAG);
                    log_event("Authentication successful");
                } else {
                    dprintf(client_fd, "ERR 001 AUTH_FAILED %s\n", SID_TAG);
                    log_event("Authentication failed: invalid token");
                }
            } else {
                dprintf(client_fd, "ERR 001 AUTH_FAILED %s\n", SID_TAG);
            }
            continue;
        }

        if (strncmp(buffer, "SYSINFO", 7) == 0) {
            dprintf(client_fd, "OK SYSINFO 0.15 512MB 3600 %s\n", SID_TAG);
        } else if (strncmp(buffer, "LISTPROC", 8) == 0) {
            dprintf(client_fd, "OK PROCS 1:init,102:systemd,445:agent %s\n", SID_TAG);
        } else if (strncmp(buffer, "EXEC ", 5) == 0) {
            char cmd[64];
            sscanf(buffer, "EXEC %63s", cmd);
            if (strcmp(cmd, "DATE") == 0 || strcmp(cmd, "UPTIME") == 0 || 
                strcmp(cmd, "DISKFREE") == 0 || strcmp(cmd, "HOSTNAME") == 0 || 
                strcmp(cmd, "WHOAMI") == 0) {
                
                FILE *fp = NULL;
                if (strcmp(cmd, "DATE") == 0) fp = popen("date", "r");
                else if (strcmp(cmd, "UPTIME") == 0) fp = popen("uptime", "r");
                else if (strcmp(cmd, "DISKFREE") == 0) fp = popen("df -h", "r");
                else if (strcmp(cmd, "HOSTNAME") == 0) fp = popen("hostname", "r");
                else if (strcmp(cmd, "WHOAMI") == 0) fp = popen("whoami", "r");

                if (fp) {
                    char out[256] = {0};
                    fgets(out, sizeof(out) - 1, fp);
                    out[strcspn(out, "\r\n")] = 0;
                    pclose(fp);
                    dprintf(client_fd, "OK EXEC_RESULT %s %s\n", out, SID_TAG);
                } else {
                    dprintf(client_fd, "ERR 003 EXEC_FAILED %s\n", SID_TAG);
                }
            } else {
                dprintf(client_fd, "ERR 002 COMMAND_NOT_ALLOWED %s\n", SID_TAG);
            }
        } else if (strncmp(buffer, "PUT ", 4) == 0) {
            char filename[256];
            long filesize = 0;
            if (sscanf(buffer, "PUT %255s %ld", filename, &filesize) == 2) {
                if (filesize > 10 * 1024 * 1024) {
                    dprintf(client_fd, "ERR 004 FILE_TOO_LARGE %s\n", SID_TAG);
                    continue;
                }
                char filepath[512];
                snprintf(filepath, sizeof(filepath), "%s/%s", STORAGE_DIR, filename);

                FILE *fp = fopen(filepath, "wb");
                if (!fp) {
                    dprintf(client_fd, "ERR 006 CANNOT_WRITE_FILE %s\n", SID_TAG);
                    continue;
                }

                long remaining = filesize;
                char fbuf[4096];
                while (remaining > 0) {
                    size_t to_read = (remaining < sizeof(fbuf)) ? remaining : sizeof(fbuf);
                    ssize_t read_bytes = recv(client_fd, fbuf, to_read, 0);
                    if (read_bytes <= 0) break;
                    fwrite(fbuf, 1, read_bytes, fp);
                    remaining -= read_bytes;
                }
                fclose(fp);
                dprintf(client_fd, "OK FILE_RECEIVED %s %s\n", filename, SID_TAG);
                snprintf(log_buf, sizeof(log_buf), "File uploaded successfully: %s", filename);
                log_event(log_buf);
            }
        } else if (strncmp(buffer, "GET ", 4) == 0) {
            char filename[256];
            sscanf(buffer, "GET %255s", filename);

            char filepath[512];
            snprintf(filepath, sizeof(filepath), "%s/%s", STORAGE_DIR, filename);

            FILE *fp = fopen(filepath, "rb");
            if (!fp) {
                dprintf(client_fd, "ERR 005 FILE_NOT_FOUND %s\n", SID_TAG);
                continue;
            }

            fseek(fp, 0, SEEK_END);
            long filesize = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            dprintf(client_fd, "OK FILE_SEND %s %ld %s\n", filename, filesize, SID_TAG);

            char fbuf[4096];
            size_t bytes;
            while ((bytes = fread(fbuf, 1, sizeof(fbuf), fp)) > 0) {
                send(client_fd, fbuf, bytes, 0);
            }
            fclose(fp);
            snprintf(log_buf, sizeof(log_buf), "File downloaded successfully: %s", filename);
            log_event(log_buf);
        } else if (strncmp(buffer, "MONITOR START ", 14) == 0) {
            int uport = 0;
            sscanf(buffer, "MONITOR START %d", &uport);
            if (!monitor.active) {
                monitor.active = 1;
                monitor.udp_port = uport;
                pthread_create(&monitor.thread_id, NULL, udp_monitor_thread, &monitor);
            }
            dprintf(client_fd, "OK MONITOR_STARTED %s\n", SID_TAG);
        } else if (strncmp(buffer, "MONITOR STOP", 12) == 0) {
            if (monitor.active) {
                monitor.active = 0;
                pthread_join(monitor.thread_id, NULL);
            }
            dprintf(client_fd, "OK MONITOR_STOPPED %s\n", SID_TAG);
        } else if (strncmp(buffer, "QUIT", 4) == 0) {
            dprintf(client_fd, "OK BYE %s\n", SID_TAG);
            break;
        } else {
            dprintf(client_fd, "ERR 000 UNKNOWN_COMMAND %s\n", SID_TAG);
        }
    }

    if (monitor.active) {
        monitor.active = 0;
        pthread_join(monitor.thread_id, NULL);
    }

    close(client_fd);
    snprintf(log_buf, sizeof(log_buf), "Client disconnected from %s", ip_str);
    log_event(log_buf);
    return NULL;
}

int main() {
    mkdir("./agentfiles", 0777);
    mkdir(STORAGE_DIR, 0777);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 10);

    printf("[Agent] Server started on port %d...\n", PORT);
    log_event("Agent service started.");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);

        if (new_socket >= 0) {
            client_args_t *args = malloc(sizeof(client_args_t));
            args->client_fd = new_socket;
            args->client_addr = client_addr;

            pthread_t thread_id;
            pthread_create(&thread_id, NULL, handle_client, args);
            pthread_detach(thread_id);
        }
    }

    close(server_fd);
    return 0;
}
