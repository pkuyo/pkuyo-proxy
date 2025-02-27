//
// Created by pkuyo on 25-2-26.
//
//
// Created by pkuyo on 25-2-25.
//

#include "http_handler.h"

#include <string>
#include <iostream>
#include <unistd.h>
#include <sys/epoll.h>

#include "spdlog/spdlog.h"


bool read_raw_buff(ConnectionContext & ctx) {
    static char buffer[BUFFER_SIZE];
    while (true) {
        ssize_t bytes_read = read(ctx.fd,buffer,BUFFER_SIZE);
        if (bytes_read == -1) {
            if  (errno == EAGAIN || errno == EWOULDBLOCK)
                return false;

            spdlog::error("read_raw_buff failed, error:{}, pid:{}, from:{}, to:{}",strerror(errno), getpid(),
                 ctx.fd,ctx.to_fd);
            return true;
        }
        else if (bytes_read == 0) {
            return true;
        }
        ctx.raw_buffer.append(buffer,bytes_read);
    }
}

size_t get_content_length(const std::string& headers) {
    size_t content_length_pos = headers.find("Content-Length:");
    if (content_length_pos == std::string::npos) {
        return 0; // 没有Content-Length头
    }
    content_length_pos += 15;
    while (content_length_pos < headers.size() &&
           (headers[content_length_pos] == ' ' || headers[content_length_pos] == '\t')) {
        ++content_length_pos;
           }
    size_t end_pos = headers.find("\r\n", content_length_pos);
    if (end_pos == std::string::npos) {
        return -1; // 格式错误
    }
    std::string value_str = headers.substr(content_length_pos, end_pos - content_length_pos);
    try {
        return std::stoul(value_str);
    } catch (...) {
        return -1; // 转换失败
    }
}
bool is_chunked(const std::string& headers) {
    size_t transfer_encoding_pos = headers.find("Transfer-Encoding:");
    if (transfer_encoding_pos == std::string::npos) {
        return false;
    }
    size_t chunked_pos = headers.find("chunked", transfer_encoding_pos);
    return chunked_pos != std::string::npos;
}
void parse_chunked_data(ConnectionContext& ctx) {
    while (!ctx.raw_buffer.empty()) {
        auto & current_context =  ctx.queue.back();
        if (ctx.state == ConnectionContext::READ_CHUNK_SIZE) {
            // 查找分块大小行
            size_t chunk_size_end = ctx.raw_buffer.find("\r\n");
            if (chunk_size_end == std::string::npos) {
                break; // 数据不完整，等待更多数据
            }

            // 解析分块大小
            std::string chunk_size_str = ctx.raw_buffer.substr(0, chunk_size_end);
            size_t chunk_size = std::stoul(chunk_size_str, nullptr, 16); // 十六进制转十进制
            ctx.raw_buffer.erase(0, chunk_size_end + 2); // 移除已解析的分块大小行

            if (chunk_size == 0) {
                // 最后一个分块，消息结束
                ctx.state = ConnectionContext::READ_HEADERS;
                current_context.is_completed = true;
                break;
            }

            current_context.data.chunk_size = chunk_size;
            ctx.state = ConnectionContext::READ_CHUNK_DATA;
        } else if (ctx.state == ConnectionContext::READ_CHUNK_DATA) {
            // 读取分块数据
            size_t bytes_to_read = std::min(current_context.data.chunk_size, ctx.raw_buffer.size());
            current_context.buffer.append(ctx.raw_buffer.data(), bytes_to_read);
            ctx.raw_buffer.erase(0, bytes_to_read);
            current_context.data.chunk_size -= bytes_to_read;

            if (current_context.data.chunk_size == 0) {
                // 当前分块读取完毕，检查分块结束符
                if (ctx.raw_buffer.size() >= 2 && ctx.raw_buffer.substr(0, 2) == "\r\n") {
                    ctx.raw_buffer.erase(0, 2); // 移除分块结束符
                }
                ctx.state = ConnectionContext::READ_CHUNK_SIZE; // 准备读取下一个分块
            }
        }
    }
}

void parse_http_context(ConnectionContext & ctx) {
    while (!ctx.raw_buffer.empty()) {

        if (ctx.state == ConnectionContext::READ_HEADERS) {
            size_t header_end = ctx.raw_buffer.find("\r\n\r\n");

            bool ignored = false;

            if (header_end != std::string::npos) {
                // 解析到完整的头部
                HttpContext request;
                request.buffer = ctx.raw_buffer.substr(0, header_end + 4);
                if (is_chunked(ctx.raw_buffer)) {
                    ctx.state = ConnectionContext::READ_CHUNK_SIZE;
                }
                else {
                    request.data.content_remaining = get_content_length(request.buffer);
                    if ( request.data.content_remaining == -1) {
                        ignored = true;
                        ctx.raw_buffer.erase(0, header_end + 4);
                        spdlog::error("get_content_length failed, pid:{}, from:{}, to:{}", getpid(),
                            ctx.fd,ctx.to_fd);
                        continue;
                    }
                    ctx.state = ConnectionContext::READ_BODY;
                    if (request.data.content_remaining == 0) {
                        ctx.state = ConnectionContext::READ_HEADERS;
                        request.is_completed = true;
                    }
                }
                ctx.queue.push(std::move(request));

                ctx.raw_buffer.erase(0, header_end + 4); // 移除已解析的头部


            } else {
                // 头部不完整，等待更多数据
                break;
            }
        }
        else if (ctx.state == ConnectionContext::READ_BODY) {
            auto & current_context = ctx.queue.back();
            // 读取消息体
            size_t bytes_to_read = std::min(current_context.data.content_remaining, ctx.raw_buffer.size());
            current_context.buffer.append(ctx.raw_buffer.data(), bytes_to_read);
            ctx.raw_buffer.erase(0, bytes_to_read); // 移除已解析的消息体
            current_context.data.content_remaining -= bytes_to_read;

            if (current_context.data.content_remaining == 0) {
                ctx.state = ConnectionContext::READ_HEADERS;
                current_context.is_completed = true;
            }
        }
        else if (ctx.state == ConnectionContext::READ_CHUNK_SIZE || ctx.state == ConnectionContext::READ_CHUNK_DATA) {
            // 解析chunked数据
            parse_chunked_data(ctx);
        }
    }
}

bool send_http_buffer(ConnectionContext & ctx, epoll_event & ev, int epoll_id) {
    bool result, need_listen = false;
    while (true) {
        if (ctx.queue.empty() || !ctx.queue.front().is_completed) {
            result = false;
            need_listen = false;
            break;
        }
        auto & current_context = ctx.queue.front();

        ssize_t byte_send = write(ctx.to_fd,current_context.buffer.data(),current_context.buffer.size());

        if (byte_send == -1) {
            if  (errno == EAGAIN || errno == EWOULDBLOCK) {
                result = false;
                need_listen = true;
                break;
            }
            spdlog::error("send http buff failed, error:{}, pid:{}, from:{}, to:{}",strerror(errno), getpid(),
                ctx.fd,ctx.to_fd);
            result = true;
            break;
        }
        else if (byte_send == 0) {
            result = true;
            break;
        }
        if (byte_send != current_context.buffer.size()) {
            current_context.buffer.erase(0,byte_send);
        }
        else {
            ctx.queue.pop();
        }
    }
    if (!result) {
        if (need_listen && !ctx.is_sending) {
            ev.events = EPOLLIN | EPOLLET | EPOLLOUT;
            epoll_ctl(epoll_id, EPOLL_CTL_MOD, ctx.to_fd, &ev);
            ctx.is_sending = true;
        }
        else if (!need_listen && ctx.is_sending) {
            ev.events = EPOLLIN | EPOLLET ;
            epoll_ctl(epoll_id, EPOLL_CTL_MOD, ctx.to_fd, &ev);
            ctx.is_sending = false;
        }
    }
    return result;
}

bool handle_http_buffer(ConnectionContext & ctx, epoll_event & ev, int epoll_id) {
    if (read_raw_buff(ctx))
        return true;
    parse_http_context(ctx);
    if (!ctx.is_sending)
        return send_http_buffer(ctx,ev,epoll_id);
    return false;
}

