//
// Created by pkuyo on 25-2-26.
//

#ifndef HTTP_HANDER_H
#define HTTP_HANDER_H

#include "def.h"
bool send_http_buffer(ConnectionContext & ctx, ConnectionContext & rev_ctx, epoll_event & ev, int epoll_id);
bool handle_http_buffer(ConnectionContext & ctx);
#endif //HTTP_HANDER_H
