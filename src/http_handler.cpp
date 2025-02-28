//
// Created by pkuyo on 25-2-26.
//
//
// Created by pkuyo on 25-2-25.
//

#include "http_handler.h"

#include <fstream>
#include <helper.h>
#include <string>
#include <unistd.h>
#include <sys/epoll.h>
#include <sstream>
#include <worker.h>
#include <sys/stat.h>

#include "spdlog/spdlog.h"

std::string get_status_msg(int errorCode) {
    switch (errorCode) {
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 400: return "Bad Request";
        case 405: return "Method Not Allowed";
        case 404: return "Not Found";
        default: return "Unknown Status";
    }
}


std::string backend_error_response(int errorCode) {
    std::stringstream response("<html><head><title>");
    response << errorCode <<" " <<get_status_msg(errorCode) <<"</title></head><body><center>pkuyo-proxy</center></body></html>";
    return response.str();
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
    context.request.url_path = headers.substr(pos+1, pos2-pos-1);
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
    response.content = backend_error_response(502);
    response.header = gen_http_header(   response.content.c_str(), 502);
    response.complete();
    get_response_head(response);

    rev_handler->recv_content(std::move(response));
}

char NormalConnHandler::buffer[BUFFER_SIZE]{};

NormalConnHandler::NormalConnHandler(Worker* owner,int _fd, int _epoll_fd, bool _is_request)
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
                    else {
                        tmp_context.content.reserve(tmp_context.data.content_remaining);
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
    if (!is_request && !content.request.is_valid()) {
        HttpContext response;
        response.content = backend_error_response(400);
        response.header = gen_http_header(response.content.c_str(), 400);
        response.complete();
        rev_handler->recv_content(std::move(response));
        return;
    } else if (is_request && !content.response.is_valid()) {
        content.content = backend_error_response(500);
        content.header = gen_http_header(content.content .c_str(), 500);
    }
    queue.push(std::move(content));
    if (!is_sending)
        handle_write_event();
}


std::string StaticFileHandler::get_mime_type(const std::string &file_path) {
    size_t dot_pos = file_path.find_last_of('.');
    if (dot_pos == std::string::npos) return "application/octet-stream";

    std::string ext = file_path.substr(dot_pos + 1);
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "js") return "application/javascript";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "json") return "application/json";
    return "text/plain";
}

bool StaticFileHandler::resolve_full_path(const std::string &url_path, std::string &full_path) {
    std::string decoded_path = url_decode(url_path);
    std::string combined_path = root_dir + "/" + decoded_path;

    char resolved[PATH_MAX];
    if (realpath(combined_path.c_str(), resolved) == nullptr) return false;

    full_path = resolved;
    return full_path.find(root_dir) == 0;
}
std::string StaticFileHandler::url_decode(const std::string &str)  {
    std::string result;
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i+2 < str.size()) {
            int hex = std::stoi(str.substr(i+1, 2), nullptr, 16);
            result += static_cast<char>(hex);
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

void StaticFileHandler::recv_content(HttpContext &&context) {
    if (context.request.method != "GET") {
        send_error(405);
        return;
    }

    std::string url_path(context.request.url_path);
    std::string full_path;
    if (!resolve_full_path(url_path, full_path)) {
        send_error(404);
        return;
    }

    //获取文件状态
    struct stat st;
    if (stat(full_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        send_error(404);
        return;
    }


    std::ifstream file(full_path, std::ios::binary);
    if (!file.is_open()) {
        send_error(500);
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());

    HttpContext response;
    response.header = "HTTP/1.1 200 OK\r\n";
    response.header += "Content-Type: " + get_mime_type(full_path) + "\r\n";
    response.header += "Content-Length: " + std::to_string(content.size()) + "\r\n";
    response.header += "Connection: close\r\n\r\n";
    response.content = std::move(content);
    response.complete();
    get_response_head(response);

    rev_handler->recv_content(std::move(response));
}

void StaticFileHandler::send_error(int code) const {

    HttpContext response;

    response.content = std::move(backend_error_response(code));
    response.header = std::move(gen_http_header(response.content.c_str(), code));
    response.complete();
    get_response_head(response);

    rev_handler->recv_content(std::move(response));
}








