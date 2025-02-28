//
// Created by pkuyo on 25-3-1.
//

#ifndef PROXY_LISTEN_HANDLER_H
#define PROXY_LISTEN_HANDLER_H
#include <memory>
#include <unistd.h>
#include "conn_handler.h"

class Worker;

struct IListenHandler {

    Worker* owner;

    int listen_fd = -1;
    int epoll_fd = -1;

protected:

    explicit IListenHandler(Worker* _owner) : owner(_owner) {}



public:

    virtual ~IListenHandler()  {
        if (listen_fd >= 0) {
            close(listen_fd);
            listen_fd = -1;
        }
        if (epoll_fd >= 0) {
            close(epoll_fd);
            epoll_fd = -1;
        }
    }

    virtual bool startup() = 0;

    virtual int wait(struct epoll_event* events, int max_event, int time_out) = 0;

    virtual std::unique_ptr<IConnHandler> accept(struct sockaddr* addr) = 0;

    virtual std::unique_ptr<IConnHandler> connect(struct sockaddr* addr, int len) = 0;

};

struct HttpListenHandler final : public IListenHandler {



    explicit HttpListenHandler(Worker* _owner)
        : IListenHandler(_owner) {
    }


    bool startup() override;

    int wait(epoll_event* events, int max_event, int time_out) override;

    std::unique_ptr<IConnHandler> accept(struct sockaddr* addr) override;

    std::unique_ptr<IConnHandler> connect(struct sockaddr* addr, int len) override;
};

#endif //PROXY_LISTEN_HANDLER_H
