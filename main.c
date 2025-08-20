#include <libgen.h>
#include <iot/mongoose.h>
#include "fsm.h"

#define HANDLER_LUA_SCRIPT "/www/iot/handler/iot-fsm.lua"

static void usage(char *prog) {
    fprintf(stderr,
            "IoT-SDK v.%s\n"
            "Usage: %s OPTIONS\n"
            "  -x PATH     - iot-fsm callback lua script, default: '%s'\n"
            "  -b n        - agent state begin, default: %d, min: 1\n"
            "  -e n        - agent state end, default: %d, min: begin+1\n"
            "  -t n        - agent state timeout, default: %d seconds\n"
            "  -v LEVEL    - debug level, from 0 to 4, default: %d\n"
            "\n"
            "  kill -USR1 `pidof %s` reset state[to init state]\n"
            "\n",
            MG_VERSION, prog, HANDLER_LUA_SCRIPT, 1, 5, 15, MG_LL_INFO, basename(prog));

    exit(EXIT_FAILURE);
}

static void parse_args(int argc, char *argv[], struct fsm_option *opts) {
    // Parse command-line flags
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-x") == 0) {
            opts->callback_lua = argv[++i];
        } else if (strcmp(argv[i], "-b") == 0) {
            opts->state_begin = atoi(argv[++i]);
            if (opts->state_begin < 1) {
                opts->state_begin = 1;
            }
        } else if (strcmp(argv[i], "-e") == 0) {
            opts->state_end = atoi(argv[++i]);
            if (opts->state_end < 2) {
                opts->state_end = 2;
            }
        } else if (strcmp(argv[i], "-t") == 0) {
            opts->state_timeout = atoi(argv[++i]);
            if (opts->state_timeout < 6) {
                opts->state_timeout = 6;
            }
        } else if (strcmp(argv[i], "-v") == 0) {
            opts->debug_level = atoi(argv[++i]);
        } else {
            usage(argv[0]);
        }
    }

    if (opts->state_begin >= opts->state_end) {
        usage(argv[0]);
    }
}

int main(int argc, char *argv[]) {

    struct fsm_option opts = {
        .callback_lua = HANDLER_LUA_SCRIPT,
        .debug_level = MG_LL_INFO,
        .state_begin = 1,
        .state_end = 5,
        .state_timeout = 15
    };

    parse_args(argc, argv, &opts);

    MG_INFO(("IoT-SDK version     : v%s", MG_VERSION));
    MG_INFO(("callback lua        : %s", opts.callback_lua));
    MG_INFO(("agent state begin   : %d", opts.state_begin));
    MG_INFO(("agent state end     : %d", opts.state_end));
    MG_INFO(("agent state timeout : %d", opts.state_timeout));

    fsm_main(&opts);
    return 0;
}