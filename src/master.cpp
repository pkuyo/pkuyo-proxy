//
// Created by pkuyo on 25-2-27.
//

#include "master.h"

#include <signal.h>
#include <bits/sigaction.h>

Master::Master(ProcContext&& _ctx): Process(std::move(_ctx)) {
    instance = this;
}

void handle_sigchld(int sig) {

}

bool Master::master_loop() {
    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, nullptr) == -1) {
        log_error("Failed to set up SIGCHLD handler");
        exit(1);
    }
    while (true) {
    }
}
