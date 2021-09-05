#include "log.h"
#include "subs.h"

const char *PROG_NAME = NULL;
const char *CMD_NAME = NULL;

#include <string.h>

int main(int argc, char **argv) {
    PROG_NAME = *argv;
    log_set(stderr);
    struct subs subs = {0};
    bool ret = false;
    if(!subs_init_from_argv(&subs, &argc, &argv))
        goto end;
    if(!subs_exec(&subs, argc, argv))
        goto end;
    ret = true;
end:
    ret = subs_destroy(&subs) && ret;
    return !ret;
}
