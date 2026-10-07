#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define AGENT_IP "127.0.0.1"
#define AGENT_PORT 9410
#define AUTH_TOKEN "OPS-2445"
#define BUFFER_SIZE 4096

int udp_running = 0;

void *udp_listener(void *arg) {
    int udp_port = *(int *)arg;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return NULL;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(udp_port);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return NULL;
    }

    printf("\n[UDP Listener] Started on port %d\ncontroller> ", udp_port);
    fflush(stdout);

    char buffer[512];
    while (udp_running) {
        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, NULL, NULL);
        if (n > 0) {
            buffer[n] = '\0';
            printf("\n[UDP STATS] %scontroller> ", buffer);
            fflush(stdout);
        }
    }
    close(sockfd);
    return NULL;
}

int read_line(int socket_fd, char *buffer, size_t max_len) {
    size_t count = 0;
    while (count < max_len - 1) {
        char c;
        ssize_t res = recv(socket_fd, &c, 1, 0);
        if (res <= 0) return res;
        if (c == '\n') break;
        if (c != '\r') {
            buffer[count++] = c;
        }
    }
    buffer[count] = '\0';
    return count;
}

int connect_and_auto_auth(const char *ip, int port) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation error");
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        perror("Invalid address / Address not supported");
        close(sock_fd);
        return -1;
    }

    if (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection to Agent failed");
        close(sock_fd);
        return -1;
    }

    // 1. Send AUTH command immediately
    char auth_cmd[256];
    snprintf(auth_cmd, sizeof(auth_cmd), "AUTH %s\n", AUTH_TOKEN);
    if (send(sock_fd, auth_cmd, strlen(auth_cmd), 0) < 0) {
        perror("Failed to send auth token");
        close(sock_fd);
        return -1;
    }

    // 2. Read lines until we get the authentication result
    char response[BUFFER_SIZE];
    while (read_line(sock_fd, response, sizeof(response)) > 0) {
        if (strstr(response, "OK AUTHENTICATED") != NULL || strstr(response, "OK") != NULL) {
            printf("[+] Auto-Authentication Successful: %s\n", response);
            return sock_fd;
        }
        if (strstr(response, "ERR") != NULL) {
            printf("[-] Auto-Authentication Failed: %s\n", response);
            break;
        }
    }

    close(sock_fd);
    return -1;
}

int main(int argc, char *argv[]) {
    const char *target_ip = (argc > 1) ? argv[1] : AGENT_IP;

    int agent_fd = connect_and_auto_auth(target_ip, AGENT_PORT);
    if (agent_fd < 0) {
        fprintf(stderr, "Failed to connect and authenticate with Agent.\n");
        return EXIT_FAILURE;
    }

    char recv_buf[BUFFER_SIZE];
    char user_input[BUFFER_SIZE];
    pthread_t udp_thread;
    int udp_port = 8888;

    printf("\n=== Controller Active (Auto-Authenticated) ===\n");
    printf("Type commands to send (e.g. SYSINFO, LISTPROC, PUT <file>, GET <file>, MONITOR START 8888, QUIT):\n\n");

    while (1) {
        printf("controller> ");
        fflush(stdout);

        if (!fgets(user_input, sizeof(user_input), stdin)) break;

        user_input[strcspn(user_input, "\r\n")] = 0;
        if (strlen(user_input) == 0) continue;

        if (strncmp(user_input, "PUT ", 4) == 0) {
            char filename[256];
            if (sscanf(user_input + 4, "%255s", filename) != 1) {
                printf("Usage: PUT <filename>\n");
                continue;
            }

            FILE *fp = fopen(filename, "rb");
            if (!fp) {
                printf("Error: Local file '%s' not found.\n", filename);
                continue;
            }

            fseek(fp, 0, SEEK_END);
            long filesize = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            dprintf(agent_fd, "PUT %s %ld\n", filename, filesize);
            read_line(agent_fd, recv_buf, sizeof(recv_buf));
            printf("Agent Response: %s\n", recv_buf);

            if (strstr(recv_buf, "OK") != NULL) {
                char fbuf[4096];
                size_t bytes_read;
                while ((bytes_read = fread(fbuf, 1, sizeof(fbuf), fp)) > 0) {
                    send(agent_fd, fbuf, bytes_read, 0);
                }
                read_line(agent_fd, recv_buf, sizeof(recv_buf));
                printf("Agent Response: %s\n", recv_buf);
            }
            fclose(fp);
            continue;
        }

        dprintf(agent_fd, "%s\n", user_input);

        if (read_line(agent_fd, recv_buf, sizeof(recv_buf)) <= 0) {
            printf("Agent disconnected.\n");
            break;
        }
        printf("Agent Response: %s\n", recv_buf);

        if (strncmp(user_input, "MONITOR START", 13) == 0 && strncmp(recv_buf, "OK", 2) == 0) {
            sscanf(user_input, "MONITOR START %d", &udp_port);
            udp_running = 1;
            pthread_create(&udp_thread, NULL, udp_listener, &udp_port);
        } 
        else if (strncmp(user_input, "MONITOR STOP", 12) == 0) {
            udp_running = 0;
        } 
        else if (strncmp(user_input, "GET ", 4) == 0 && strncmp(recv_buf, "OK FILE_SEND", 12) == 0) {
            char filename[256];
            long filesize = 0;
            sscanf(recv_buf, "OK FILE_SEND %255s %ld", filename, &filesize);

            char save_path[512];
            snprintf(save_path, sizeof(save_path), "downloaded_%s", filename);
            FILE *fp = fopen(save_path, "wb");

            long remaining = filesize;
            char fbuf[4096];
            while (remaining > 0) {
                size_t to_read = (remaining < sizeof(fbuf)) ? remaining : sizeof(fbuf);
                ssize_t read_bytes = recv(agent_fd, fbuf, to_read, 0);
                if (read_bytes <= 0) break;
                fwrite(fbuf, 1, read_bytes, fp);
                remaining -= read_bytes;
            }
            if (fp) fclose(fp);
            printf("[+] Download completed: Saved as %s (%ld bytes)\n", save_path, filesize);
        } 
        else if (strcmp(user_input, "QUIT") == 0) {
            break;
        }
    }

    if (udp_running) {
        udp_running = 0;
    }

    close(agent_fd);
    printf("Connection closed.\n");
    return EXIT_SUCCESS;
}
