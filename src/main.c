#include "log.h"
#include "subs.h"

const char *PROG_NAME = NULL;
const char *CMD_NAME = NULL;

#include <string.h>

int main(int argc, char **argv) {
    PROG_NAME = *argv;
    log_set(stderr);
    struct subs subs = {0};
    if(!subs_init_from_argv(&subs, &argc, &argv))
        return 1;
    if(argc && strcmp(argv[0], "lua") == 0) {
        bool ret = subs_lua(&subs, argv[1]);
        ret = subs_destroy(&subs) && ret;
        return !ret;
    }
    bool ret = subs_exec(&subs, argc, argv);
    ret = subs_destroy(&subs) && ret;
    return !ret;
}
