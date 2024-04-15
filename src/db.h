#ifndef SUBS_DB_H
#define SUBS_DB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include <sqlite3.h>

struct buffer;

void query_add_param_list(struct buffer *b, size_t n);
static bool step_stmt_once(sqlite3_stmt *stmt);
static int db_print_row(void *data, int n, char **cols, char **names);
static bool write_stmt(
    sqlite3_stmt *stmt, FILE *f, void fmt(sqlite3_stmt*, FILE*));
bool db_sqlite_init(void);
sqlite3 *db_init(const char *path);
int exists_query(sqlite3 *db, const char *sql, int len, const int *param);
int exists_query_stmt(sqlite3_stmt *stmt);

static inline bool step_stmt_once(sqlite3_stmt *stmt) {
    for(;;)
        switch(sqlite3_step(stmt)) {
        default: return false;
        case SQLITE_DONE: return true;
        case SQLITE_ROW:
        case SQLITE_BUSY: break;
        }
}

static inline int db_print_row(void *data, int n, char **cols, char **names) {
    (void)names;
    if(!n)
        return SQLITE_OK;
    FILE *const f = data;
    fputs(*cols, f);
    for(int i = 1; i != n; ++i)
        fputs(" ", f), fputs(cols[i], f);
    putc('\n', f);
    return SQLITE_OK;
}

static inline bool write_stmt(
    sqlite3_stmt *stmt, FILE *f, void fmt(sqlite3_stmt*, FILE*))
{
    for(;;)
        switch(sqlite3_step(stmt)) {
        case SQLITE_ROW: fmt(stmt, f); break;
        case SQLITE_BUSY: continue;
        case SQLITE_DONE: return true;
        default: return false;
        }
}

#endif
