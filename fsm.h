#ifndef __FSM_H__
#define __FSM_H__

#include <iot/mongoose.h>

struct fsm_option {
    const char *callback_lua;
    int debug_level;
    int state_begin; //state, start from
    int state_end;  //state, end to
    int state_timeout; //state, timeout, fallbcak
};

struct fsm_config {
    struct fsm_option *opts;
};

struct fsm_private {
    struct fsm_config cfg;
    struct mg_mgr mgr;
    int state;  //state, current
    uint64_t state_stay; //time of stay in state
    struct mg_str data;
};

int fsm_main(void *user_options);

#endif