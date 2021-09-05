#ifndef SUBS_UPDATE_H
#define SUBS_UPDATE_H

#include <stdbool.h>

#include "def.h"

struct buffer;
struct http_client;
struct subs;

bool update_lbry(
    const struct subs *s, const struct http_client *http, struct buffer *b,
    u32 flags, int id, const char *ext_id);

#endif
