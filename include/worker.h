//
// Created by pkuyo on 25-2-26.
//

#ifndef WOKER_H
#define WOKER_H
#include <map>
#include <memory>
#include <sys/epoll.h>
#include <arpa/inet.h>

#include "def.h"

#include "process.h"
#include "spdlog/spdlog.h"

#include "proxy_handler.h"
#include "listen_handler.h"
#include "conn_handler.h"

class Worker : public Process {

    epoll_event events[MAX_EVENTS];
    sockaddr_in client_addr{};


    std::map<int, std::unique_ptr<ProxyHandler>> client_to_backend;
    std::map<int, ProxyHandler*> backend_to_client;

    std::unique_ptr<IListenHandler> listen_handler;

public:
    explicit Worker(ProcContext&& _ctx);
    void worker_loop();


private:

    std::unique_ptr<IConnHandler> new_backend_fd();

    void disconnect(int fd);

    bool startup();

    void process_events();

    bool process_read_event(const epoll_event & ev);
    bool process_write_event(const epoll_event & ev);


    bool accept_new_conn();

};
#endif //WOKER_H
