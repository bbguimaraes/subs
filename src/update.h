#ifndef SUBS_UPDATE_H
#define SUBS_UPDATE_H

#include <stdbool.h>

#include <unistd.h>

#include "def.h"

struct buffer;
struct http_client;
struct subs;

struct update_youtube {
    pid_t channel_pid;
    int channel_r, channel_w;
    pid_t info_pid;
    int info_r, info_w;
};

bool update_lbry(
    const struct subs *s, const struct http_client *http, struct buffer *b,
    u32 flags, int depth, int id, const char *ext_id);
bool update_youtube_init(struct update_youtube *u);
bool update_youtube_destroy(struct update_youtube *u);
bool update_youtube(
    const struct subs *s, struct update_youtube *u, struct buffer *b, u32 flags,
    int depth, int id, const char *ext_id);

#endif
