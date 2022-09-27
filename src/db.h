#ifndef SUBS_DB_H
#define SUBS_DB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include <sqlite3.h>

static bool step_stmt_once(sqlite3_stmt *stmt);
static bool write_stmt(
    sqlite3_stmt *stmt, FILE *f, void fmt(sqlite3_stmt*, FILE*));
void db_sqlite_init(void);
sqlite3 *db_init(const char *path);

static inline bool step_stmt_once(sqlite3_stmt *stmt) {
    for(;;)
        switch(sqlite3_step(stmt)) {
        default: return false;
        case SQLITE_DONE: return true;
        case SQLITE_ROW:
        case SQLITE_BUSY: break;
        }
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
