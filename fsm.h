#ifndef __FSM_H__
#define __FSM_H__

#include <iot/mongoose.h>

struct fsm_option {
    const char *callback_lua;
    int debug_level;
    int state_begin; //state, start from
    int state_end;  //state, end to
};

struct fsm_config {
    struct fsm_option *opts;
};

struct fsm_private {
    struct fsm_config cfg;
    struct mg_mgr mgr;
    int state;  //state, current
    int state_delay; //time of stay in state
    int state_timeout; //state timeout in seconds
    int state_stay; // whether stay in current state
    pid_t pid;
    int fd_read;
    int fd_write;
};

int fsm_main(void *user_options);

#endif