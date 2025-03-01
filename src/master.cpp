//
// Created by pkuyo on 25-2-27.
//

#include "master.h"

#include <csignal>
#include <fcntl.h>
#include <bits/sigaction.h>

extern bool needExit;


Master::Master(ProcContext&& _ctx): Process(std::move(_ctx)) {
}

bool Master::is_server_healthy(const sockaddr_in& addr) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) return false;


    //fcntl(sock, F_SETFL, O_NONBLOCK);
    if (connect(sock, ( sockaddr*)&addr, sizeof(addr)) == -1) {
        close(sock);
        return false;
    }

    // fd_set fdset;
    // FD_ZERO(&fdset);
    // FD_SET(sock, &fdset);
    //
    // timeval tv{.tv_sec = 1, .tv_usec = 0};
    //
    // if (select(sock + 1, nullptr, &fdset, nullptr, &tv) <= 0) {
    //     close(sock);
    //     return false;
    // }
    //
    // int error = 0;
    // socklen_t len = sizeof(error);
    // getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);
    close(sock);
    //return error == 0;
    return true;
}

void Master::check_health() {
    auto& backends = ctx.config.load_balancer.backends;
    for (size_t i = 0; i < backends.size(); ++i) {
        bool healthy = is_server_healthy(backends[i].server_addr);
        if (ctx.load_balancer->stats[i].is_healthy != healthy) {
            log_info("backend index:%d, state:%s",i, healthy ? "Alive" : "Failed");
        }
        ctx.load_balancer->stats[i].is_healthy.store(healthy);
    }
}

bool Master::master_loop() {
    while (!needExit) {
        if (ctx.config.check_health_time != -1) {
            sleep(ctx.config.check_health_time);
            check_health();
        }
        pause();
    }
    log_info("Exiting master");
    return false;
}
