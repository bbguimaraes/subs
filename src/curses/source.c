#include "source.h"

#include <stdlib.h>

#include "../buffer.h"
#include "../def.h"
#include "../log.h"
#include "../subs.h"

#include "curses.h"
#include "subs.h"
#include "videos.h"

#include "window/list_search.h"

enum { ALL, TAGS, UNTAGGED };

static bool populate(
    sqlite3 *db, const char *sql, size_t len, int width,
    i64 *ids, char **lines)
{
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(db, sql, (int)len, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret = false;
    for(;;) {
        switch(sqlite3_step(stmt)) {
        case SQLITE_ROW: break;
        case SQLITE_BUSY: continue;
        case SQLITE_DONE: ret = true; /* fall through */
        default: goto end;
        }
        *ids++ = sqlite3_column_int64(stmt, 0);
        *lines++ = name_with_counts(
            width, "  ", (const char*)sqlite3_column_text(stmt, 1),
            sqlite3_column_int(stmt, 2), sqlite3_column_int(stmt, 3));
    }
end:
    return (sqlite3_finalize(stmt) == SQLITE_OK) && ret;
}

static bool select(struct source_bar *b) {
    const int types = UNTAGGED + b->n_tags + 1;
    const int i = b->list.i;
    if(i == TAGS || i == types)
        return true;
    struct subs_bar *const s = b->subs_bar;
    struct videos *const v = b->videos;
    if(i == UNTAGGED) {
        subs_bar_set_untagged(s);
        videos_set_untagged(v);
    } else if(i < types) {
        const int id = (int)b->list.ids[i];
        subs_bar_set_tag(s, id);
        videos_set_tag(v, id);
    } else {
        const int id = (int)b->list.ids[i];
        subs_bar_set_type(s, id);
        videos_set_type(v, id);
    }
    return subs_bar_reload(s)
        && videos_reload(v)
        && change_window(b->s, VIDEOS_IDX);
}

static void render_border(struct list *l, u8 flags, const struct search *s) {
    enum { BAR = 1, SPACE = 1 };
    flags &= WATCHED | NOT_WATCHED;
    list_box(l);
    int x = -SPACE;
    if(flags) {
        x -= 2 * SPACE;
        char b[2], *p = b;
        if(flags & WATCHED)
            --x, *p++ = 'w';
        else if(flags & NOT_WATCHED)
            --x, *p++ = 'W';
        *p++ = 0;
        list_write_title(l, x, " %s ", b);
    }
    if(!search_is_active(s))
        return;
    const struct buffer search = s->b;
    x -= (search.n ? (int)search.n - 1 : 0) + BAR + (1 + !(bool)flags) * SPACE;
    list_write_title(l, x, " /%s ", search.p ? (const char*)search.p : "");
}

static enum subs_curses_key input_search(struct source_bar *b, int c) {
    struct search *const s = &b->search;
    struct list *const l = &b->list;
    const enum subs_curses_key ret = list_search_input(s, l, c);
    if(ret == KEY_HANDLED) {
        list_box(l);
        render_border(l, b->s->flags, s);
        list_refresh(l);
    }
    return ret;
}

void source_bar_update_title(struct source_bar *b) {
    struct list *const l = &b->list;
    // XXX
    if(!l->w) return;
    render_border(l, b->s->flags, &b->search);
    list_refresh(l);
}

bool source_bar_update_count(struct source_bar *b) {
    const char sql[] = "select count(*) from tags";
    int n = 0;
    if(!query_to_int(b->s->db, sql, sizeof(sql) - 1, NULL, &n))
        return false;
    b->n_tags = n;
    return true;
}

bool source_bar_reload(struct source_bar *b) {
    struct subs_curses *const s = b->s;
    const int lbry = SUBS_LBRY, youtube = SUBS_YOUTUBE;
    sqlite3 *const db = s->db;
    const int n_tags = b->n_tags;
    int
        n_videos = 0, n_unwatched = 0, n_untagged = 0, n_untagged_unwatched = 0,
        n_lbry = 0, n_lbry_unwatched = 0,
        n_youtube = 0, n_youtube_unwatched = 0;
    const char count_all[] = "select count(*) from videos";
    const char count_unwatched[] =
        "select count(*) from videos where watched == 0";
#define Q \
    "select count(*) from videos" \
    " left outer join subs_tags on videos.sub == subs_tags.sub" \
    " left outer join videos_tags on videos.id == videos_tags.video" \
    " where subs_tags.id is null and videos_tags.id is null"
    const char count_untagged[] = Q;
    const char count_untagged_unwatched[] = Q " and videos.watched == 0";
#undef Q
#define Q \
    "select count(*) from videos" \
    " left outer join subs on videos.sub == subs.id" \
    " where subs.type == ?"
    const char types[] = Q;
    const char types_unwatched[] = Q " and videos.watched == 0";
#undef Q
    if(!(
        query_to_int(db, count_all, sizeof(count_all), NULL, &n_videos)
        && query_to_int(
            db, count_unwatched, sizeof(count_unwatched) - 1, NULL,
            &n_unwatched)
        && query_to_int(
            db, count_untagged, sizeof(count_untagged) - 1, NULL, &n_untagged)
        && query_to_int(
            db, count_untagged_unwatched, sizeof(count_untagged_unwatched) - 1,
            NULL, &n_untagged_unwatched)
        && query_to_int(db, types, sizeof(types) - 1, &lbry, &n_lbry)
        && query_to_int(db, types, sizeof(types) - 1, &youtube, &n_youtube)
        && query_to_int(
            db, types_unwatched, sizeof(types_unwatched) - 1,
            &lbry, &n_lbry_unwatched)
        && query_to_int(
            db, types_unwatched, sizeof(types_unwatched) - 1,
            &youtube, &n_youtube_unwatched)
    ))
        return false;
    const int n_special = 2, n_sections = 2, n_types = 2, null_term = 1;
    const int n = n_special + n_sections + n_tags + n_types + null_term;
    i64 *const ids = checked_calloc((size_t)n, sizeof(*ids));
    if(!ids)
        goto err0;
    char **const lines = checked_calloc((size_t)n, sizeof(*lines));
    if(!lines)
        goto err1;
    const int text_width = b->width - 4;
    const int i_tags = UNTAGGED + 1, i_types = i_tags + n_tags + 1;
    lines[ALL] = name_with_counts(text_width, "", "all", n_unwatched, n_videos);
    lines[TAGS] = strdup("tags");
    lines[UNTAGGED] =
        name_with_counts(text_width, "  ", "[untagged]",
        n_untagged_unwatched, n_untagged);
    const char tags[] =
        "select"
            " tags.id, name, count(tags.id) filter (where videos.watched == 0),"
            " count(tags.id)"
        " from tags"
        " left outer join subs_tags on tags.id == subs_tags.tag"
        " left outer join videos_tags on tags.id == videos_tags.tag"
        " left outer join videos on subs_tags.sub == videos.sub"
        " group by tags.id"
        " order by name";
    if(!populate(
        db, tags, sizeof(tags) - 1, text_width,
        ids + i_tags, lines + i_tags
    ))
        goto err2;
    lines[i_types - 1] = strdup("types");
    lines[i_types + 0] = name_with_counts(
        text_width, "  ", "lbry", n_lbry_unwatched, n_lbry);
    lines[i_types + 1] = name_with_counts(
        text_width, "  ", "youtube", n_youtube_unwatched, n_youtube);
    ids[i_types + 0] = SUBS_LBRY;
    ids[i_types + 1] = SUBS_YOUTUBE;
    if(!list_init(
        &b->list, NULL, (int)n - 1, ids, lines, b->x, b->y, b->width, n + 1
    ))
        goto err2;
    render_border(&b->list, b->s->flags, &b->search);
    if(b->n_tags != n_tags)
        s->flags = (u8)(s->flags | RESIZED);
    b->n_tags = n_tags;
    list_refresh(&b->list);
    return true;
err2:
    for(char **p = lines; *p; ++p)
        free(*p);
    free(lines);
err1:
    free(ids);
err0:
    return false;
}

void source_bar_destroy(struct source_bar *b) {
    list_destroy(&b->list);
    free(b->search.b.p);
}

bool source_bar_leave(void *data) {
    list_set_active(&((struct source_bar*)data)->list, false);
    return true;
}

bool source_bar_enter(void *data) {
    list_set_active(&((struct source_bar*)data)->list, true);
    curs_set(0);
    return true;
}

void source_bar_redraw(void *data) {
    struct list *const l = &((struct source_bar*)data)->list;
    list_redraw(l);
    list_refresh(l);
}

enum subs_curses_key source_bar_input(void *data, int c) {
    struct source_bar *b = data;
    if(search_is_input_active(&b->search))
        return input_search(b, c);
    switch(c) {
    case '\n': if(!select(b)) return false; break;
    case '/':
        search_reset(&b->search);
        render_border(&b->list, b->s->flags, &b->search);
        list_refresh(&b->list);
        break;
    case 'n':
        if(!search_is_empty(&b->search))
            list_search_next(&b->search, &b->list);
        break;
    default: return list_input(&b->list, c);
    }
    list_refresh(&b->list);
    return true;
}
