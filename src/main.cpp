

#include "logger.h"
#include "config_loader.h"
#include "init_listener.h"
int main() {
    ProxyConfig config;

    if (setup_logger())
        exit(1);

    spdlog::info("Pkuyo-Proxy start up");

    if (load_config("config/proxy_config.json",config))
        exit(1);

    fork_listeners(config);

    while (true) {
        pause();
    }
    spdlog::shutdown();
    return 0;
}