#include "subs.h"

#include <stdlib.h>

#include "../buffer.h"
#include "../util.h"

#include "curses.h"
#include "videos.h"

enum flags {
    UNTAGGED = 1u << 0,
};

static void clear_selection(struct subs_bar *b) {
    b->tag = b->type = 0;
    b->flags = (u8)(b->flags & ~UNTAGGED);
}

static char *subs_row_to_str(sqlite3_stmt *stmt, int width) {
    return name_with_counts(
        width, "", (const char*)sqlite3_column_text(stmt, 1),
        sqlite3_column_int(stmt, 2), sqlite3_column_int(stmt, 3));
}

static void build_query_common(
    int tag, int type, u8 global_flags, u8 flags, struct buffer *b)
{
    const bool watched = global_flags & WATCHED;
    const bool not_watched = global_flags & NOT_WATCHED;
    const bool untagged = flags & UNTAGGED;
    const bool filtered = tag || type || untagged;
    if(untagged || tag)
        --b->n, buffer_append_str(b,
            " left outer join subs_tags on subs.id == subs_tags.sub"
            " left outer join videos_tags on videos.id == videos_tags.video");
    if(untagged)
        --b->n, buffer_append_str(b,
            " where (subs_tags.id is null and videos_tags.id is null)");
    else if(tag)
        --b->n, buffer_append_str(b,
            " where (videos_tags.tag == ?1 or subs_tags.tag == ?1)");
    else if(type)
        --b->n, buffer_append_str(b, " where subs.type == ?");
    if(watched) {
        if(filtered)
            --b->n, buffer_append_str(b, " and");
        else
            --b->n, buffer_append_str(b, " where");
        --b->n, buffer_append_str(b, " videos.watched == 1");
    } else if(not_watched) {
        if(filtered)
            --b->n, buffer_append_str(b, " and");
        else
            --b->n, buffer_append_str(b, " where");
        --b->n, buffer_append_str(b, " videos.watched == 0");
    }
}

static void build_query_count(
    int tag, int type, u8 global_flags, u8 flags, struct buffer *b)
{
    const bool watched = global_flags & WATCHED;
    const bool not_watched = global_flags & NOT_WATCHED;
    const bool untagged = flags & UNTAGGED;
    const bool filtered = tag || type || untagged || watched || not_watched;
    if(filtered)
        buffer_append_str(b,
            "select count(distinct subs.id) from subs"
            " left outer join videos on subs.id == videos.sub");
    else
        buffer_append_str(b, "select count(*) from subs");
    build_query_common(tag, type, global_flags, flags, b);
}

static void build_query_list(
    int tag, int type, u8 global_flags, u8 flags, struct buffer *b)
{
    buffer_append_str(b,
        "select"
            " subs.id, subs.name,"
            " count(videos.id) filter (where videos.watched == 0),"
            " count(videos.id)"
        " from subs"
        " left outer join videos on subs.id == videos.sub");
    build_query_common(tag, type, global_flags, flags, b);
    --b->n, buffer_append_str(b, " group by subs.id order by subs.name");
}

static bool populate(
    sqlite3 *db, const char *sql, size_t len, const int *param, int width,
    char **lines, int *ids)
{
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(db, sql, (int)len, 0, &stmt, NULL);
    if(!stmt)
        return false;
    if(param)
        sqlite3_bind_int(stmt, 1, *param);
    bool ret = false;
    for(;;) {
        switch(sqlite3_step(stmt)) {
        case SQLITE_ROW: break;
        case SQLITE_BUSY: continue;
        case SQLITE_DONE: ret = true; /* fall through */
        default: goto end;
        }
        *ids++ = sqlite3_column_int(stmt, 0);
        *lines++ = subs_row_to_str(stmt, width);
    }
end:
    *lines = NULL;
    return (sqlite3_finalize(stmt) == SQLITE_OK) && ret;
}

static bool select_item(struct subs_bar *b, int i) {
    struct videos *const v = b->videos;
    videos_set_sub(v, b->items[i]);
    return videos_reload(v)
        && change_window(b->s, VIDEOS_IDX);
}

static bool reload_item(struct subs_bar *b) {
    const char sql[] =
        "select"
            " subs.id, subs.name,"
            " count(videos.id) filter (where videos.watched == 0),"
            " count(videos.id)"
        " from subs"
        " left outer join videos on subs.id == videos.sub"
        " where subs.id == ?"
        " group by subs.id";
    const int id = b->items[b->list.i];
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(b->s->db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret = false;
    struct buffer str = {0};
    if(sqlite3_bind_int(stmt, 1, id) != SQLITE_OK)
        goto err0;
    for(;;) {
        switch(sqlite3_step(stmt)) {
        case SQLITE_ROW: break;
        case SQLITE_BUSY: continue;
        case SQLITE_DONE: /* fall through */
        default: goto end;
        }
        list_set_name(&b->list, "%s", subs_row_to_str(stmt, b->width - 4));
        ret = true;
        goto end;
    }
end:
err0:
    free(str.p);
    ret = (sqlite3_finalize(stmt) == SQLITE_OK) && ret;
    return ret;
}

void subs_bar_toggle_watched(struct subs_bar *b) {
    if((b->flags ^= WATCHED))
        b->flags = (u8)((unsigned)b->flags & ~(unsigned)NOT_WATCHED);
}

void subs_bar_toggle_not_watched(struct subs_bar *b) {
    if((b->flags ^= NOT_WATCHED))
        b->flags = (u8)((unsigned)b->flags & ~(unsigned)WATCHED);
}

void subs_bar_set_untagged(struct subs_bar *b) {
    clear_selection(b);
    b->flags |= UNTAGGED;
}

void subs_bar_set_tag(struct subs_bar *b, int t) {
    clear_selection(b);
    b->tag = t;
}

void subs_bar_set_type(struct subs_bar *b, int t) {
    clear_selection(b);
    b->type = t;
}

bool subs_bar_next(struct subs_bar *b) {
    struct list *const l = &b->list;
    if(l->i == l->n - 1)
        return true;
    const int i = l->i + 1;
    list_move(&b->list, i);
    return select_item(b, i);
}

bool subs_bar_prev(struct subs_bar *b) {
    struct list *const l = &b->list;
    if(!l->i)
        return true;
    const int i = l->i - 1;
    list_move(&b->list, i);
    return select_item(b, i);
}

bool subs_bar_reload(struct subs_bar *b) {
    sqlite3 *const db = b->s->db;
    const int width = b->width;
    const int tag = b->tag, type = b->type;
    const int *const param = tag ? &tag : type ? &type : NULL;
    const u8 global_flags = b->s->flags, flags = b->flags;
    int n = 0;
    struct buffer sql = {0};
    build_query_count(tag, type, global_flags, flags, &sql);
    if(!query_to_int(db, sql.p, sql.n - 1, param, &n))
        goto err0;
    char **lines = calloc((size_t)n + 1, sizeof(*lines));
    if(!lines)
        goto err0;
    int *const items = calloc((size_t)n, sizeof(*items));
    if(n && !items)
        goto err0;
    sql.n = 0;
    build_query_list(tag, type, global_flags, flags, &sql);
    if(!populate(db, sql.p, sql.n - 1, param, width - 4, lines, items))
        goto err1;
    if(!list_init(&b->list, NULL, n, lines, b->x, b->y, width, LINES - b->y))
        goto err1;
    free(sql.p);
    b->items = items;
    b->width = width;
    const unsigned n_len = int_digits(n);
    list_write_title(&b->list, -((int)n_len + 3), " %d ", n);
    list_refresh(&b->list);
    return true;
err1:
    for(char **p = lines; *p; ++p)
        free(*p);
    free(lines);
    free(items);
err0:
    free(sql.p);
    return false;
}

void subs_bar_destroy(struct subs_bar *b) {
    list_destroy(&b->list);
    free(b->items);
}

bool subs_bar_leave(void *data) {
    list_set_active(&((struct subs_bar*)data)->list, false);
    return true;
}

bool subs_bar_enter(void *data) {
    list_set_active(&((struct subs_bar*)data)->list, true);
    curs_set(0);
    return true;
}

void subs_bar_redraw(void *data) {
    struct subs_bar *const b = data;
    struct list *l = &b->list;
    list_redraw(l);
    list_refresh(l);
}

enum subs_curses_key subs_bar_input(void *data, int c) {
    struct subs_bar *b = data;
    switch(c) {
    case '\n': return select_item(b, b->list.i);
    case 'r': if(!reload_item(b)) return false; break;
    default: return list_input(&b->list, c);
    }
    list_refresh(&b->list);
    return true;
}
