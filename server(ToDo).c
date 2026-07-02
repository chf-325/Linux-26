/**
 * 简易群聊室服务器端（基于 epoll）
 * 
 * 功能：
 * - 监听指定端口（默认 8888）
 * - 使用 epoll（水平触发）管理多个客户端连接
 * - 接收客户端消息并广播给所有其他在线客户端
 * - 检测客户端上下线，并广播通知
 * - 正确处理异常断开，无资源泄漏
 * 
 * 编译：gcc -o server server.c -Wall
 * 运行：./server [port]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_EVENTS       64      // epoll_wait 一次最多返回的事件数
#define MAX_CLIENTS      64      // 最大支持客户端数量
#define BUFFER_SIZE      1024    // 消息缓冲区大小
#define DEFAULT_PORT     8888    // 默认端口

// 全局客户端状态数组
static int client_fds[MAX_CLIENTS];           // 客户端 socket 描述符，-1 表示空闲
static char client_names[MAX_CLIENTS][32];    // 客户端昵称
static int epoll_fd;                          // epoll 实例描述符
static int listen_fd;                         // 监听 socket

/**
 * 设置文件描述符为非阻塞模式
 */
void set_nonblocking(int fd) {
	
	
	 //--------------------------------------------------------这里补全代码
    
}

/**
 * 添加文件描述符到 epoll 监听集
 * @param epfd epoll 实例描述符
 * @param fd   要添加的描述符
 * @param events 感兴趣的事件（如 EPOLLIN | EPOLLRDHUP）
 */
void add_to_epoll(int epfd, int fd, uint32_t events) {
   //--------------------------------------------------------这里补全代码
}

/**
 * 从 epoll 中移除文件描述符（可选，关闭 socket 时会自动移除）
 * @param epfd epoll 实例描述符
 * @param fd   要移除的描述符
 */
void remove_from_epoll(int epfd, int fd) {
    //--------------------------------------------------------这里补全代码
}

/**
 * 广播消息给所有在线客户端（除了发送者自己）
 * @param sender_fd     发送消息的客户端 fd，传入 -1 表示广播系统消息（不排除任何人）
 * @param msg           消息内容（已经格式化好，例如 "[Alice] hello" 或 "*** Bob joined ***"）
 * @param msg_len       消息长度
 */
void broadcast_message(int sender_fd, const char *msg, int msg_len) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] != -1 && client_fds[i] != sender_fd) {
            // 发送消息，忽略错误（客户端可能断开，后续 epoll 会处理）
            int ret = send(client_fds[i], msg, msg_len, 0);
            if (ret == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("send");
                // 不立即关闭，让 epoll 事件处理
            }
        }
    }
}

/**
 * 关闭并清理一个客户端
 * @param idx 客户端在全局数组中的索引
 */
void cleanup_client(int idx) {
    int fd = client_fds[idx];
    if (fd == -1) return;

    // 构造下线通知消息
    char quit_msg[128];
    int len = snprintf(quit_msg, sizeof(quit_msg), "*** %s left the chatroom ***\n", 
                       client_names[idx]);
    // 广播下线通知给所有人（包括发送者实际上已断开，但广播函数会跳过 -1 ？不，这里 sender_fd 传 fd 也会被跳过，但下线消息希望所有人知道，包括其他人）
    // 注意：广播函数会排除 sender_fd，但下线消息应该让其他人收到，sender_fd 正是要离开的人，所以排除他是正确的。
    broadcast_message(fd, quit_msg, len);

    // 从 epoll 中移除并关闭 socket
    remove_from_epoll(epoll_fd, fd);
    close(fd);

    // 清空数组条目
    client_fds[idx] = -1;
    memset(client_names[idx], 0, sizeof(client_names[idx]));
}

/**
 * 处理新客户端连接
 */
void handle_new_connection() {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd == -1) {
        perror("accept");
        return;
    }

    // 检查是否超出最大客户端数
    int idx = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] == -1) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        printf("Max clients reached, rejecting connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        close(client_fd);
        return;
    }

    // 设置非阻塞
    set_nonblocking(client_fd);

    // 添加到 epoll 监听
    add_to_epoll(epoll_fd, client_fd, EPOLLIN | EPOLLRDHUP);

    // 保存客户端 fd，昵称暂时为空（等待第一条消息作为昵称）
    client_fds[idx] = client_fd;
    client_names[idx][0] = '\0';   // 空昵称标志

    printf("New client connected: fd=%d, ip=%s, port=%d\n", 
           client_fd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
}

/**
 * 处理来自客户端的数据
 * @param fd 客户端 socket
 */
void handle_client_data(int fd) {
    char buffer[BUFFER_SIZE];
    int ret = recv(fd, buffer, sizeof(buffer) - 1, 0);
    if (ret <= 0) {
        // 客户端断开或出错
        if (ret == 0) {
            printf("Client fd=%d disconnected normally.\n", fd);
        } else {
            perror("recv");
        }
        // 查找并清理客户端
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fds[i] == fd) {
                cleanup_client(i);
                break;
            }
        }
        return;
    }

    buffer[ret] = '\0';   // 添加字符串结束符

    // 查找客户端索引和昵称状态
    int idx = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] == fd) {
            idx = i;
            break;
        }
    }
    if (idx == -1) return;  // 不可能发生

    // 如果昵称为空，则第一条消息作为昵称
    if (client_names[idx][0] == '\0') {
        // 去除末尾换行符
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') buffer[len-1] = '\0';
        if (strlen(buffer) >= sizeof(client_names[idx])) {
            printf("Nickname too long, closing client %d\n", fd);
            cleanup_client(idx);
            return;
        }
        strcpy(client_names[idx], buffer);

        // 广播上线通知
        char join_msg[128];
        int msg_len = snprintf(join_msg, sizeof(join_msg), "*** %s joined the chatroom ***\n", 
                               client_names[idx]);
        broadcast_message(-1, join_msg, msg_len);  // -1 表示不排除任何人（包括新客户端自己）
        printf("%s joined.\n", client_names[idx]);
        return;
    }

    // 正常聊天消息：去除末尾换行符
    if (ret > 0 && buffer[ret-1] == '\n') buffer[ret-1] = '\0';
    
    // 构建广播消息格式 "[昵称] 消息内容\n"
    char broadcast_buf[BUFFER_SIZE + 64];
    int msg_len = snprintf(broadcast_buf, sizeof(broadcast_buf), "[%s] %s\n", 
                           client_names[idx], buffer);
    // 广播（排除自己）
    broadcast_message(fd, broadcast_buf, msg_len);
    printf("%s said: %s\n", client_names[idx], buffer);
}

/**
 * 忽略 SIGPIPE 信号，防止向已关闭的 socket 写入时进程终止
 */
void ignore_sigpipe() {
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number, using default %d\n", DEFAULT_PORT);
            port = DEFAULT_PORT;
        }
    }

    // 忽略 SIGPIPE
    ignore_sigpipe();

    // 1. 创建监听 socket
    //listen_fd //--------------------------------------------------------这里补全代码
    if (listen_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // 设置 socket 选项：地址重用，避免 "Address already in use"
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // 绑定地址和端口
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;   // 监听所有本地接口，支持局域网
    server_addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // 监听
    if (listen(listen_fd, SOMAXCONN) == -1) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server started on port %d, waiting for connections...\n", port);

    // 将监听 socket 设为非阻塞
    set_nonblocking(listen_fd);

    // 2. 创建 epoll 实例
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // 将监听 socket 加入 epoll
    add_to_epoll(epoll_fd, listen_fd, EPOLLIN);

    // 初始化客户端数组（全部置为 -1）
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_fds[i] = -1;
    }

    // 3. epoll 事件循环
    struct epoll_event events[MAX_EVENTS];
    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;  // 信号中断，继续
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == listen_fd) {
                // 新连接到来
                handle_new_connection();
            } else {
                // 客户端 socket 事件
                if (ev & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    // 客户端异常断开或挂断
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (client_fds[j] == fd) {
                            cleanup_client(j);
                            break;
                        }
                    }
                } else if (ev & EPOLLIN) {
                    // 有数据可读
                    handle_client_data(fd);
                }
            }
        }
    }

    // 清理（程序一般不会走到这里）
    close(listen_fd);
    close(epoll_fd);
    return 0;
}
