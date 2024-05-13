#include "task.h"

#include "log.h"

static bool terminate(void *p) { return (void)p, false; }

static bool set_cnd(struct task_thread *t, struct task task) {
    mtx_t *const mtx = &t->mtx;
    if(mtx_lock(mtx) != thrd_success)
        return LOG_ERRNO("mtx_lock", 0), false;
    t->task = task;
    if(cnd_signal(&t->cnd) != thrd_success)
        return LOG_ERRNO("cnd_wait", 0), false;
    if(mtx_unlock(mtx) != thrd_success)
        return LOG_ERRNO("mtx_unlock", 0), false;
    return true;
}

static bool wait_cnd(struct task_thread *t, struct task *p) {
    mtx_t *const mtx = &t->mtx;
    cnd_t *const cnd = &t->cnd;
    task_f **const f = &t->task.f;
    if(mtx_lock(mtx) != thrd_success)
        return LOG_ERRNO("mtx_lock", 0), 1;
    while(!*f)
        if(cnd_wait(cnd, mtx) != thrd_success)
            return LOG_ERRNO("cnd_wait", 0), false;
    if(p)
        *p = t->task;
    t->task = (struct task){0};
    if(mtx_unlock(mtx) != thrd_success)
        return LOG_ERRNO("mtx_unlock", 0), 1;
    return true;
}

static int f(void *d) {
    struct task_thread *const t = d;
    struct task task = {0};
    for(;;) {
        if(!wait_cnd(t, &task))
            goto err;
        if(task.f == &terminate)
            return 0;
        if(!task.f(task.p))
            goto err;
    }
err:
    if(t->error_f)
        t->error_f(t->data);
    return 1;
}

bool task_thread_init(struct task_thread *t) {
    if(mtx_init(&t->mtx, mtx_plain) != thrd_success)
        return LOG_ERRNO("mtx_init", 0), false;
    if(cnd_init(&t->cnd) != thrd_success)
        return LOG_ERRNO("cnd_init", 0), false;
    if(thrd_create(&t->t, f, t) != thrd_success)
        return LOG_ERRNO("pthread_create", 0), false;
    return true;
}

bool task_thread_destroy(struct task_thread *t) {
    if(!t->t)
        return true;
    if(!set_cnd(t, (struct task){.f = &terminate}))
        return false;
    int ret;
    if(thrd_join(t->t, &ret) != thrd_success)
        return LOG_ERRNO("thrd_join", 0), false;
    cnd_destroy(&t->cnd);
    mtx_destroy(&t->mtx);
    if(ret)
        return LOG_ERR("thread exited with status: %d\n", ret), false;
    return true;
}

bool task_thread_send(struct task_thread *t, struct task task) {
    return set_cnd(t, task);
}
