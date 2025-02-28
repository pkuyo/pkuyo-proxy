//
// Created by pkuyo on 25-2-28.
//

#ifndef SOCK_HANDLER_H
#define SOCK_HANDLER_H
#include <sys/epoll.h>

class HttpListenHandler;

struct IConnHandler {

protected:


public:
    int conn_fd = -1;

    virtual ~IConnHandler() = default;


    virtual ssize_t read(void *buffer, unsigned long size) = 0;

    virtual ssize_t write(void *buffer, unsigned long size) = 0;

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

    HttpConnHandler(HttpListenHandler *_listener, int _conn_id)
        : ConnHandler(_listener, _conn_id) {
    }

    ~HttpConnHandler() override;


    ssize_t read(void *buffer, unsigned long size) override;

    ssize_t write(void *buffer, unsigned long size) override;

    int modify_event(int new_events, epoll_event* ev) override;

};

#endif //SOCK_HANDLER_H
