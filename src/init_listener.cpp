//
// Created by pkuyo on 25-2-26.
//
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <cstring>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "init_listener.h"

#include <csignal>
#include <sys/wait.h>

#include "worker.h"
#include "master.h"

extern bool needExit;

//创建新的共享内存区域
void init_mmap(ProcContext & ctx) {
    constexpr size_t lb_size = sizeof(ShmLoadBalancer);
    constexpr size_t backend_align = alignof(BackendStat);

    //计算对齐
    constexpr size_t padding = (backend_align - (lb_size % backend_align)) % backend_align;
    auto size = lb_size + padding + sizeof(BackendStat) * ctx.config.process_count;

    int shm_fd = shm_open(ctx.config.server_name, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, size);

    auto *raw_mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    auto load_balancer_mem = new(raw_mem) ShmLoadBalancer();


    constexpr size_t offset = sizeof(ShmLoadBalancer) + padding;
    auto *stats_mem = reinterpret_cast<BackendStat*>(static_cast<char*>(raw_mem) + offset);
    for (size_t i = 0; i < ctx.config.process_count; ++i) {
        new(&stats_mem[i]) BackendStat();
    }
    load_balancer_mem->stats = stats_mem;

    ctx.load_balancer = load_balancer_mem;
    ctx.mem_length = size;
}


void waitpid_signal_handler(int) {
    while (waitpid(-1, nullptr, WNOHANG) > 0);
}

void start_listener(ListenerConfig & config) {

    ProcContext ctx(config);

    init_mmap(ctx);

    for (int current_count = 0;current_count < config.process_count;current_count++) {
        pid_t pid = fork_with_cleanup();
        if (pid == 0) {

            Worker worker(std::move(ctx));
            worker.worker_loop();
            return;
        }
    }



    auto master = std::make_unique<Master>(std::move(ctx));
    signal(SIGCHLD, waitpid_signal_handler);

    if (master->master_loop()) {

        auto ctx = std::move(master->ctx);
        master.reset();

        Worker worker(std::move(ctx));
        worker.worker_loop();
    }
}

void reload_signal_handler(int) {
    needExit = true;
}

void fork_listeners(ProxyConfig & config) {
    for (int i = 0; i < config.listener_count; i++) {
        pid_t pid = fork_with_cleanup();
        if (pid == 0) {

            signal(SIGHUP, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            signal(SIGCHLD, SIG_DFL);
            // 子进程运行监听逻辑
            if (signal(SIGHUP, reload_signal_handler) == SIG_ERR)
                spdlog::error("failed to reload signal handler");
            start_listener(config.listeners[i]);
            exit(0);
        } else if (pid < 0) {
            perror("fork failed");
            exit(1);
        }
    }
}