//
// Created by pkuyo on 25-2-26.
//
#include "worker.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include "helper.h"
#include "spdlog/spdlog.h"
#include "proxy_handler.h"

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
        return backend.stat->is_healthy ? backend : BackServer{nullptr,nullptr};

    } else if (config.algorithm == LoadBalancingAlgorithm::WeightedRoundRobin) {

        auto backend = &config.backends[0];
        auto state = &lb->stats[0];
        state->current_weight += backend->weight;
        for (int i = 1; i < config.backend_count; i++) {
            if (!lb->stats[i].is_healthy)
                continue;
            lb->stats[i].current_weight += config.backends[i].weight;
            if (lb->stats[i].current_weight < state->current_weight) {
                state = &lb->stats[i];
                backend = &config.backends[i];
            }
        }
        if (state->is_healthy) {
            state->current_weight -= config.total_weight;
            return BackServer{backend,state};
        }
        else {
            state->current_weight -= backend->weight;
            return BackServer{nullptr,nullptr};
        }

    } else if (config.algorithm == LoadBalancingAlgorithm::LeastConnections) {
        // 最小连接数策略
        auto backend = &config.backends[0];
        auto state = &lb->stats[0];
        for (int i = 1; i < config.backend_count; i++) {
            if (!lb->stats[i].is_healthy)
                continue;

            if (lb->stats[i].connections < state->connections) {
                state = &lb->stats[i];
                backend = &config.backends[i];
            }
        }
        return state->is_healthy ? BackServer{backend,state} :
        BackServer{nullptr,nullptr};
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

    if (ctx.config.is_ssl) {
        listen_handler = std::make_unique<HttpsListenHandler>(this);
    }
    else {
        listen_handler = std::make_unique<HttpListenHandler>(this);
    }
    return listen_handler->startup();

}

std::unique_ptr<IConnHandler> Worker::new_backend_fd() {

    auto [config, stat] = get_next_backend(ctx.config.load_balancer,ctx.load_balancer);

    if (config == nullptr) {
        log_error("Failed to find next backend");
        return nullptr;
    }

    auto result = listen_handler->connect(reinterpret_cast<sockaddr *>(&config->server_addr),
        sizeof(config->server_addr));
    if (!result)
        stat->failed_connections.fetch_add(1);

    stat->connections.fetch_add(1);
    return result;
}


bool Worker::accept_new_conn() {

    auto client = listen_handler->accept(reinterpret_cast<sockaddr *>(&client_addr));
    if (!client) {
        if (errno == EMFILE || errno == ENFILE) {
            epoll_event ev{};
            ev.events = 0;
            ev.data.fd = listen_handler->listen_fd;
            epoll_ctl(listen_handler->epoll_fd, EPOLL_CTL_MOD, listen_handler->listen_fd, &ev);
            log_error("FD limit reached, paused accepting");
            // TODO:设置定时器或标记，稍后恢复
        } else {
            log_error("accept failed: %s", strerror(errno));
        }
        return true;
    }
    int client_fd = client->conn_fd;
    client_to_backend[client_fd] = std::make_unique<ProxyHandler>();
    client_to_backend[client_fd]->client = std::make_unique<NormalConnHandler>(this,std::move(client),true);

    if (ctx.config.backend_type == ListenerConfig::LOAD_BALANCER) {
        auto backend = new_backend_fd();
        int backend_fd = backend ? backend->conn_fd : -1;
        if (backend) {
            client_to_backend[client_fd]->backend = std::make_unique<NormalConnHandler>(this,std::move(backend),false);
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
        //log_debug("New connection: client %d -> static",client_fd);

    }
    return false;
}

void Worker::process_events() {

    auto ev_count = listen_handler->wait(events, MAX_EVENTS, -1);
    for (int i = 0;i < ev_count; i++) {
        if (events[i].data.fd == listen_handler->listen_fd) {
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
    if (startup()) {
        spdlog::error("Worker start up failed");

        return;
    }
    while (!needExit || !client_to_backend.empty()) {
        process_events();
    }

    log_info("Exiting worker");
}

