//
// Created by pkuyo on 25-2-26.
//

#ifndef HTTP_HANDER_H
#define HTTP_HANDER_H

#include <conn_handler.h>
#include <memory>
#include <sys/epoll.h>

#include "def.h"


class Worker;

class IProxyHandler {
protected:

    explicit IProxyHandler(Worker* _owner) : owner(_owner) {}

public:

    Worker* owner;

    IProxyHandler* rev_handler = nullptr;

    virtual ~IProxyHandler() = default;

    virtual bool handle_read_event() {
        return false;
    }

    virtual bool handle_write_event() {
        return false;
    }

    virtual int id() {
        return -1;
    }

    virtual void recv_content(HttpContext&& content) = 0;
};


struct ProxyHandler {
    std::unique_ptr<IProxyHandler> client;
    std::unique_ptr<IProxyHandler> backend;

    void init_link() const {
        backend->rev_handler = client.get();
        client->rev_handler = backend.get();
    }
};


class FailedBackendHandler final : public IProxyHandler {
public:
    explicit FailedBackendHandler(Worker *_owner)
        : IProxyHandler(_owner) {
    }

    void recv_content(HttpContext&& content) override;
};

class NormalConnHandler final : public IProxyHandler {

    std::queue<HttpContext> queue;

    HttpContext tmp_context;
    std::string raw_buffer;

    ConnState state = ConnState::READ_HEADERS;

    bool is_sending = false;

    bool is_request;

    std::unique_ptr<IConnHandler> conn_handler;


    epoll_event event{};

    static char buffer[BUFFER_SIZE];


public:
    explicit NormalConnHandler(Worker* owner,std::unique_ptr<IConnHandler>&& _conn_handler, bool _is_request);

    void recv_content(HttpContext &&content) override;

    bool handle_read_event() override;

    bool handle_write_event() override;

    int id() override {
        return conn_handler->conn_fd;
    }

private:
    void log_access();
    bool read_raw_buff();
    void parse_http_context();
    void parse_chunked_content();
};

class StaticFileHandler final : public IProxyHandler {

    std::string root_dir;

    std::string get_mime_type(const std::string& file_path);

    bool resolve_full_path(const std::string& url_path, std::string& full_path);

    std::string url_decode(const std::string& str);

public:
    StaticFileHandler(Worker* owner, const std::string& root)
        : IProxyHandler(owner), root_dir(root) {}

    void recv_content(HttpContext&& context) override;

private:
    void send_error(int code) const;
};

#endif //HTTP_HANDER_H
