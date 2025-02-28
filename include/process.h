//
// Created by pkuyo on 25-2-28.
//

#ifndef PROCESS_H
#define PROCESS_H
#include "def.h"
#include "spdlog/spdlog.h"
struct Process {
    explicit Process(ProcContext&& _ctx) : ctx(std::move(_ctx)), format_buffer{} {
    }

    char format_buffer[512];

public:
    ProcContext ctx;
    template<typename ...Args>
    void log_info(const char * message, Args... args) {
        sprintf(format_buffer,message,std::forward<Args>(args)...);
        spdlog::info("{}, server:{}:{}.",format_buffer,
        ctx.config.server_name,
        ctx.config.port);
    }
    template<typename ...Args>
    void log_debug(const char * message, Args... args) {
        sprintf(format_buffer,message,std::forward<Args>(args)...);
        spdlog::debug("{}, server:{}:{}.",format_buffer,
        ctx.config.server_name,
        ctx.config.port);
    }

    template<typename ...Args>
    void log_error(const char * message, Args... args) {
        sprintf(format_buffer,message,std::forward<Args>(args)...);
        spdlog::error("{}, server:{}:{}.",format_buffer,
        ctx.config.server_name,
        ctx.config.port);
    }

    void log_error(const char * message, bool has_error = true) const;

    void log_info(const char * message) const;

    void log_debug(const char * message) const;

};
#endif //PROCESS_H
