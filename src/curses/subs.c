#include "subs.h"

#include <ctype.h>
#include <stdlib.h>

#include "../buffer.h"
#include "../util.h"

#include "curses.h"
#include "list_search.h"
#include "videos.h"

enum flags {
    UNTAGGED =   1u << 0,
    ORDER_DESC = 1u << 1,
};

enum order { SUBS_NAME, SUBS_ID, SUBS_WATCHED, SUBS_UNWATCHED };

static const char *MENU_OPTIONS[] = {
    [SUBS_ID] = "id",
    [SUBS_NAME] = "name",
    [SUBS_WATCHED] = "watched",
    [SUBS_UNWATCHED] = "unwatched",
};

static const char *MENU_DESC[] = {
    [SUBS_ID] = "database ID",
    [SUBS_NAME] = "subscription name",
    [SUBS_WATCHED] = "watched videos",
    [SUBS_UNWATCHED] = "unwatched videos",
};

static void render_border(
    struct list *l, const struct search *s, u8 flags, u8 order)
{
    enum { BAR = 1, SPACE = 1 };
    const int n = l->n;
    list_box(l);
    int x = 0;
    x -= (int)int_digits(n) + 3 * SPACE;
    list_write_title(l, x, " %d ", n);
    if(order) {
        const char cs[] = {0, 'i', 'w', 'u'};
        assert(order < sizeof(cs));
        const char c = cs[order];
        x -= 2 + 2 * SPACE;
        list_write_title(l, x, " O:%c ", flags & ORDER_DESC ? toupper(c) : c);
    }
    if(!search_is_active(s))
        return;
    const struct buffer search = s->b;
    x -= (search.n ? (int)search.n - 1 : 0) + BAR + SPACE;
    list_write_title(l, x, " /%s ", search.p ? (const char*)search.p : "");
}

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
    int tag, int type, u8 global_flags, u8 flags, u8 order, struct buffer *b)
{
    buffer_append_str(b,
        "select"
            " subs.id, subs.name,"
            " count(videos.id) filter (where videos.watched == 0),"
            " count(videos.id)"
        " from subs"
        " left outer join videos on subs.id == videos.sub");
    build_query_common(tag, type, global_flags, flags, b);
    --b->n, buffer_append_str(b, " group by subs.id");
    --b->n, buffer_append_str(b, " order by ");
    --b->n;
    switch(order) {
    case SUBS_NAME: buffer_append_str(b, "subs.name"); break;
    case SUBS_ID: buffer_append_str(b, "subs.id"); break;
    case SUBS_WATCHED:
        buffer_append_str(b,
            "count(case when videos.watched then 1 else null end)");
        break;
    case SUBS_UNWATCHED:
        buffer_append_str(b,
            "count(case when not videos.watched then 1 else null end)");
        break;
    }
    if(flags & ORDER_DESC)
        --b->n, buffer_append_str(b, " desc");
    switch(order) {
    case SUBS_WATCHED:
    case SUBS_UNWATCHED:
        --b->n, buffer_append_str(b, ", subs.name");
    }
}

static bool populate(
    sqlite3 *db, const char *sql, size_t len, const int *param, int width,
    i64 *ids, char **lines)
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
        *ids++ = sqlite3_column_int64(stmt, 0);
        *lines++ = subs_row_to_str(stmt, width);
    }
end:
    *lines = NULL;
    return (sqlite3_finalize(stmt) == SQLITE_OK) && ret;
}

static enum subs_curses_key input_menu(struct subs_bar *b, int c) {
    struct menu *const m = &b->menu;
    switch(c) {
    case ERR:
        return false;
    default:
        return menu_input(m, c) ? true : KEY_IGNORED;
    case '\n':
        b->order = (u8)menu_current(m);
        menu_destroy(m);
        m->m = NULL;
        return subs_bar_reload(b);
    case ESC:
    case 'q':
        menu_destroy(m);
        m->m = NULL;
        list_redraw(&b->list);
        list_refresh(&b->list);
        return true;
    }
}

static enum subs_curses_key input_search(struct subs_bar *b, int c) {
    struct search *const s = &b->search;
    struct list *const l = &b->list;
    const enum subs_curses_key ret = list_search_input(s, l, c);
    if(ret == KEY_HANDLED) {
        render_border(l, s, b->flags, b->order);
        list_refresh(l);
    }
    return ret;
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
    const i64 id = b->list.ids[b->list.i];
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(b->s->db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        return false;
    bool ret = false;
    struct buffer str = {0};
    if(sqlite3_bind_int64(stmt, 1, id) != SQLITE_OK)
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

static void show_ordering_menu(struct subs_bar *b) {
    enum { n = ARRAY_SIZE(MENU_OPTIONS) };
    struct menu *const m = &b->menu;
    subs_menu_init(
        m, b->list.sub, n,
        b->flags & ORDER_DESC ? "Order by (desc.):" : "Order by:",
        MENU_OPTIONS, MENU_DESC);
    menu_set_current(m, b->order);
    menu_display(m);
    menu_refresh(m);
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
    i64 *const ids = checked_calloc((size_t)n, sizeof(*ids));
    if(!ids)
        goto err0;
    char **const lines = checked_calloc((size_t)n + 1, sizeof(*lines));
    if(!lines)
        goto err1;
    sql.n = 0;
    build_query_list(tag, type, global_flags, flags, b->order, &sql);
    if(!populate(db, sql.p, sql.n - 1, param, width - 4, ids, lines))
        goto err2;
    if(!list_init(
        &b->list, NULL, n, ids, lines, b->x, b->y, width, LINES - b->y
    ))
        goto err2;
    free(sql.p);
    b->width = width;
    render_border(&b->list, &b->search, b->flags, b->order);
    list_refresh(&b->list);
    return true;
err2:
    for(char **p = lines; *p; ++p)
        free(*p);
    free(lines);
err1:
    free(ids);
err0:
    free(sql.p);
    return false;
}

void subs_bar_destroy(struct subs_bar *b) {
    if(b->menu.m)
        menu_destroy(&b->menu);
    list_destroy(&b->list);
    free(b->search.b.p);
}

bool subs_bar_leave(struct window *w) {
    struct subs_bar *b = w->data;
    if(!b->menu.m)
        list_set_active(&((struct subs_bar*)w->data)->list, false);
    return true;
}

bool subs_bar_enter(struct window *w) {
    struct subs_bar *const b = w->data;
    if(!b->menu.m)
        list_set_active(&b->list, true);
    curs_set(0);
    return true;
}

void subs_bar_redraw(struct window *w) {
    struct list *l = &((struct subs_bar*)w->data)->list;
    list_redraw(l);
    list_refresh(l);
}

enum subs_curses_key subs_bar_input(struct window *w, int c) {
    struct subs_bar *b = w->data;
    if(b->menu.m)
        return input_menu(b, c);
    if(search_is_input_active(&b->search))
        return input_search(b, c);
    struct subs_curses *const s = b->s;
    struct videos *const v = b->videos;
    switch(c) {
    case '\n':
        videos_set_sub(v, (int)b->list.ids[b->list.i]);
        return videos_reload(v)
            && change_window(s, VIDEOS_IDX);
    case '/':
        search_reset(&b->search);
        render_border(&b->list, &b->search, b->flags, b->order);
        list_refresh(&b->list);
        break;
    case 'O':
        show_ordering_menu(b);
        return true;
    case 'R':
        b->flags ^= ORDER_DESC;
        if(!subs_bar_reload(b))
            return false;
        break;
    case 'n':
        if(!search_is_empty(&b->search))
            list_search_next(&b->search, &b->list);
        break;
    case 'r': if(!reload_item(b)) return false; break;
    default: return list_input(&b->list, c);
    }
    list_refresh(&b->list);
    return true;
}
