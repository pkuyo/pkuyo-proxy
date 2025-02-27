//
// Created by pkuyo on 25-2-27.
//

#ifndef LOGGER_H
#define LOGGER_H
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <unistd.h>
#include <sys/file.h>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <iomanip>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/details/null_mutex.h>

template<typename Mutex>
class rotating_multi_process_file_sink : public spdlog::sinks::base_sink<Mutex> {
public:
    explicit rotating_multi_process_file_sink(const std::string& folder)
        : folder_name(folder){
        open_file();
    }

    ~rotating_multi_process_file_sink() override {
        if (info_fd != -1) close(info_fd);
        if (error_fd != -1) close(error_fd);
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);

        if (msg.level >= spdlog::level::err) {
            if (::write(error_fd, formatted.data(), formatted.size()) == -1) {
                throw std::runtime_error("Failed to write error log");
            }

        } else {

            if (::write(info_fd, formatted.data(), formatted.size()) == -1) {
                throw std::runtime_error("Failed to write info_fd log");
            }
        }
    }

    void flush_() override {

    }


private:
    void open_file() {
        auto file_name = folder_name +"/access.log";
        info_fd = open(file_name.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (info_fd == -1) {
            throw std::runtime_error("Failed to open file: " + file_name);
        }
        auto error_file = folder_name +"/error.log";
        error_fd = open(error_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (error_fd == -1) {
            throw std::runtime_error("Failed to open file: " + error_file);
        }
    }









    std::string folder_name;

    int info_fd = -1;
    int error_fd = -1;

};
int setup_logger();


#endif //LOGGER_H
