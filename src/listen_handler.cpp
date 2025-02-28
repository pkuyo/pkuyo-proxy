//
// Created by pkuyo on 25-3-1.
//
#include "listen_handler.h"

#include <helper.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "worker.h"

bool HttpListenHandler::startup() {

    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        owner->log_error("Failed to create socket");
        return true;
    }
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        owner->log_error("setsockopt(SO_REUSEADDR | SO_REUSEPORT) failed");
        return true;
    }

    sockaddr_in server_addr{};
    // 绑定地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(owner->ctx.config.port);
    if (bind(listen_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) == -1) {
        owner->log_error("Failed to bind");
        return true;
    }

    // 监听
    if (listen(listen_fd, SOMAXCONN) == -1) {
         owner->log_error("Failed to listen");
        return true;
    }

    // 创建 epoll 实例
    if ((epoll_fd = epoll_create1(0)) == -1) {
        owner->log_error("Failed to create epoll");
        return true;
    }

    // 添加监听套接字到 epoll
    set_nonblocking(listen_fd);
    add_epoll_fd(epoll_fd, listen_fd, EPOLLIN);
    owner->log_info("Listen on port:%d", owner->ctx.config.port);
    return false;
}

int HttpListenHandler::wait(epoll_event *events, int max_event, int time_out) {
    return epoll_wait(epoll_fd, events, max_event, time_out);
}

std::unique_ptr<IConnHandler> HttpListenHandler::accept(struct sockaddr *addr) {
    socklen_t len = 0;
    int fd = ::accept(listen_fd, reinterpret_cast<sockaddr *>(&addr), &len);
    if (fd == -1)
        return nullptr;
    set_nonblocking(fd);
    add_epoll_fd(epoll_fd, fd, EPOLLIN | EPOLLET);

    return std::make_unique<HttpConnHandler>(this, fd);
}

std::unique_ptr<IConnHandler> HttpListenHandler::connect(struct sockaddr *addr, int len) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        owner->log_error("Failed to create backend socket, %s",strerror(errno));
        close(fd);
        fd = -1;
    }


    if (::connect(fd, addr,len) == -1) {
        owner->log_error( "Failed to connect to backend, %s",strerror(errno));
        close(fd);
        fd = -1;
    }
    if (fd != -1) {
        set_nonblocking(fd);
        add_epoll_fd(epoll_fd, fd, EPOLLIN | EPOLLET);
    }
    if (fd == -1)
        return nullptr;
    return std::make_unique<HttpConnHandler>(this, fd);
}
