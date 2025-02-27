//
// Created by pkuyo on 25-2-26.
//

#ifndef SEM_HELPER_H
#define SEM_HELPER_H
#include <sys/sem.h>
#include <fcntl.h>

// 设置文件描述符为非阻塞模式
inline void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        throw std::runtime_error("fcntl F_GETFL failed");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::runtime_error("fcntl F_SETFL failed");
    }
}

// 添加文件描述符到 epoll
inline void add_epoll_fd(int epoll_fd, int fd, uint32_t events) {
    epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        throw std::runtime_error("epoll_ctl add failed");
    }
}

// 从 epoll 中移除文件描述符
inline void remove_epoll_fd(int epoll_fd, int fd) {
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        throw std::runtime_error("epoll_ctl del failed");
    }
}

// 关闭连接
void close_connection(int epoll_fd, int fd) {
    remove_epoll_fd(epoll_fd, fd);
    close(fd);
}

#endif //SEM_HELPER_H
