//
// Created by pkuyo on 25-2-26.
//
//
// Created by pkuyo on 25-2-25.
//

#include "http_handler.h"

#include <helper.h>
#include <string>
#include <unistd.h>
#include <sys/epoll.h>
#include <sstream>
#include <worker.h>

#include "spdlog/spdlog.h"

static const char* backend_error_response = R"(<html>
<head><title>502 Bad Gateway</title></head>
<body>
<center><h1>502 Bad Gateway</h1></center>
<hr><center>pkuyo-proxy</center>
</body>
</html>)";

std::string get_status_msg(int errorCode) {
    switch (errorCode) {
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        default: return "Unknown Status";
    }
}

std::string gen_http_header(const char* content, int errorCode) {
    std::stringstream response;

    response << "HTTP/1.1 " << errorCode << " " << get_status_msg(errorCode) << "\r\n";

    response << "Content-Type: text/html\r\n";
    response << "Content-Length: " << strlen(content) << "\r\n";
    response << "Connection: close\r\n";

    response << "\r\n";
    return response.str();
}

//失败返回空
std::string_view get_header_arg(const std::string_view& headers,const std::string_view& key) {
    static const std::string zero("0");
    size_t content_length_pos = headers.find(key);
    if (content_length_pos == std::string::npos) {
        return zero;
    }
    content_length_pos+= key.size();
    while (content_length_pos < headers.size() &&
       (headers[content_length_pos] == ' ' || headers[content_length_pos] == '\t')) {
        ++content_length_pos;
       }
    size_t end_pos = headers.find("\r\n", content_length_pos);
    if (end_pos == std::string::npos) {
        return {};
    }
    return headers.substr(content_length_pos, end_pos - content_length_pos);
}

size_t get_content_length(const std::string& headers) {
    auto value_str = std::string(get_header_arg(headers,"Content-Length:"));
    try {
        return std::stoul(value_str);
    } catch (...) {
        return -1;
    }
}

bool is_chunked(const std::string& headers) {
    return get_header_arg(headers,"Transfer-Encoding:") == "chunked";
}

bool get_request_head(HttpContext & context) {
    std::string_view headers = context.header;
    auto pos = headers.find(' ');
    if (pos == std::string::npos) {
        return true;
    }
    context.request.method = headers.substr(0, pos);
    auto pos2 = headers.find(' ',pos+1);
    if (pos2 == std::string::npos) {
        return true;
    }
    context.request.url_path = headers.substr(pos+1, pos2);
    return false;
}

bool get_response_head(HttpContext & context) {
    std::string_view headers = context.header;
    auto pos = headers.find(' ');
    if (pos == std::string::npos) {
        return true;
    }
    context.response.status_code = headers.substr(0, pos);
    return false;
}


void FailedBackendHandler::recv_content(HttpContext &&content) {
    HttpContext response;
    response.header = gen_http_header(backend_error_response, 502);
    response.content = backend_error_response;
    response.complete();
    rev_handler->recv_content(std::move(response));
}

char NormalConnHandler::buffer[BUFFER_SIZE]{};

NormalConnHandler:: NormalConnHandler(Worker* owner,int _fd, int _epoll_fd, bool _is_request)
    : IConnHandler(owner),fd(_fd),epoll_fd(_epoll_fd),is_request(_is_request) {
    set_nonblocking(_fd);
    add_epoll_fd(_epoll_fd, _fd, EPOLLIN | EPOLLET);
}

bool NormalConnHandler::handle_read_event() {
    if (read_raw_buff()) {
        return true;
    }
    parse_http_context();
    return false;
}

bool NormalConnHandler::handle_write_event() {
    bool result, need_listen = false;
    while (true) {
        if (queue.empty() || !queue.front().is_completed) {
            result = false;
            need_listen = false;
            break;
        }
        auto & current_context = queue.front();
        auto & buffer = current_context.header;
        if (current_context.state == HttpContext::SEND_CONTENT)
            buffer = current_context.content;
        ssize_t byte_send = write(fd,buffer.data(),buffer.size());
        if (byte_send == -1) {
            if  (errno == EAGAIN || errno == EWOULDBLOCK) {
                result = false;
                need_listen = true;
                break;
            }
            owner->log_error("send http buff failed, error:%s, fd:%d",strerror(errno),fd);
            result = true;
            break;
        }
        else if (byte_send == 0) {
            result = true;
            break;
        }
        if (byte_send != buffer.size()) {
            buffer.erase(0,byte_send);
        }
        else {
            if (current_context.state == HttpContext::SEND_CONTENT || current_context.content.empty()) {
                queue.pop();
                current_context.state = HttpContext::SEND_HEADER;
            }
            else
                current_context.state = HttpContext::SEND_CONTENT;
        }
    }
    if (!result) {
        if (need_listen && !is_sending) {
            event.events = EPOLLIN | EPOLLET | EPOLLOUT;
            event.data.fd = fd;
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
            is_sending = true;
        }
        else if (!need_listen && is_sending) {
            event.events = EPOLLIN | EPOLLET;
            event.data.fd = fd;
            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
            is_sending = false;
        }
    }
    return result;
}

NormalConnHandler::~NormalConnHandler() {
    if (fd != -1)
        close(fd);
}

void NormalConnHandler::log_access() {
    if (is_request) {
        if (get_request_head(tmp_context)) {
            owner->log_error("get_request_head failed, fd:%d", fd);
            return;
        }
        owner->log_info("reqeust: %.*s %.*s",
            static_cast<int>(tmp_context.request.method.size()),
            tmp_context.request.method.data(),
            static_cast<int>(tmp_context.request.url_path.size()),
            tmp_context.request.url_path.data());
    }
    else {
        if (get_response_head(tmp_context)) {
            owner->log_error("get_response_head failed, fd:%d", fd);
            return;
        }
        owner->log_info("response: %.*s %.*s",
         static_cast<int>(tmp_context.response.status_code.size()),
         tmp_context.response.status_code.data());
    }
}

bool NormalConnHandler::read_raw_buff() {
    while (true) {
        ssize_t bytes_read = read(fd,buffer,BUFFER_SIZE);
        if (bytes_read == -1) {
            if  (errno == EAGAIN || errno == EWOULDBLOCK)
                return false;

            owner->log_error("read_raw_buff failed, error:%s, fd:%d",strerror(errno),fd);
            return true;
        }
        else if (bytes_read == 0) {
            return true;
        }
        raw_buffer.append(buffer,bytes_read);
    }
}

void NormalConnHandler::parse_http_context() {
    while (!raw_buffer.empty()) {

        if (state == ConnState::READ_HEADERS) {
            size_t header_end = raw_buffer.find("\r\n\r\n");


            if (header_end != std::string::npos) {
                // 解析到完整的头部
                tmp_context.reset();
                tmp_context.header = raw_buffer.substr(0, header_end + 4);
                if (is_chunked(raw_buffer)) {
                    state = ConnState::READ_CHUNK_SIZE;
                    log_access();
                }
                else {
                    tmp_context.data.content_remaining = get_content_length(tmp_context.header);
                    if (tmp_context.data.content_remaining == -1) {
                        raw_buffer.erase(0, header_end + 4);
                        owner->log_error("get_content_length failed, fd:%d", fd);
                        continue;
                    }
                    state = ConnState::READ_BODY;
                    log_access();
                    if (tmp_context.data.content_remaining == 0) {
                        state = ConnState::READ_HEADERS;
                        tmp_context.complete();
                        rev_handler->recv_content(std::move(tmp_context));
                    }
                }
                raw_buffer.erase(0, header_end + 4); // 移除已解析的头部


            } else {
                // 头部不完整，等待更多数据
                break;
            }
        }
        else if (state == ConnState::READ_BODY) {
            // 读取消息体
            size_t bytes_to_read = std::min(tmp_context.data.content_remaining, raw_buffer.size());
            tmp_context.content.append(raw_buffer.data(), bytes_to_read);
            raw_buffer.erase(0, bytes_to_read); // 移除已解析的消息体
            tmp_context.data.content_remaining -= bytes_to_read;

            if (tmp_context.data.content_remaining == 0) {
                state = ConnState::READ_HEADERS;
                tmp_context.complete();
                rev_handler->recv_content(std::move(tmp_context));
            }
        }
        else if (state == ConnState::READ_CHUNK_SIZE || state == ConnState::READ_CHUNK_DATA) {
            parse_chunked_content();
        }
    }
}

void NormalConnHandler::parse_chunked_content() {
      while (!raw_buffer.empty()) {
        if (state == ConnState::READ_CHUNK_SIZE) {
            // 查找分块大小行
            size_t chunk_size_end = raw_buffer.find("\r\n");
            if (chunk_size_end == std::string::npos) {
                break;
            }
            std::string chunk_size_str = raw_buffer.substr(0, chunk_size_end);
            size_t chunk_size = std::stoul(chunk_size_str, nullptr, 16);
            raw_buffer.erase(0, chunk_size_end + 2);

            if (chunk_size == 0) {
                // 消息结束
                state = ConnState::READ_HEADERS;
                tmp_context.complete();
                rev_handler->recv_content(std::move(tmp_context));
                break;
            }

            tmp_context.data.chunk_size = chunk_size;
            state = ConnState::READ_CHUNK_DATA;
        } else if (state == ConnState::READ_CHUNK_DATA) {
            // 读取分块数据
            size_t bytes_to_read = std::min(tmp_context.data.chunk_size, raw_buffer.size());
            tmp_context.content.append(raw_buffer.data(), bytes_to_read);
            raw_buffer.erase(0, bytes_to_read);
            tmp_context.data.chunk_size -= bytes_to_read;

            if (tmp_context.data.chunk_size == 0) {

                if (raw_buffer.size() >= 2 && raw_buffer.substr(0, 2) == "\r\n") {
                    raw_buffer.erase(0, 2);
                }
                state = ConnState::READ_CHUNK_SIZE;
            }
        }
    }
}



void NormalConnHandler::recv_content(HttpContext &&content) {
    queue.push(std::move(content));
    if (!is_sending)
        handle_write_event();
}









