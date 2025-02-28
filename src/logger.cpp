//
// Created by pkuyo on 25-2-27.
//
#include "logger.h"
#include <cstdio>
#include <sys/stat.h>

int setup_logger()
{
    try {
        const char* dir_path = "/home/pkuyo/pkuyo_proxy/logs";
        if (access(dir_path, F_OK)) {
            if (!mkdir(dir_path, 0755)) {
                fprintf(stderr,"Error creating dir: %s",strerror(errno));
                return -1;
            }
        }

        auto sink = std::make_shared<rotating_multi_process_file_sink<std::mutex>>(dir_path);
        auto logger = std::make_shared<spdlog::logger>("multiprocess_logger", sink);


        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [PID:%P] [%l] %v");
        logger->set_level(spdlog::level::info);

        register_logger(logger);
        set_default_logger(logger);

    } catch (const spdlog::spdlog_ex& ex) {
        fprintf(stderr, "Log initialization failed: %s\n", ex.what());
        return -1;
    }
    return 0;
}
