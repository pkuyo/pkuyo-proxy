//
// Created by pkuyo on 25-2-28.
//
#include "conn_handler.h"

#include <helper.h>

#include "worker.h"


HttpConnHandler::~HttpConnHandler() {
    if (conn_fd != -1) {
        epoll_ctl(listener->epoll_fd, EPOLL_CTL_DEL, conn_fd, nullptr);
        close(conn_fd);
    }
}


ssize_t HttpConnHandler::read(void *buffer, unsigned long size) {
    return ::read(conn_fd, buffer, size);
}

ssize_t HttpConnHandler::write(void *buffer, unsigned long size) {
    return ::write(conn_fd, buffer, size);
}

int HttpConnHandler::modify_event(int new_events, epoll_event *ev) {
    ev->events = EPOLLIN | EPOLLET;
    ev->data.fd = conn_fd;
    return epoll_ctl(listener->epoll_fd, EPOLL_CTL_MOD, conn_fd, ev);
}
