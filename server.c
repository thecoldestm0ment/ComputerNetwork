#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUFFER_SIZE 4096

// 오류 발생 시 메시지를 출력하고 프로그램을 종료
void error(const char *msg) {
    perror(msg);
    exit(1);
}

// 파일 확장자에 따라 HTTP Content-Type을 반환
const char *get_content_type(const char *filename) {
    const char *ext = strrchr(filename, '.');

    if (ext == NULL) { 
        return "application/octet-stream"; // 확장자 없는 경우 일반 binary data로 처리
    }

    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) {
        return "text/html";
    } else if (strcmp(ext, ".gif") == 0) {
        return "image/gif";
    } else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
        return "image/jpeg";
    } else if (strcmp(ext, ".mp3") == 0) {
        return "audio/mpeg";
    } else if (strcmp(ext, ".pdf") == 0) {
        return "application/pdf";
    }

    return "application/octet-stream";
}

// 파일 크기 구하는 함수
long get_file_size(FILE *fp) {
    long size;

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    return size;
}

// 요청한 파일 없으면 404 응답 전송
void send_404(int client_fd) {
    const char *body =
        "<html>"
        "<body>"
        "<h1>404 Not Found</h1>"
        "<p>The requested file was not found.</p>"
        "</body>"
        "</html>";

    char header[BUFFER_SIZE];

    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 404 Not Found\r\n"
                              "Content-Type: text/html\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              strlen(body));

    if (header_len < 0 || header_len >= (int)sizeof(header)) {
        perror("snprintf error");
        return;
    }

    send(client_fd, header, strlen(header), 0);
    send(client_fd, body, strlen(body), 0);
}

// HTTP 응답 헤더를 먼저 보내고 그 뒤에 파일 내용을 전송
void send_file(int client_fd, const char *filename) {
    
    FILE *fp = fopen(filename, "rb");

    if (fp == NULL) {
        printf("File not found: %s\n", filename);
        send_404(client_fd);
        return;
    }

    long file_size = get_file_size(fp);
    const char *content_type = get_content_type(filename);

    char header[BUFFER_SIZE];

    // browser가 파일 종류와 크기를 알 수 있도록 response header 생성
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %ld\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              content_type,
                              file_size);

    if (header_len < 0 || header_len >= (int)sizeof(header)) {
        perror("snprintf error");
        fclose(fp);
        send_404(client_fd);
        return;
    }

    send(client_fd, header, strlen(header), 0);

    char file_buffer[BUFFER_SIZE];
    size_t bytes_read;

    // 파일은 binary mode로 열고, 일정 크기씩 읽어서 전송
    while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), fp)) > 0) {
        send(client_fd, file_buffer, bytes_read, 0);
    }

    fclose(fp);
}

// 하나의 client 요청을 처리
void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];

    memset(buffer, 0, sizeof(buffer));

    // browser가 보낸 HTTP request message 읽기
    int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_received <= 0) {
        return;
    }

    // Part A: browser가 보낸 HTTP request를 console에 출력
    printf("===== HTTP Request =====\n");
    printf("%s\n", buffer);
    printf("========================\n");

    char method[16];
    char path[256];
    char version[16];
    char filename[256];

    memset(method, 0, sizeof(method));
    memset(path, 0, sizeof(path));
    memset(version, 0, sizeof(version));
    memset(filename, 0, sizeof(filename));

    // request line에서 method, path, version을 분리
    if (sscanf(buffer, "%15s %255s %15s", method, path, version) != 3) {
        send_404(client_fd);
        return;
    }

    printf("Method: %s\n", method);
    printf("Path: %s\n", path);
    printf("Version: %s\n", version);

    if (strcmp(method, "GET") != 0) {
        send_404(client_fd);
        return;
    }

    // "/"로 요청하면 기본 파일인 index.html을 반환
    if (strcmp(path, "/") == 0) {
        strcpy(filename, "index.html");
    } else {
        // path의 맨 앞 '/'를 제외하고 실제 파일 이름으로 사용
        snprintf(filename, sizeof(filename), "%s", path + 1);
    }

    printf("Requested file: %s\n", filename);

    send_file(client_fd, filename);
}

int main(int argc, char *argv[]) {
    int server_fd;
    int client_fd;
    int port;

    socklen_t client_len;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(1);
    }

    port = atoi(argv[1]);

    // child process가 종료된 뒤 zombie process가 남지 않게함
    signal(SIGCHLD, SIG_IGN);

    // TCP socket을 생성한다.
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        error("socket error");
    }

    // 서버를 재실행할 때 같은 port를 바로 사용할 수 있도록 설정
    int opt = 1;

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        error("setsockopt error");
    }

    memset(&server_addr, 0, sizeof(server_addr));

    // IPv4 주소와 입력받은 port number를 server address에 설정
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // socket을 지정한 port에 연결
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        error("bind error");
    }

    // client connection을 기다리는 상태로 전환
    if (listen(server_fd, 5) < 0) {
        error("listen error");
    }

    printf("Server started on port %d\n", port);

    // 여러 client 요청을 계속 받기 위해 무한 루프를 사용
    while (1) {
        client_len = sizeof(client_addr);

        // client가 접속하면 client 전용 socket을 반환
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            perror("accept error");
            continue;
        }

        // 각 client 요청은 child process에서 처리
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork error");
            close(client_fd);
            continue;
        } else if (pid == 0) {
            // 자식 프로세스는 서버 소켓 닫기 
            close(server_fd);

            handle_client(client_fd);

            close(client_fd);
            exit(0);
        } else {
            // 부모 프로세스는 client 소켓 닫기
            close(client_fd);
        }
    }

    close(server_fd);

    return 0;
}