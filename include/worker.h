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
#include "spdlog/spdlog.h"

struct ProxyContext;

class Worker {
    ProcContext ctx;
    int epoll_fd = -1;
    int listen_fd = -1;

    bool handle_new_conn = false;

    epoll_event events[10];
    sockaddr_in client_addr{};

    uint backend_len = sizeof(sockaddr_in);

    std::map<int, std::unique_ptr<ProxyContext>> client_to_backend;
    std::map<int, ProxyContext*> backend_to_client;

    char format_buffer[512];



public:
    explicit Worker(const ProcContext& _ctx);
    ~Worker();
    void worker_loop();


private:
    void log_error(const char * message, bool has_error = true) const;

    void log_info(const char * message) const;

    template<typename ...Args>
    void log_info(const char * message, Args... args) {
        sprintf(format_buffer,message,std::forward<Args>(args)...);
        spdlog::info("{}, server:{}:{}.",format_buffer,
        ctx.config.server_name,
        ctx.config.port);
    }


    void disconnect(int fd);

    bool startup();

    bool process_events();

    bool process_read_event(epoll_event & ev);
    bool process_write_event(epoll_event & ev);


    bool accept_new_conn();

};
#endif //WOKER_H
