//
// Created by pkuyo on 25-2-28.
//
#include "conn_handler.h"

#include <helper.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "worker.h"


void IConnHandler::setup_info(sockaddr *addr) {
    inet_ntop(addr->sa_family, addr, ip, INET_ADDRSTRLEN);
    port = ntohs(reinterpret_cast<const sockaddr_in *>(addr)->sin_port);
}

HttpConnHandler::~HttpConnHandler() {
    if (conn_fd != -1) {
        epoll_ctl(listener->epoll_fd, EPOLL_CTL_DEL, conn_fd, nullptr);
        close(conn_fd);
    }
}


EventState HttpConnHandler::read(void *buffer, unsigned long size) {
    EventState re {.byte_count = ::read(conn_fd, buffer, size)};
    if (re.byte_count == -1 &&
        errno != EAGAIN && errno != EWOULDBLOCK) {
        re.is_error = true;
    }
    return re;
}

EventState HttpConnHandler::write(void *buffer, unsigned long size) {
    EventState re {.byte_count = ::write(conn_fd, buffer, size)};
    if (re.byte_count == -1 &&
        errno != EAGAIN && errno != EWOULDBLOCK) {
        re.is_error = true;
    }
    return re;
}

int HttpConnHandler::modify_event(int new_events, epoll_event *ev) {
    ev->events = new_events;
    ev->data.fd = conn_fd;
    return epoll_ctl(listener->epoll_fd, EPOLL_CTL_MOD, conn_fd, ev);
}

HttpsConnHandler::~HttpsConnHandler() {

    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (conn_fd != -1) {
        epoll_ctl(listener->epoll_fd, EPOLL_CTL_DEL, conn_fd, nullptr);
        close(conn_fd);
    }


}

bool HttpsConnHandler::handshake() {
    int ret = SSL_accept(ssl);
    epoll_event ev{};
    if (ret <= 0) {
        int ssl_err = SSL_get_error(ssl, ret);
        if (ssl_err == SSL_ERROR_WANT_READ) {
            modify_event(EPOLLIN | EPOLLET,&ev);
            return false;
        } else if (ssl_err == SSL_ERROR_WANT_WRITE) {
            modify_event(EPOLLOUT | EPOLLET,&ev);
            return false;
        }
        listener->owner->log_error("SSL handshake fatal error: %s (errno=%d)",
            ERR_error_string(ERR_get_error(), nullptr), errno);

        return true;
    }
    modify_event(EPOLLIN | EPOLLET,&ev);
    handshake_done = true;
    return false;
}

EventState HttpsConnHandler::read(void *buffer, unsigned long size) {
    EventState re {.byte_count = SSL_read(ssl, buffer, size)};
    if (re.byte_count == -1) {
        int err = SSL_get_error(ssl, re.byte_count);
        if (err != SSL_ERROR_WANT_WRITE && err != SSL_ERROR_WANT_READ) {

            if (err != SSL_ERROR_SYSCALL || (errno != EAGAIN && errno != EWOULDBLOCK))
                re.is_error = true;
        }
    }
    return re;
}

EventState HttpsConnHandler::write(void *buffer, unsigned long size) {
    EventState re {.byte_count = SSL_write(ssl, buffer, size)};
    if (re.byte_count == -1) {
        int err = SSL_get_error(ssl, re.byte_count);
        if (err != SSL_ERROR_WANT_WRITE && err != SSL_ERROR_WANT_READ) {

            if (err != SSL_ERROR_SYSCALL || (errno != EAGAIN && errno != EWOULDBLOCK))
                re.is_error = true;
        }
    }
    return re;
}

int HttpsConnHandler::modify_event(int new_events, epoll_event *ev) {
    ev->events = new_events;
    ev->data.fd = conn_fd;
    return epoll_ctl(listener->epoll_fd, EPOLL_CTL_MOD, conn_fd, ev);
}
