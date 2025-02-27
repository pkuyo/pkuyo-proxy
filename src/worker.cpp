//
// Created by pkuyo on 25-2-26.
//
#include "worker.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include "helper.h"
#include "spdlog/spdlog.h"
#include "http_handler.h"


Worker::Worker(const ProcContext& _ctx)
: ctx(_ctx) {
}
Worker::~Worker() {
    ctx.Clear();
}

struct BackServer {
    BackendServerConfig* config = nullptr;
    BackendStat* stat = nullptr;
};

BackServer get_next_backend(LoadBalancerConfig & config, ShmLoadBalancer * lb) {
    if (config.algorithm == LoadBalancingAlgorithm::RoundRobin) {
        auto backend = BackServer{
            &config.backends[lb->current_index],
            &lb->stats[lb->current_index]
        };
        lb->current_index = (lb->current_index + 1) % config.backend_count;
        return backend;
    } else if (config.algorithm == LoadBalancingAlgorithm::WeightedRoundRobin) {
        //TODO:实现
    } else if (config.algorithm == LoadBalancingAlgorithm::LeastConnections) {
        // 最小连接数策略
        auto backend = &config.backends[0];
        auto state = &lb->stats[0];
        for (int i = 1; i < config.backend_count; i++) {
            if (lb->stats[i].connections < state->connections) {
                state = &lb->stats[i];
                backend = &config.backends[i];
            }
        }
        return BackServer{backend,state};
    }
    return BackServer{nullptr,nullptr};
}

void Worker::disconnect(int fd) {

    if (client_to_backend.contains(fd)) {
        auto backend_fd = client_to_backend[fd]->backend_fd;
        close_connection(epoll_fd, fd);
        close_connection(epoll_fd, backend_fd);
        client_to_backend.erase(fd);
        backend_to_client.erase(backend_fd);
    } else if (backend_to_client.contains(fd)) {
        int client_fd = backend_to_client[fd]->client_fd;
        close_connection(epoll_fd, fd);
        close_connection(epoll_fd, client_fd);
        backend_to_client.erase(fd);
        client_to_backend.erase(client_fd);
    }
   log_info("Connection closed: %d", fd);
}


void Worker::log_error(const char * message, bool has_error) const {
    if (has_error) {
        spdlog::error("{}, error:{}, server:{}:{}.",
            message,
            strerror(errno),
        ctx.config.server_name,
        ctx.config.port);
    }
    else {
        spdlog::error("{}, server:{}:{}.",
            message,
        ctx.config.server_name,
        ctx.config.port);
    }
}
void Worker::log_info(const char * message) const {
    spdlog::info("{}, server:{}:{}.",
        message,
    ctx.config.server_name,
    ctx.config.port);
}



bool Worker::startup() {
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        log_error("Failed to create socket");
        return true;
    }
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        log_error("setsockopt(SO_REUSEADDR | SO_REUSEPORT) failed");
        close(listen_fd);
        return true;
    }

    sockaddr_in server_addr{};
    // 绑定地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(ctx.config.port);
    if (bind(listen_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) == -1) {
        log_error("Failed to bind");
        close(listen_fd);
        return true;
    }

    // 监听
    if (listen(listen_fd, SOMAXCONN) == -1) {
        log_error("Failed to listen");
        close(listen_fd);
        return true;
    }

    // 创建 epoll 实例
    if ((epoll_fd = epoll_create1(0)) == -1) {
        log_error("Failed to create epoll");
        close(listen_fd);
        return true;
    }

    // 添加监听套接字到 epoll
    set_nonblocking(listen_fd);
    add_epoll_fd(epoll_fd, listen_fd, EPOLLIN | EPOLLET);
    log_info("Listen on port:%d",ctx.config.port);
    return false;
}


bool  Worker::accept_new_conn() {
    // if (!handle_new_conn)
    //         return false;

    int client_fd = accept(listen_fd, reinterpret_cast<sockaddr *>(&client_addr),&backend_len);
    if (client_fd == -1) {
        log_error("Failed to accept client");
        return true;
    }
    int backend_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (backend_fd == -1) {
        log_error("Failed to create backend socket");
        close(client_fd);
        return true;
    }

    auto backend = get_next_backend(ctx.config.load_balancer,ctx.load_balancer);

    if (backend.config == nullptr) {
        log_error("Failed to find next backend");
        return true;
    }

    if (connect(backend_fd, reinterpret_cast<sockaddr *>(&backend.config->server_addr),
        sizeof(backend.config->server_addr)) == -1) {
        log_error( "Failed to connect to backend");
        backend.stat->failed_connections.fetch_add(1);
        close(client_fd);
        close(backend_fd);
        return true;
    }

    set_nonblocking(client_fd);
    set_nonblocking(backend_fd);
    add_epoll_fd(epoll_fd, client_fd, EPOLLIN | EPOLLET);
    add_epoll_fd(epoll_fd, backend_fd, EPOLLIN | EPOLLET);

    auto ctx = std::make_unique<ProxyContext>(client_fd,backend_fd);


    backend_to_client[backend_fd] = ctx.get();
    client_to_backend[client_fd] = std::move(ctx);

    log_info("New connection: client %d -> backend %d",client_fd,backend_fd);
    backend.stat->connections.fetch_add(1);
    return false;
}

bool Worker::process_events() {

    auto ev_count = epoll_wait(epoll_fd, events, 10, -1);
    for (int i = 0;i < ev_count; i++) {
        if (events[i].data.fd == listen_fd) {
            accept_new_conn();
        }
        else if (events[i].events & EPOLLIN) {
            if (process_read_event(events[i]))
                disconnect(events[i].data.fd);
        }
        else if (events[i].events & EPOLLOUT) {
            if (process_write_event(events[i]))
                disconnect(events[i].data.fd);
        }
    }
    return false;
}

bool Worker::process_read_event(epoll_event & ev) {
    int fd = ev.data.fd;

    if (client_to_backend.contains(fd)) {

        auto ctx = client_to_backend[fd].get();
        return handle_http_buffer(ctx->client_ctx,ev,epoll_fd);
    } else if (backend_to_client.contains(fd)) {

        auto ctx = backend_to_client[fd];
        return handle_http_buffer(ctx->backend_ctx,ev,epoll_fd);
    }
    return true;
}

bool Worker::process_write_event(epoll_event & ev) {
    int fd = ev.data.fd;

    if (client_to_backend.contains(fd)) {

        auto ctx = client_to_backend[fd].get();
        return send_http_buffer(ctx->client_ctx,ev,epoll_fd);
    } else if (backend_to_client.contains(fd)) {

        auto ctx = backend_to_client[fd];
        return send_http_buffer(ctx->backend_ctx,ev,epoll_fd);
    }
    return true;
}


// bool Worker::try_accept_lock() {
//     const int result = pthread_mutex_trylock(&ctx.load_balancer->accept_mutex);
//     if (result == 0) {
//         handle_new_conn = true;
//         return false;
//     }
//     else if (result == EBUSY) {
//         return false;
//     }
//     log_error("Access accept_mutex failed");
//     return true;
// }
//
// bool Worker::release_lock() {
//     if (handle_new_conn) {
//         handle_new_conn = false;
//         const int result = pthread_mutex_unlock(&ctx.load_balancer->accept_mutex);
//         if (result != 0) {
//             return true;
//         }
//     }
//     return false;
// }

void Worker::worker_loop() {
    if (startup())
        return;
    while (true) {
        if (process_events())
            break;
    }

}
