#ifndef SUBS_HTTP_FAKE_H
#define SUBS_HTTP_FAKE_H

#include <stddef.h>

#include "http.h"

struct http_fake_response {
    enum http_method method;
    const char *url, *post_data, *data;
};

struct http_fake_server {
    const struct http_fake_response *responses;
    size_t n;
};

struct http_client http_client_fake_init(const struct http_fake_server *s);

#endif
