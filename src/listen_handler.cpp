//
// Created by pkuyo on 25-3-1.
//
#include "listen_handler.h"

#include <helper.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "worker.h"
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/err.h>

bool HttpListenHandler::startup() {

    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        owner->log_error("Failed to create socket");
        return true;
    }
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        owner->log_error("setsockopt(SO_REUSEADDR | SO_REUSEPORT) failed");
        return true;
    }

    sockaddr_in server_addr{};
    // 绑定地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(owner->ctx.config.port);
    if (bind(listen_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) == -1) {
        owner->log_error("Failed to bind");
        return true;
    }

    // 监听
    if (listen(listen_fd, SOMAXCONN) == -1) {
         owner->log_error("Failed to listen");
        return true;
    }

    // 创建 epoll 实例
    if ((epoll_fd = epoll_create1(0)) == -1) {
        owner->log_error("Failed to create epoll");
        return true;
    }

    // 添加监听套接字到 epoll
    set_nonblocking(listen_fd);
    add_epoll_fd(epoll_fd, listen_fd, EPOLLIN);
    owner->log_info("Listen on port:%d", owner->ctx.config.port);
    return false;
}

int HttpListenHandler::wait(epoll_event *events, int max_event, int time_out) {
    return epoll_wait(epoll_fd, events, max_event, time_out);
}

std::unique_ptr<IConnHandler> HttpListenHandler::accept(sockaddr *addr) {
    socklen_t len = sizeof(sockaddr_in);
    int fd = ::accept(listen_fd, addr, &len);
    if (fd == -1)
        return nullptr;
    set_nonblocking(fd);
    add_epoll_fd(epoll_fd, fd, EPOLLIN | EPOLLET);

    auto result = std::make_unique<HttpConnHandler>(this, fd,addr);
    result->handshake();
    return result;
}

std::unique_ptr<IConnHandler> HttpListenHandler::connect(sockaddr *addr, int len) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        owner->log_error("Failed to create backend socket, %s",strerror(errno));
        close(fd);
        fd = -1;
    }


    if (::connect(fd, addr,len) == -1) {
        owner->log_error( "Failed to connect to backend, %s",strerror(errno));
        close(fd);
        fd = -1;
    }
    if (fd != -1) {
        set_nonblocking(fd);
        add_epoll_fd(epoll_fd, fd, EPOLLIN | EPOLLET);
    }
    if (fd == -1)
        return nullptr;
    return std::make_unique<HttpConnHandler>(this, fd,addr);
}

// 初始化 OpenSSL
bool HttpsListenHandler::initialize_openssl() {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    // 创建 SSL_CTX 对象
    const SSL_METHOD* method = TLS_server_method();
    ssl_ctx = SSL_CTX_new(method);
    if (!ssl_ctx) {
        owner->log_error("Failed to create SSL_CTX: %s", ERR_error_string(ERR_get_error(), nullptr));;
        return true;
    }

    // 加载证书和私钥
    if (SSL_CTX_use_certificate_file(ssl_ctx, owner->ctx.config.ssl_cert_file, SSL_FILETYPE_PEM) <= 0) {
      owner->log_error("Failed to load certificate: %s, path:%s", ERR_error_string(ERR_get_error(), nullptr),
          owner->ctx.config.ssl_cert_file);
        return true;
    }
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx,  owner->ctx.config.ssl_key_file, SSL_FILETYPE_PEM) <= 0) {
        owner->log_error("Failed to load private key: %s, path:%s", ERR_error_string(ERR_get_error(), nullptr)
            ,owner->ctx.config.ssl_key_file);
        return true;
    }

    // 验证私钥和证书是否匹配
    if (!SSL_CTX_check_private_key(ssl_ctx)) {
        owner->log_error("Private key does not match the certificate: %s", ERR_error_string(ERR_get_error(), nullptr));
        return true;
    }

    return false;
}

bool HttpsListenHandler::startup() {
    if (initialize_openssl())
        return true;

    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        owner->log_error("Failed to create socket");
        return true;
    }
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        owner->log_error("setsockopt(SO_REUSEADDR | SO_REUSEPORT) failed");
        return true;
    }

    sockaddr_in server_addr{};
    // 绑定地址
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(owner->ctx.config.port);
    if (bind(listen_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) == -1) {
        owner->log_error("Failed to bind");
        return true;
    }

    // 监听
    if (listen(listen_fd, SOMAXCONN) == -1) {
        owner->log_error("Failed to listen");
        return true;
    }

    // 创建 epoll 实例
    if ((epoll_fd = epoll_create1(0)) == -1) {
        owner->log_error("Failed to create epoll");
        return true;
    }

    // 添加监听套接字到 epoll
    set_nonblocking(listen_fd);
    add_epoll_fd(epoll_fd, listen_fd, EPOLLIN);
    owner->log_info("Listen on port:%d", owner->ctx.config.port);
    return false;
}


std::unique_ptr<IConnHandler> HttpsListenHandler::accept(sockaddr *addr) {
    socklen_t len = sizeof(sockaddr_in);
    int fd = ::accept(listen_fd, addr, &len);
    if (fd == -1)
        return nullptr;
    set_nonblocking(fd);
    add_epoll_fd(epoll_fd, fd, EPOLLIN | EPOLLET);
    SSL* ssl = SSL_new(ssl_ctx);
    SSL_set_fd(ssl, fd);

    auto result = std::make_unique<HttpsConnHandler>(this,ssl,fd,addr);

    if (result->handshake())
        return nullptr;

    return result;
}


HttpsListenHandler::~HttpsListenHandler() {
    if (ssl_ctx) {
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = nullptr;
    }
    EVP_cleanup();
    ERR_free_strings();
}
