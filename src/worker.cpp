//
// Created by pkuyo on 25-2-26.
//
#include "worker.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include "helper.h"
#include "spdlog/spdlog.h"
#include "http_handler.h"

extern bool needExit;


Worker::Worker(ProcContext&& _ctx)
    : Process(std::move(_ctx)), events{} {
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
        client_to_backend.erase(fd);
    } else if (backend_to_client.contains(fd)) {
        int client_fd = backend_to_client[fd]->client->id();
        backend_to_client.erase(fd);
        client_to_backend.erase(client_fd);
    }
   log_debug("Connection closed: %d", fd);
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

int Worker::new_backend_fd() {

    int backend_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (backend_fd == -1) {
        log_error("Failed to create backend socket");
        close(backend_fd);
        backend_fd = -1;
    }

    auto backend = get_next_backend(ctx.config.load_balancer,ctx.load_balancer);

    if (backend.config == nullptr) {
        log_error("Failed to find next backend");
        close(backend_fd);
        backend_fd = -1;
    }

    if (connect(backend_fd, reinterpret_cast<sockaddr *>(&backend.config->server_addr),
        sizeof(backend.config->server_addr)) == -1) {
        log_error( "Failed to connect to backend");
        backend.stat->failed_connections.fetch_add(1);
        close(backend_fd);
        backend_fd = -1;
        }
    backend.stat->connections.fetch_add(1);
    return backend_fd;
}


bool Worker::accept_new_conn() {

    int client_fd = accept(listen_fd, reinterpret_cast<sockaddr *>(&client_addr),&backend_len);
    if (client_fd == -1) {
        log_error("Failed to accept client");
        return true;
    }

    client_to_backend[client_fd] = std::make_unique<ProxyHandler>();
    client_to_backend[client_fd]->client = std::make_unique<NormalConnHandler>(this,client_fd,epoll_fd,true);

    if (ctx.config.backend_type == ListenerConfig::LOAD_BALANCER) {
        int backend_fd = new_backend_fd();
        if (backend_fd != -1) {
            client_to_backend[client_fd]->backend = std::make_unique<NormalConnHandler>(this,backend_fd,epoll_fd,false);
            backend_to_client[backend_fd] = client_to_backend[client_fd].get();
        }
        else {
            client_to_backend[client_fd]->backend = std::make_unique<FailedBackendHandler>(this);
        }

        client_to_backend[client_fd]->init_link();

        log_debug("New connection: client %d -> backend %d",client_fd,backend_fd);
    }
    else if (ctx.config.backend_type == ListenerConfig::STATIC) {
        client_to_backend[client_fd]->backend = std::make_unique<StaticFileHandler>(this,
            ctx.config.static_file.root_path);

        client_to_backend[client_fd]->init_link();
        log_debug("New connection: client %d -> static",client_fd);

    }
    return false;
}

void Worker::process_events() {

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
}


bool Worker::process_read_event(const epoll_event & ev) {
    int fd = ev.data.fd;

    if (client_to_backend.contains(fd)) {

        auto handler = client_to_backend[fd].get();
        return handler->client->handle_read_event();

    } else if (backend_to_client.contains(fd)) {
        auto handler = backend_to_client[fd];
        return handler->backend->handle_read_event();
    }
    return true;
}

bool Worker::process_write_event(const epoll_event & ev) {
    int fd = ev.data.fd;
    if (client_to_backend.contains(fd)) {

        auto handler = client_to_backend[fd].get();
        return handler->client->handle_write_event();

    } else if (backend_to_client.contains(fd)) {
        auto handler = backend_to_client[fd];
        return handler->backend->handle_write_event();
    }
    return true;
}



void Worker::worker_loop() {
    if (startup())
        return;
    while (!needExit || !client_to_backend.empty()) {
        process_events();
    }

    log_info("Exiting worker");
}

