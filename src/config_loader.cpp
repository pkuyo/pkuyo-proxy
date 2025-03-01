//
// Created by pkuyo on 25-2-26.
//
#include "config_loader.h"

#include <cstdio>
#include <cstring>
#include <jansson.h>

#include "spdlog/spdlog.h"

int load_config(const char *filename, ProxyConfig & config) {
    json_error_t error;
    json_t *root = json_load_file(filename, 0, &error);
    if (!root) {
        spdlog::error("Error loading JSON config: {}", error.text);
        return -1;
    }

    const json_t *listen_ports = json_object_get(root, "listen_ports");
    config.listener_count = json_array_size(listen_ports);

    for (int i = 0; i < config.listener_count; i++) {
        json_t *item = json_array_get(listen_ports, i);

        ListenerConfig & lc = config.listeners.emplace_back();
        lc.port = json_integer_value(json_object_get(item, "port"));
        strcpy(lc.server_name, json_string_value(json_object_get(item, "server_name")));
        lc.process_count = json_integer_value(json_object_get(item, "process_count"));

        json_t* health = json_object_get(item, "check_health_time");
        if (health) {
            lc.check_health_time = json_integer_value(health);
        }

        auto ssl = json_object_get(item, "ssl");
        if (ssl && (lc.is_ssl = json_is_true(ssl))) {
            strcpy(lc.ssl_key_file, json_string_value(json_object_get(item, "key")));
            strcpy(lc.ssl_cert_file, json_string_value(json_object_get(item, "cert")));
        }

        const auto backends = json_object_get(item, "backends");
        if (backends) {
            const auto count = json_array_size(backends);

            auto load_balancing = std::string(json_string_value(json_object_get(item, "load_balancing")));
            lc.backend_type = ListenerConfig::LOAD_BALANCER;
            if (load_balancing == "weighted_round_robin") {
                lc.load_balancer.algorithm = LoadBalancingAlgorithm::WeightedRoundRobin;
            }
            else if (load_balancing == "round_robin") {
                lc.load_balancer.algorithm = LoadBalancingAlgorithm::RoundRobin;
            }
            else if (load_balancing == "least_connections") {
                lc.load_balancer.algorithm = LoadBalancingAlgorithm::LeastConnections;
            }


            for (int j = 0; j < count; j++) {
                auto& backend = lc.load_balancer.backends.emplace_back();
                json_t *backend_item = json_array_get(backends, i);

                backend.server_addr.sin_family = AF_INET;
                backend.server_addr.sin_port = htons(json_integer_value(json_object_get(backend_item, "port")));
                inet_pton(AF_INET, json_string_value(json_object_get(backend_item, "host")), & backend.server_addr.sin_addr);

                if (const auto weight = json_object_get(backend_item, "weight")) {
                    backend.weight = json_integer_value(weight);
                }
                if (lc.load_balancer.algorithm == LoadBalancingAlgorithm::WeightedRoundRobin) {
                    if (backend.weight == 0)
                        backend.weight = 1;
                    lc.load_balancer.total_weight += backend.weight;
                }


            }
        }
        else {
            lc.backend_type = ListenerConfig::STATIC;
            lc.static_file.root_path = std::string(json_string_value(json_object_get(item, "root")));
        }

    }

    json_decref(root);
    return 0;
}
