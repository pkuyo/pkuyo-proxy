//
// Created by pkuyo on 25-2-26.
//

#ifndef DEF_H
#define DEF_H

#include <atomic>
#include <queue>
#include <vector>
#include <arpa/inet.h>


enum class LoadBalancingAlgorithm {
    RoundRobin,         // 轮询
    WeightedRoundRobin, // 加权轮询
    LeastConnections    // 最小连接数
};

struct BackendServerConfig {
    sockaddr_in server_addr;
    int weight;
};

struct LoadBalancerConfig{
    int backend_count;
    LoadBalancingAlgorithm algorithm;
    std::vector<BackendServerConfig> backends;
};


struct ListenerConfig{
    int port{};
    char server_name[256]{};
    int process_count{};
    LoadBalancerConfig load_balancer{};
};

struct ProxyConfig {
    std::vector<ListenerConfig> listeners;
    int listener_count;
};

struct ProcContext {
    explicit ProcContext(ListenerConfig & _config) : config(_config) {}

    ProcContext(ProcContext && other) noexcept : config(other.config),
        mem_length(other.mem_length),
        shm_fd(other.shm_fd),
        load_balancer(other.load_balancer)
    {
        other.is_valid = false;
    }

    ProcContext(const ProcContext & other) = delete;
    ProcContext& operator=(const ProcContext&) = delete;

    ListenerConfig & config;
    int mem_length = -1;
    int shm_fd = -1;
    struct ShmLoadBalancer* load_balancer = nullptr;

    bool is_valid = true;

    ~ProcContext();
};



struct BackendStat {
    std::atomic<int> connections = 0; // 当前连接数（用于最小连接数策略）
    std::atomic<int> failed_connections = 0;
};
struct ShmLoadBalancer {
    //pthread_mutex_t accept_mutex{};
    std::atomic<int> current_index = 0;
    BackendStat* stats = nullptr;

    ShmLoadBalancer() {
        // pthread_mutexattr_t attr;
        // pthread_mutexattr_init(&attr);
        // pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        // pthread_mutex_init(&accept_mutex, &attr);
    }
};

struct epoll_event;
const int MAX_EVENTS = 1024;
const int BUFFER_SIZE = 4096;

struct HttpContext {
    union {
        size_t content_remaining;
        size_t chunk_size;
    } data;

    bool is_completed;
    std::string buffer;
};



struct ConnectionContext {
    int fd;
    int to_fd;

    enum {
        READ_HEADERS,
        READ_BODY,
        READ_CHUNK_SIZE,
        READ_CHUNK_DATA,
        } state = READ_HEADERS;
    bool is_sending;
    std::queue<HttpContext> queue;
    std::string raw_buffer;
};


struct ProxyContext {

    ProxyContext(int client_fd,int backend_fd) :
        client_ctx{.fd = client_fd, .to_fd = backend_fd},
        backend_ctx{.fd = backend_fd, .to_fd = client_fd},
        client_fd(client_fd),
        backend_fd(backend_fd)
    {}

    ConnectionContext client_ctx;
    ConnectionContext backend_ctx;

    int client_fd;
    int backend_fd;
};


pid_t fork_with_cleanup();

#endif //DEF_H
