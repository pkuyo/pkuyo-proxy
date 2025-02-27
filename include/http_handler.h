//
// Created by pkuyo on 25-2-26.
//

#ifndef HTTP_HANDER_H
#define HTTP_HANDER_H

#include <queue>
#include <string>

struct epoll_event;
const int MAX_EVENTS = 1024;
const int BUFFER_SIZE = 4096;

struct HttpContext {
    union {
        size_t content_remaining;
        size_t chunk_size;
    } data;

    bool is_completed;
    std::string buffer;
};



struct ConnectionContext {
    int fd;
    int to_fd;

    enum {
        READ_HEADERS,
        READ_BODY,
        READ_CHUNK_SIZE,
        READ_CHUNK_DATA,
        } state = READ_HEADERS;
    bool is_sending;
    std::queue<HttpContext> queue;
    std::string raw_buffer;
};


struct ProxyContext {

    ProxyContext(int client_fd,int backend_fd) :
    client_ctx{.fd = client_fd, .to_fd = backend_fd},
    backend_ctx{.fd = backend_fd, .to_fd = client_fd},
    client_fd(client_fd),
    backend_fd(backend_fd)
    {}

    ConnectionContext client_ctx;
    ConnectionContext backend_ctx;

    int client_fd;
    int backend_fd;
};

bool handle_http_buffer(ConnectionContext & ctx, epoll_event & ev, int epoll_id);
bool send_http_buffer(ConnectionContext & ctx, epoll_event & ev, int epoll_id);
#endif //HTTP_HANDER_H
