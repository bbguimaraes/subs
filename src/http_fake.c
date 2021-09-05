#include "http_fake.h"

#include <string.h>

#include "buffer.h"
#include "log.h"
#include "util.h"

static const struct http_fake_response *match(
    const struct http_fake_server *s,
    enum http_method method, const char *url, const char *post_data)
{
    const char *const slash = strchr(url, '/');
    if(slash)
        url = slash + 1;
    else
        url = "/";
    const struct http_fake_response *p = s->responses;
    const struct http_fake_response *const e = p + s->n;
    for(; p != e; ++p) {
        if(p->method != method)
            continue;
        if(strcmp(p->url, url) != 0)
            continue;
        if(method == HTTP_GET)
            return p;
        if(post_data) {
            if(!p->post_data)
                continue;
            if(strcmp(p->post_data, post_data) != 0)
                continue;
        } else if(p->post_data)
            continue;
        return p;
    }
    return NULL;
}

static bool serve(
    const struct http_fake_server *s,
    enum http_method method, const char *url, const char *post_data,
    struct buffer *b)
{
    const struct http_fake_response *const ret =
        match(s, method, url, post_data);
    if(!ret) {
        log_err(
            "%s: unexpected request: %s %s (post data: %s)\n",
            __func__, http_method_str(method), url, post_data);
        return false;
    }
    buffer_append_str(b, ret->data);
    return true;
}

static bool get(void *p, const char *url, struct buffer *b) {
    return serve(p, HTTP_GET, url, NULL, b);
}

static bool post(void *p, const char *url, const char *data, struct buffer *b) {
    return serve(p, HTTP_POST, url, data, b);
}

struct http_client http_client_fake_init(const struct http_fake_server *s) {
    return (struct http_client){
        .data = (struct http_fake_server*)s,
        .get = get,
        .post = post,
    };
}
