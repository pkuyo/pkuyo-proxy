//
// Created by pkuyo on 25-2-26.
//

#ifndef DEF_H
#define DEF_H

#include <atomic>
#include <queue>
#include <vector>
#include <arpa/inet.h>

#define MAX_EVENTS 30

constexpr int BUFFER_SIZE = 4096;

enum class LoadBalancingAlgorithm {
    RoundRobin,         // 轮询
    WeightedRoundRobin, // 加权轮询
    LeastConnections    // 最小连接数
};

struct BackendServerConfig {
    sockaddr_in server_addr;
    int weight = 0;
};

struct LoadBalancerConfig{
    int backend_count = 0;
    int total_weight = 0;
    LoadBalancingAlgorithm algorithm;
    std::vector<BackendServerConfig> backends;

};

struct StaticFileConfig {
    std::string root_path;
};

struct ListenerConfig{
    int port{};
    char server_name[256]{};
    int process_count{};

    bool is_ssl = false;

    char ssl_cert_file[256]{};
    char ssl_key_file[256]{};

    enum {
        LOAD_BALANCER,
        STATIC
    } backend_type = STATIC;

    LoadBalancerConfig load_balancer;
    StaticFileConfig static_file;

    ListenerConfig();
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
    std::atomic<int> connections = 0;
    std::atomic<int> failed_connections = 0;
    std::atomic<int> current_weight = 0;
};
struct ShmLoadBalancer {
    std::atomic<int> current_index = 0;
    BackendStat* stats = nullptr;
};

struct epoll_event;


struct HttpContext {

    enum  {
        SEND_HEADER,
        SEND_CONTENT,
    } state = SEND_HEADER;

    union {
        size_t content_remaining;
        size_t chunk_size;
    } data{};


    bool is_completed;


    struct {
        std::string_view url_path;
        std::string_view method;
        [[nodiscard]] bool is_valid() const {
            return !url_path.empty() && !method.empty();
        }
    } request;
    struct {
        int status_code = -1;
        [[nodiscard]] bool is_valid() const {
            return status_code != -1;
        }
    } response;

    std::string header;
    std::string content;
    HttpContext() noexcept: is_completed(false) {
    }

    HttpContext(HttpContext && other) noexcept : is_completed(other.is_completed),
                                                 request(other.request),
                                                 response(other.response),
                                                 state(other.state),
                                                 header(std::move(other.header)),
                                                 content(std::move(other.content)) {

    }

    void complete() {
        is_completed = true;
        state = SEND_HEADER;
    }

    void reset() {
        data.chunk_size = 0;
        is_completed = false;
        state = SEND_HEADER;
        header.clear();
        content.clear();
        request.url_path = request.method = "";
        response.status_code = -1;

    }
};

enum class ConnState {
    READ_HEADERS,
    READ_BODY,
    READ_CHUNK_SIZE,
    READ_CHUNK_DATA,
};




pid_t fork_with_cleanup();

#endif //DEF_H
