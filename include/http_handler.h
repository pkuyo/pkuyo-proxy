//
// Created by pkuyo on 25-2-26.
//

#ifndef HTTP_HANDER_H
#define HTTP_HANDER_H

#include <memory>
#include <sys/epoll.h>

#include "def.h"


class Worker;

class IConnHandler {
protected:

    explicit IConnHandler(Worker* _owner) : owner(_owner) {}

public:

    Worker* owner;

    IConnHandler* rev_handler = nullptr;

    virtual ~IConnHandler() = default;

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
    std::unique_ptr<IConnHandler> client;
    std::unique_ptr<IConnHandler> backend;

    void init_link() const {
        backend->rev_handler = client.get();
        client->rev_handler = backend.get();
    }
};


class FailedBackendHandler final : public IConnHandler {
public:
    explicit FailedBackendHandler(Worker *_owner)
        : IConnHandler(_owner) {
    }

    void recv_content(HttpContext&& content) override;
};

class NormalConnHandler final : public IConnHandler {

    std::queue<HttpContext> queue;

    HttpContext tmp_context;
    std::string raw_buffer;

    ConnState state = ConnState::READ_HEADERS;

    bool is_sending = false;

    bool is_request;

    int fd;
    int epoll_fd;

    epoll_event event{};

    static char buffer[BUFFER_SIZE];


public:
    explicit NormalConnHandler(Worker* owner,int _fd, int _epoll_fd, bool _is_request);

    void recv_content(HttpContext &&content) override;

    bool handle_read_event() override;

    bool handle_write_event() override;

    int id() override {
        return fd;
    }

    ~NormalConnHandler() override;
private:
    void log_access();
    bool read_raw_buff();
    void parse_http_context();
    void parse_chunked_content();
};

class StaticFileHandler final : public IConnHandler {

    std::string root_dir;

    std::string get_mime_type(const std::string& file_path);

    bool resolve_full_path(const std::string& url_path, std::string& full_path);

    std::string url_decode(const std::string& str);

public:
    StaticFileHandler(Worker* owner, const std::string& root)
        : IConnHandler(owner), root_dir(root) {}

    void recv_content(HttpContext&& context) override;

private:
    void send_error(int code) const;
};

#endif //HTTP_HANDER_H
