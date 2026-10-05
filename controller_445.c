#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define SERVER_PORT 9410
#define BUFFER_SIZE 4096

int udp_running = 0;

void *udp_listener(void *arg) {
    int udp_port = *(int *)arg;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(udp_port);

    bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    printf("\n[UDP Listener] Started on port %d\n> ", udp_port);
    fflush(stdout);

    char buffer[512];
    while (udp_running) {
        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, NULL, NULL);
        if (n > 0) {
            buffer[n] = '\0';
            printf("\n[UDP STATS] %s> ", buffer);
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
        buffer[count++] = c;
    }
    buffer[count] = '\0';
    return count;
}

int main(int argc, char *argv[]) {
    const char *server_ip = (argc > 1) ? argv[1] : "127.0.0.1";

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, server_ip, &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        return 1;
    }

    printf("Connected to RemoteOps Agent at %s:%d\n", server_ip, SERVER_PORT);
    printf("Type commands directly (e.g. AUTH OPS-2445, SYSINFO, QUIT)\n");

    char send_buf[BUFFER_SIZE];
    char recv_buf[BUFFER_SIZE];
    pthread_t udp_thread;
    int udp_port = 8888;

    while (1) {
        printf("> ");
        fflush(stdout);
        if (!fgets(send_buf, sizeof(send_buf), stdin)) break;

        send_buf[strcspn(send_buf, "\r\n")] = 0;
        if (strlen(send_buf) == 0) continue;

        if (strncmp(send_buf, "PUT ", 4) == 0) {
            char filename[256];
            sscanf(send_buf + 4, "%255s", filename);
            FILE *fp = fopen(filename, "rb");
            if (!fp) {
                printf("Local file not found.\n");
                continue;
            }
            fseek(fp, 0, SEEK_END);
            long filesize = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            dprintf(sock, "PUT %s %ld\n", filename, filesize);
            read_line(sock, recv_buf, sizeof(recv_buf));
            printf("Agent: %s\n", recv_buf);

            if (strncmp(recv_buf, "OK", 2) == 0) {
                char fbuf[4096];
                size_t b;
                while ((b = fread(fbuf, 1, sizeof(fbuf), fp)) > 0) {
                    send(sock, fbuf, b, 0);
                }
                read_line(sock, recv_buf, sizeof(recv_buf));
                printf("Agent: %s\n", recv_buf);
            }
            fclose(fp);
            continue;
        }

        dprintf(sock, "%s\n", send_buf);

        if (read_line(sock, recv_buf, sizeof(recv_buf)) <= 0) break;
        printf("Agent: %s\n", recv_buf);

        if (strncmp(send_buf, "MONITOR START", 13) == 0 && strncmp(recv_buf, "OK", 2) == 0) {
            udp_running = 1;
            pthread_create(&udp_thread, NULL, udp_listener, &udp_port);
        } else if (strncmp(send_buf, "MONITOR STOP", 12) == 0) {
            udp_running = 0;
        } else if (strncmp(send_buf, "GET ", 4) == 0 && strncmp(recv_buf, "OK FILE_SEND", 12) == 0) {
            char filename[256];
            long filesize = 0;
            sscanf(recv_buf, "OK FILE_SEND %255s %ld", filename, &filesize);

            FILE *fp = fopen(filename, "wb");
            long remaining = filesize;
            char fbuf[4096];
            while (remaining > 0) {
                size_t to_read = (remaining < sizeof(fbuf)) ? remaining : sizeof(fbuf);
                ssize_t read_bytes = recv(sock, fbuf, to_read, 0);
                if (read_bytes <= 0) break;
                fwrite(fbuf, 1, read_bytes, fp);
                remaining -= read_bytes;
            }
            fclose(fp);
            printf("Downloaded %s (%ld bytes)\n", filename, filesize);
        } else if (strcmp(send_buf, "QUIT") == 0) {
            break;
        }
    }

    close(sock);
    return 0;
}
