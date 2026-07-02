/**
 * 简易群聊室客户端
 * 
 * 功能：
 * - 连接服务器，输入昵称
 * - 实时接收并显示服务器广播消息（包括聊天内容和系统通知）
 * - 用户可输入消息发送给服务器
 * - 输入 "quit" 或按 Ctrl+C 正常退出
 * 
 * 编译：gcc -o client client.c -Wall -pthread
 * 运行：./client <server_ip> [port]
 * 示例：./client 192.168.1.100 8888
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BUFFER_SIZE 1024

static int sock_fd = -1;          // 客户端 socket
static volatile int running = 1;   // 控制主循环是否继续
static pthread_t recv_thread;      // 接收线程 ID（全局以便信号处理）

/**
 * 接收线程函数：不断从服务器接收消息并打印到屏幕
 */
void *recv_thread_func(void *arg) {
    char buffer[BUFFER_SIZE];
    int ret;
    while (running) {
        ret = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
        if (ret <= 0) {
            if (ret == 0) {
                printf("\nServer closed the connection.\n");
            } else {
                // 如果是因为 socket 被主线程关闭，errno 可能为 EBADF 或其它，不打印多余错误
                if (errno != EBADF)
                    perror("recv");
            }
            break;
        }
        buffer[ret] = '\0';
        // 输出服务器消息（加上换行保护，避免和用户输入混叠）
        printf("%s", buffer);
        fflush(stdout);
    }
    return NULL;
}

/**
 * 发送消息给服务器（自动添加换行符）
 * @param msg 消息内容（不含换行）
 * @return 0 成功，-1 失败
 */
int send_message(const char *msg) {
    char buf[BUFFER_SIZE];
    int len = snprintf(buf, sizeof(buf), "%s\n", msg);
    int ret = send(sock_fd, buf, len, 0);
    if (ret == -1) {
        perror("send");
        return -1;
    }
    return 0;
}

/**
 * 正常退出，关闭 socket 并等待线程结束
 */
void clean_exit() {
    if (sock_fd != -1) {
        // 主动关闭 socket，让阻塞的 recv 返回
        close(sock_fd);
        sock_fd = -1;
    }
    // 等待接收线程退出（如果已创建且未 join）
    pthread_join(recv_thread, NULL);
    exit(0);
}

/**
 * 信号处理函数（Ctrl+C 等）
 */
void sig_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\nExiting...\n");
        running = 0;
        clean_exit();
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server_ip> [port]\n", argv[0]);
        fprintf(stderr, "Example: %s 192.168.1.100 8888\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *server_ip = argv[1];
    int port = 8888;
    if (argc >= 3) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port, using default 8888\n");
            port = 8888;
        }
    }

    // 注册信号处理
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // 1. 创建 socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // 2. 连接服务器
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to chat server %s:%d\n", server_ip, port);

    // 3. 输入昵称并发送给服务器
    char nickname[32];
    printf("Please enter your nickname: ");
    fflush(stdout);
    if (fgets(nickname, sizeof(nickname), stdin) == NULL) {
        fprintf(stderr, "Failed to read nickname\n");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    // 去掉末尾换行符
    size_t len = strlen(nickname);
    if (len > 0 && nickname[len-1] == '\n') nickname[len-1] = '\0';
    if (strlen(nickname) == 0) {
        fprintf(stderr, "Nickname cannot be empty\n");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    if (send_message(nickname) == -1) {
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    // 4. 创建接收线程
    if (pthread_create(&recv_thread, NULL, recv_thread_func, NULL) != 0) {
        perror("pthread_create");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    // 5. 主线程：读取用户输入并发送
    char input[BUFFER_SIZE];
    printf("\n--- You are now in the chatroom. Type 'quit' to exit. ---\n");
    while (running) {
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        // 去掉末尾换行符
        len = strlen(input);
        if (len > 0 && input[len-1] == '\n') input[len-1] = '\0';

        // 检查退出命令
        if (strcmp(input, "quit") == 0) {
            printf("Leaving chatroom...\n");
            running = 0;
            break;  // 退出主循环，随后调用 clean_exit
        }

        // 发送消息（空消息不发送）
        if (strlen(input) > 0) {
            if (send_message(input) == -1) {
                break;
            }
        }
    }

    // 6. 清理
    clean_exit();
    return 0;
}