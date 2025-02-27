//
// Created by pkuyo on 25-2-28.
//
#include "process.h"

void Process::log_error(const char *message, bool has_error) const {
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
void Process::log_info(const char *message) const {
    spdlog::info("{}, server:{}:{}.",
        message,
    ctx.config.server_name,
    ctx.config.port);
}

void Process::log_debug(const char *message) const {
    spdlog::debug("{}, server:{}:{}.",
        message,
    ctx.config.server_name,
    ctx.config.port);
}