//
// Created by pkuyo on 25-2-27.
//
#include "logger.h"
#include <cstdio>
#include <sys/stat.h>

int setup_logger()
{
    try {
        // 1. 创建支持多进程的 rotating_file_sink
        std::string log_file = "logs/access.log";
        const char* dir_path = "./logs";
        if (access(dir_path, F_OK)) {
            if (!mkdir(dir_path, 0755)) {
                fprintf(stderr,"Error creating dir: %s",strerror(errno));
                return -1;
            }
        }


        auto sink = std::make_shared<rotating_multi_process_file_sink<std::mutex>>(log_file,1024 * 1024 * 5, 3);

        // 2. 创建同步 logger
        auto logger = std::make_shared<spdlog::logger>("multiprocess_logger", sink);

        // 3. 设置日志格式和级别
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [PID:%P] [%l] %v");
        logger->set_level(spdlog::level::info);

        // 4. 注册为全局 logger
        spdlog::register_logger(logger);
        spdlog::set_default_logger(logger);

    } catch (const spdlog::spdlog_ex& ex) {
        fprintf(stderr, "Log initialization failed: %s\n", ex.what());
        return -1;
    }
    return 0;
}
