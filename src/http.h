#ifndef SUBS_HTTP_H
#define SUBS_HTTP_H

#include <stdbool.h>
#include <stdint.h>

#include "def.h"

struct buffer;

typedef bool http_get_fn(void *p, const char *url, struct buffer *buffer);
typedef bool http_post_fn(
    void *p, const char *url, const char *post_data, struct buffer *buffer);

enum http_method {
    HTTP_GET,
    HTTP_POST,
};

enum http_flags {
    HTTP_VERBOSE = (u32)1 << 0,
};

struct http_client {
    void *data;
    u32 flags;
    http_get_fn *get;
    http_post_fn *post;
};

static const char *http_method_str(enum http_method m);
static void http_client_init(struct http_client *c, u32 flags);
bool http_get(void *p, const char *url, struct buffer *buffer);
bool http_post(
    void *p, const char *url, const char *post_data, struct buffer *buffer);

static inline const char *http_method_str(enum http_method m) {
    switch(m) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    default: return "unknown method";
    }
}

static inline void http_client_init(struct http_client *c, u32 flags) {
    *c = (struct http_client){
        .data = c,
        .flags = flags,
        .get = http_get,
        .post = http_post,
    };
}

#endif
