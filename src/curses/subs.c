#include "subs.h"

#include <ctype.h>
#include <stdlib.h>

#include "../buffer.h"
#include "../db.h"
#include "../util.h"

#include "curses.h"
#include "form.h"
#include "list_search.h"
#include "videos.h"

enum flags {
    UNTAGGED =   1u << 0,
    ORDER_DESC = 1u << 1,
};

enum order { SUBS_NAME, SUBS_ID, SUBS_WATCHED, SUBS_UNWATCHED };

struct tags_form {
    struct form f;
    i64 sub;
    i64 *ids;
};

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
        buffer_str_append_str(b,
            " left outer join subs_tags on subs.id == subs_tags.sub"
            " left outer join videos_tags on videos.id == videos_tags.video");
    if(untagged)
        buffer_str_append_str(b,
            " where (subs_tags.id is null and videos_tags.id is null)");
    else if(tag)
        buffer_str_append_str(b,
            " where (videos_tags.tag == ?1 or subs_tags.tag == ?1)");
    else if(type)
        buffer_str_append_str(b, " where subs.type == ?");
    if(watched) {
        if(filtered)
            buffer_str_append_str(b, " and");
        else
            buffer_str_append_str(b, " where");
        buffer_str_append_str(b, " videos.watched == 1");
    } else if(not_watched) {
        if(filtered)
            buffer_str_append_str(b, " and");
        else
            buffer_str_append_str(b, " where");
        buffer_str_append_str(b, " videos.watched == 0");
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
    buffer_str_append_str(b, " group by subs.id");
    buffer_str_append_str(b, " order by ");
    switch(order) {
    case SUBS_NAME: buffer_str_append_str(b, "subs.name"); break;
    case SUBS_ID: buffer_str_append_str(b, "subs.id"); break;
    case SUBS_WATCHED:
        buffer_str_append_str(b,
            "count(case when videos.watched then 1 else null end)");
        break;
    case SUBS_UNWATCHED:
        buffer_str_append_str(b,
            "count(case when not videos.watched then 1 else null end)");
        break;
    }
    if(flags & ORDER_DESC)
        buffer_str_append_str(b, " desc");
    switch(order) {
    case SUBS_WATCHED:
    case SUBS_UNWATCHED:
        buffer_str_append_str(b, ", subs.name");
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

static bool get_tags(
    sqlite3 *db, int id,
    size_t *n, i64 **ids, char ***tags, bool **p);

static bool show_tag_form(struct subs_bar *b) {
    const struct list *const l = &b->list;
    if(!l->n)
        return true;
    sqlite3 *const db = b->s->db;
    const int id = (int)l->ids[l->i];
    size_t n_tags;
    i64 *tag_ids;
    char **tags;
    bool *tags_set;
    if(!get_tags(db, id, &n_tags, &tag_ids, &tags, &tags_set))
        return false;
    struct form_field *const fields =
        checked_calloc(2 * n_tags, sizeof(*fields));
    if(!fields)
        goto e0;
    for(size_t i = 0; i != n_tags; ++i) {
        fields[2 * i] = (struct form_field) {
            .type = FIELD_TYPE_CHECKBOX,
            .text = tags_set[i] ? "x" : " ",
            .x = 1,
            .y = (int)i,
            .width = 1,
            .height = 1,
        };
        fields[2 * i + 1] = (struct form_field) {
            .type = FIELD_TYPE_LABEL,
            .text = tags[i],
            .x = 0,
            .y = (int)i,
            .width = /*XXX*/16,
            .height = 1,
        };
    }
    struct tags_form *const f = checked_malloc(sizeof(*f));
    if(!f)
        goto e1;
    b->tags_form = f;
    *f = (struct tags_form) {
        .sub = id,
        .ids = tag_ids,
    };
    if(!subs_form_init(&f->f, b->list.sub, "Tags:", 2 * n_tags, fields))
        goto e2;
    free(fields);
    for(size_t i = 0; i != n_tags; ++i)
        free(tags[i]);
    free(tags);
    free(tags_set);
    form_redraw(&f->f);
    form_refresh(&f->f);
    curs_set(1);
    return true;
e2:
    free(f);
e1:
    free(fields);
e0:
    free(tag_ids);
    for(size_t i = 0; i != n_tags; ++i)
        free(tags[i]);
    free(tags);
    return false;
}

static bool get_tags(
    sqlite3 *db, int id,
    size_t *p_n, i64 **p_ids, char ***p_tags, bool **p_set)
{
    const char count_sql[] = "select count(*) from tags";
    int n;
    if(!query_to_int(db, count_sql, sizeof(count_sql) - 1, NULL, &n))
        return false;
    i64 *const ids = checked_calloc((size_t)n + 1, sizeof(*ids));
    if(!ids)
        return false;
    char **const tags = checked_calloc((size_t)n + 1, sizeof(*tags));
    if(!tags)
        goto e0;
    bool *const set = checked_calloc((size_t)n + 1, sizeof(*set));
    if(!set)
        goto e1;
    const char sql[] =
        "select tags.id, tags.name, subs_tags.sub not null from tags"
        " left outer join"
            " subs_tags on tags.id == subs_tags.tag and subs_tags.sub == ?"
        " order by tags.name";
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v3(db, sql, sizeof(sql) - 1, 0, &stmt, NULL);
    if(!stmt)
        goto e2;
    if(sqlite3_bind_int(stmt, 1, id) != SQLITE_OK)
        goto e3;
    for(int i = 0;;) {
        switch(sqlite3_step(stmt)) {
        case SQLITE_BUSY: continue;
        case SQLITE_DONE: break;
        default: goto e3;
        case SQLITE_ROW:
            ids[i] = sqlite3_column_int64(stmt, 0);
            tags[i] = sprintf_alloc(
                "[ ] %s", (char*)sqlite3_column_text(stmt, 1));
            set[i] = sqlite3_column_int(stmt, 2);
            ++i;
            continue;
        }
        break;
    }
    if(sqlite3_finalize(stmt) != SQLITE_OK)
        goto e2;
    *p_n = (size_t)n;
    *p_ids = ids;
    *p_tags = tags;
    *p_set = set;
    return true;
e3:
    sqlite3_finalize(stmt);
e2:
    for(int i = 0; i != n; ++i)
        free(tags[i]);
    free(tags);
e1:
    free(set);
e0:
    free(ids);
    return false;
}

static bool set_tags(sqlite3 *db, struct tags_form *f) {
    const char sql_add[] =
        "insert or ignore into subs_tags (sub, tag) values (?, ?)";
    const char sql_rm[] =
        "delete from subs_tags where sub == ? and tag == ?";
    const i64 sub = f->sub;
    const i64 *const ids = f->ids;
    const size_t n = form_field_count(&f->f) >> 1;
    for(size_t i = 0; i != n; ++i) {
        const char *const b = form_buffer(&f->f, 2 * i);
        const bool add = b[0] != ' ';
        const char *const sql = add ? sql_add : sql_rm;
        const int len = (add ? sizeof(sql_add) : sizeof(sql_rm)) - 1;
        sqlite3_stmt *stmt = NULL;
        sqlite3_prepare_v3(db, sql, len, 0, &stmt, NULL);
        const bool ok =
            stmt
            && sqlite3_bind_int64(stmt, 1, sub) == SQLITE_OK
            && sqlite3_bind_int64(stmt, 2, ids[i]) == SQLITE_OK
            && step_stmt_once(stmt);
        if(sqlite3_finalize(stmt) != SQLITE_OK || !ok)
            return false;
    }
    return true;
}

bool destroy_tags_form(struct subs_bar *b) {
    struct tags_form *const f = b->tags_form;
    form_destroy(&f->f);
    free(f->ids);
    free(f);
    b->tags_form = NULL;
    list_redraw(&b->list);
    list_refresh(&b->list);
    curs_set(0);
    return true;
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
        return subs_bar_reload(b);
    case ESC:
    case 'q':
        menu_destroy(m);
        list_redraw(&b->list);
        list_refresh(&b->list);
        return true;
    }
}

static enum subs_curses_key input_form(struct subs_bar *b, int c) {
    struct tags_form *const f = b->tags_form;
    switch(c) {
    case ESC:
        return destroy_tags_form(b);
    case '\n': ;
        const bool ret = set_tags(b->s->db, f);
        return destroy_tags_form(b) && ret;
    }
    return form_input(&f->f, c);
}

static enum subs_curses_key input_search(struct subs_bar *b, int c, int count) {
    struct search *const s = &b->search;
    struct list *const l = &b->list;
    const enum subs_curses_key ret = list_search_input(s, l, c, count);
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
    menu_redraw(m);
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
        &b->list, NULL, n, ids, lines, b->x, b->y, width, b->height
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
    struct subs_bar *const b = (struct subs_bar*)w->data;
    struct menu *const m = &b->menu;
    struct tags_form *const f = b->tags_form;
    if(m->m) {
        menu_redraw(m);
        menu_refresh(m);
    } else if(f) {
        form_redraw(&f->f);
        form_refresh(&f->f);
    } else {
        struct list *const l = &b->list;
        list_redraw(l);
        list_refresh(l);
    }
}

void subs_bar_resize(struct subs_bar *b) {
    list_resize(&b->list, NULL, b->x, b->y, b->width, b->height);
    list_refresh(&b->list);
}

enum subs_curses_key subs_bar_input(struct window *w, int c, int count) {
    struct subs_bar *b = w->data;
    if(b->menu.m)
        return input_menu(b, c);
    if(b->tags_form)
        return input_form(b, c);
    if(search_is_input_active(&b->search))
        return input_search(b, c, count);
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
            list_search_next(&b->search, &b->list, count);
        break;
    case 'r': if(!reload_item(b)) return false; break;
    case 't':
        return show_tag_form(b);
    default: return list_input(&b->list, c, count);
    }
    list_refresh(&b->list);
    return true;
}
