#ifndef SUBS_CURSES_TASK_H
#define SUBS_CURSES_TASK_H

#include <stdbool.h>
#include <threads.h>

typedef bool task_f(void*);
typedef bool task_error_f(void*);
struct task { task_f *f; void *p; };

struct task_thread {
    thrd_t t;
    mtx_t mtx;
    cnd_t cnd;
    void *data;
    struct task task;
    task_error_f *error_f;
};

bool task_thread_init(struct task_thread *t);
bool task_thread_destroy(struct task_thread *t);
bool task_thread_send(struct task_thread *t, struct task task);

#endif
