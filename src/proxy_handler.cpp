//
// Created by pkuyo on 25-2-26.
//
//
// Created by pkuyo on 25-2-25.
//

#include "proxy_handler.h"

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
        case 200: return "OK";
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


#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <stdexcept>

using namespace std;

// 辅助函数：去除字符串两端的空白字符
void trim(string& s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == string::npos) {
        s.clear();
        return;
    }
    size_t end = s.find_last_not_of(" \t");
    s = s.substr(start, end - start + 1);
}

// 辅助函数：转换为小写
string to_lower(string s) {
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return tolower(c); });
    return s;
}

// 检查Transfer-Encoding是否包含chunked
bool contains_chunked(const string& value) {
    size_t start = 0;
    while (true) {
        size_t comma = value.find(',', start);
        string token = value.substr(start, comma - start);
        trim(token);
        if (to_lower(token) == "chunked") {
            return true;
        }
        if (comma == string::npos) break;
        start = comma + 1;
    }
    return false;
}

vector<string> split_headers(const string& headers) {
    vector<string> lines;
    size_t start = 0;
    while (start < headers.size()) {
        size_t end = headers.find("\r\n", start);
        if (end == string::npos) {
            lines.push_back(headers.substr(start));
            break;
        }
        lines.push_back(headers.substr(start, end - start));
        start = end + 2;
    }
    return lines;
}
// 处理HTTP头部
pair<bool,size_t> process_http_headers(HttpContext & context, const char* addr, bool is_request) {
    vector<string> lines = split_headers(context.header);
    bool chunked = false;
    size_t content_length = 0;

    int xff_idx = -1;
    string xff_value;
    int xri_idx = -1;
    vector<string> field_names;

    // 第一遍遍历：收集关键信息
    for (size_t i = 1; i < lines.size(); ++i) {
        string& line = lines[i];
        size_t colon = line.find(':');
        if (colon == string::npos) continue;

        string fn = line.substr(0, colon);
        string fv = line.substr(colon + 1);
        trim(fn);
        trim(fv);
        string lfn = to_lower(fn);
        field_names.push_back(lfn);

        if (lfn == "transfer-encoding") {
            if (contains_chunked(fv)) chunked = true;
        } else if (lfn == "content-length") {
            try { content_length = stoul(fv); }
            catch (...) {
                /* 处理错误 */
            }
        } else if (is_request && lfn == "x-forwarded-for") {
            if (xff_idx == -1) { xff_idx = i; xff_value = fv; }
        } else if (is_request && lfn == "x-real-ip") {
            if (xri_idx == -1) xri_idx = i;
        }
    }

    if (is_request) {
        string addr_str(addr);
        if (xff_idx != -1) {
            lines[xff_idx] = lines[xff_idx].substr(0, lines[xff_idx].find(':') + 1) + " " + xff_value + ", " + addr_str;
        } else {
            lines.push_back("X-Forwarded-For: " + addr_str);
        }


        if (xri_idx != -1) {
            lines[xri_idx] = "X-Real-IP: " + addr_str;
        } else {
            lines.push_back("X-Real-IP: " + addr_str);
        }
    }

    context.header.clear();
    for (const string& line : lines) {
        context.header += line + "\r\n";
    }
    context.header += "\r\n";
    //spdlog::info(context.header);
    return {chunked, content_length};
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
    std::string_view header = context.header;
    size_t first_space = header.find(' ');
    if (first_space == std::string_view::npos) {
        return true;
    }


    // 查找第二个空格
    size_t second_space = header.find(' ', first_space + 1);
    if (second_space == std::string_view::npos) {
        second_space = header.size();
    }

    // 提取状态码部分
    std::string_view status_code_str = header.substr(first_space + 1, second_space - (first_space + 1));


    try {
        context.response.status_code = std::stoi(std::string(status_code_str));
        return false;
    } catch (...) {
        return true;
    }
}


void FailedBackendHandler::recv_content(HttpContext &&content) {
    HttpContext response;
    response.content = backend_error_response(502);
    response.header = gen_http_header(response.content.c_str(), 502);
    response.complete();
    get_response_head(response);

    rev_handler->recv_content(std::move(response));
}

char NormalConnHandler::buffer[BUFFER_SIZE]{};

NormalConnHandler::NormalConnHandler(Worker* owner,std::unique_ptr<IConnHandler>&& _conn_handler, bool _is_request)
    : IProxyHandler(owner),conn_handler(std::move(_conn_handler)),is_request(_is_request) {
}

bool NormalConnHandler::handle_read_event() {
    if (!conn_handler->handshake_done)
        if (conn_handler->handshake())
            return true;
    if (!conn_handler->handshake_done)
        return false;

    if (read_raw_buff()) {
        return true;
    }
    parse_http_context();
    return false;
}

bool NormalConnHandler::handle_write_event() {
    if (!conn_handler->handshake_done)
        if (conn_handler->handshake())
            return true;
    if (!conn_handler->handshake_done)
        return false;
    
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
        auto[error, byte_send] = conn_handler->write(buffer.data(),buffer.size());
        if (byte_send == -1) {
            if  (!error) {
                result = false;
                need_listen = true;
                break;
            }
            owner->log_error("send http buff failed, error:%s",strerror(errno));
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

            conn_handler->modify_event(EPOLLIN | EPOLLET | EPOLLOUT,&event);

            is_sending = true;
        }
        else if (!need_listen && is_sending) {
            conn_handler->modify_event(EPOLLIN | EPOLLET ,&event);
            is_sending = false;
        }
    }
    return result;
}


void NormalConnHandler::log_access() {
    if (is_request) {
        if (get_request_head(tmp_context)) {
            owner->log_error("get_request_head failed");
            return;
        }
        owner->log_info("reqeust: %.*s %.*s, From %s:%d",
            static_cast<int>(tmp_context.request.method.size()),
            tmp_context.request.method.data(),
            static_cast<int>(tmp_context.request.url_path.size()),
            tmp_context.request.url_path.data(),
            conn_handler->ip,
            conn_handler->port);
    }
    else {
        if (get_response_head(tmp_context)) {
            owner->log_error("get_response_head failed");
            return;
        }
    }
}

bool NormalConnHandler::read_raw_buff() {
    while (true) {
        auto [error, bytes_read] = conn_handler->read(buffer,BUFFER_SIZE);
        if (bytes_read == -1) {
            if  (!error)
                return false;

            owner->log_error("read_raw_buff failed, error:%s",strerror(errno));
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
                tmp_context.header = std::move(raw_buffer.substr(0, header_end + 2));

                auto [is_chunk, content_length ] =
                    process_http_headers(tmp_context,conn_handler->ip,is_request);

                if (is_chunk) {
                    state = ConnState::READ_CHUNK_SIZE;
                    log_access();
                }
                else {
                    tmp_context.data.content_remaining = content_length;
                    if (tmp_context.data.content_remaining == -1) {
                        raw_buffer.erase(0, header_end + 4);
                        owner->log_error("get_content_length failed");
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
        get_response_head(content);
    }
    if (is_request)
        owner->log_info("response: %d",content.response.status_code);
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








