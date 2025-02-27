//
// Created by pkuyo on 25-2-26.
//

#ifndef DEF_H
#define DEF_H

#include <atomic>
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
    ListenerConfig & config;
    int mem_length = -1;
    int shm_fd = -1;
    struct ShmLoadBalancer* load_balancer;

    void Clear() {

    }
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


#endif //DEF_H
