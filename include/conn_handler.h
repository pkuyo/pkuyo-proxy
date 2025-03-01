//
// Created by pkuyo on 25-2-28.
//

#ifndef SOCK_HANDLER_H
#define SOCK_HANDLER_H
#include <netinet/in.h>
#include <sys/epoll.h>

struct HttpsListenHandler;
struct HttpListenHandler;

struct EventState {
    bool is_error = false;
    ssize_t byte_count = 0;
};

struct IConnHandler {

protected:


public:
    int conn_fd = -1;

    char ip[INET_ADDRSTRLEN]{};
    int port{};

    bool handshake_done = false;

    virtual ~IConnHandler() = default;

    void setup_info(struct sockaddr* addr);

    virtual bool handshake() = 0;


    virtual EventState read(void *buffer, unsigned long size) = 0;

    virtual EventState write(void *buffer, unsigned long size) = 0;

    virtual int modify_event(int new_events, epoll_event* ev) = 0;


protected:
    explicit IConnHandler(int _conn_fd) : conn_fd(_conn_fd)  {};


};

template<typename Listener>
struct ConnHandler : public IConnHandler{

protected:
    Listener* listener;
    explicit ConnHandler(Listener* _listener, int _conn_id) : IConnHandler(_conn_id),listener(_listener)  {}
};

struct HttpConnHandler : public ConnHandler<HttpListenHandler> {

    HttpConnHandler(HttpListenHandler *_listener, int _conn_id,struct sockaddr* addr)
        : ConnHandler(_listener, _conn_id) {
        setup_info(addr);
    }

    ~HttpConnHandler() override;


    bool handshake() override {
        handshake_done = true;
        return false;
    }

    EventState read(void *buffer, unsigned long size) override;

    EventState write(void *buffer, unsigned long size) override;

    int modify_event(int new_events, epoll_event* ev) override;

};

struct HttpsConnHandler : public ConnHandler<HttpsListenHandler> {

    struct ssl_st* ssl;
    HttpsConnHandler(HttpsListenHandler *_listener,ssl_st* _ssl, int _conn_id,sockaddr* addr)
     : ConnHandler(_listener, _conn_id), ssl(_ssl) {
        setup_info(addr);
    }
    ~HttpsConnHandler() override;

    bool handshake() override;

    EventState read(void *buffer, unsigned long size) override;

    EventState write(void *buffer, unsigned long size) override;

    int modify_event(int new_events, epoll_event* ev) override;

};

#endif //SOCK_HANDLER_H
