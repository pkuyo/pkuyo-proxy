//
// Created by pkuyo on 25-2-27.
//

#ifndef LOGGER_H
#define LOGGER_H
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <unistd.h>
#include <sys/file.h>



#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/details/file_helper.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <stdexcept>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

template<typename Mutex>
class rotating_multi_process_file_sink : public spdlog::sinks::base_sink<Mutex> {
public:
    explicit rotating_multi_process_file_sink(const std::string& filename, std::size_t max_size, std::size_t max_files)
        : filename_(filename), max_size_(max_size), max_files_(max_files) {
        fd_ = open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd_ == -1) {
            throw std::runtime_error("Failed to open file: " + filename);
        }
    }

    ~rotating_multi_process_file_sink() override {
        if (fd_ != -1) close(fd_);
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);

        // 获取文件锁（阻塞直到成功）
        struct flock lock = {};
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;

        if (fcntl(fd_, F_SETLKW, &lock) == -1) {
            throw std::runtime_error("Failed to acquire file lock");
        }

        try {
            off_t current_size = lseek(fd_, 0, SEEK_END);
            if (current_size == -1) {
                throw std::runtime_error("Failed to get file size");
            }

            if (static_cast<std::size_t>(current_size) + formatted.size() > max_size_) {
                rotate_();
            }


            if (::write(fd_, formatted.data(), formatted.size()) == -1) {
                throw std::runtime_error("Failed to write to file");
            }
        } catch (...) {

            lock.l_type = F_UNLCK;
            fcntl(fd_, F_SETLK, &lock);
            throw;
        }


        lock.l_type = F_UNLCK;
        if (fcntl(fd_, F_SETLK, &lock) == -1) {
            throw std::runtime_error("Failed to release file lock");
        }
    }

    void flush_() override {
        if (fsync(fd_) == -1) {
            throw std::runtime_error("Failed to flush file");
        }
    }

private:
    void rotate_() {

        if (fd_ != -1) {
            close(fd_);
            fd_ = -1;
        }
        for (int i = max_files_ - 1; i > 0; --i) {
            std::string old_name = i == 1 ? filename_ : filename_ + "." + std::to_string(i - 1);
            std::string new_name = filename_ + "." + std::to_string(i);

            if (access(old_name.c_str(), F_OK) == 0) {
                if (rename(old_name.c_str(), new_name.c_str()) != 0) {
                    throw std::runtime_error("Failed to rotate file: " + old_name + " to " + new_name);
                }
            }
        }

        fd_ = open(filename_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd_ == -1) {
            throw std::runtime_error("Failed to open file: " + filename_);
        }
    }

    std::string filename_;
    std::size_t max_size_;
    std::size_t max_files_;
    int fd_ = -1;
};
int setup_logger();


#endif //LOGGER_H
