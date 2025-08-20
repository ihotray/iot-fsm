#include <lualib.h>
#include <lauxlib.h>
#include <iot/mongoose.h>
#include <iot/cJSON.h>
#include "fsm.h"

static int s_signo = 0;
static int s_sig_reset = 0;
static void signal_handler(int signo) {
    switch (signo) {
    case SIGUSR1:
        s_sig_reset = 1;
        break;
    default:
        s_signo = signo;
        break;
    }
}

struct handle_result {
    int code;
    int next_state;
    int next_state_stay;
};

static int do_state_update(struct mg_mgr *mgr) {
    struct fsm_private *priv = (struct fsm_private *)mgr->userdata;
    const char *ret = NULL;
    cJSON *root = NULL, *code = NULL, *next_state = NULL, *next_state_stay = NULL, *data = NULL;
    struct handle_result result = {
        .code = -1,
        .next_state = -1,
        .next_state_stay = -1,
    };
    lua_State *L = luaL_newstate();
    char *input = mg_mprintf("%.*s", (int)priv->data.len, priv->data.ptr);

    luaL_openlibs(L);

    if ( luaL_dofile(L, priv->cfg.opts->callback_lua) ) {
        MG_ERROR(("lua dofile %s failed, state: %d", priv->cfg.opts->callback_lua, priv->state));
        goto done;
    }

    lua_getfield(L, -1, "handle_state");
    if (!lua_isfunction(L, -1)) {
        MG_ERROR(("method handle_state is not a function"));
        goto done;
    }

    lua_pushinteger(L, priv->state);
    lua_pushstring(L, input);

    if (lua_pcall(L, 2, 1, 0)) {//three params, one return values, zero error func
        MG_ERROR(("callback failed"));
        goto done;
    }

    ret = lua_tostring(L, -1);
    if (!ret) {
        MG_ERROR(("lua call no ret"));
        goto done;
    }

    MG_INFO(("state: %d, ret: %s", priv->state, ret));
    root = cJSON_Parse(ret);
    code = cJSON_GetObjectItem(root, "code");
    if ( cJSON_IsNumber(code) ) {
       result.code = (int)cJSON_GetNumberValue(code);
    }

    if (result.code) //failed
        goto done;

    next_state = cJSON_GetObjectItem(root, "next_state");
    if ( cJSON_IsNumber(next_state) ) {
       result.next_state = (int)cJSON_GetNumberValue(next_state);
    }
    next_state_stay = cJSON_GetObjectItem(root, "next_state_stay");
    if ( cJSON_IsNumber(next_state_stay) ) {
       result.next_state_stay = (int)cJSON_GetNumberValue(next_state_stay);
    }

    if (priv->data.ptr) { //free prev state data
        free((void *)priv->data.ptr);
        priv->data.ptr = NULL;
        priv->data.len = 0;
    }

    data = cJSON_GetObjectItem(root, "data");
    if ( cJSON_IsString(data) ) { //save output
        priv->data = mg_strdup(mg_str(cJSON_GetStringValue(data)));
    }

    int n_next_state = priv->state + 1;
    if (result.next_state > 0 ) {
        n_next_state = result.next_state;
    }

    int n_next_state_stay = 0;
    if (result.next_state_stay > 0) {
        n_next_state_stay = result.next_state_stay;
    }

    MG_INFO(("state update, state: %d -> %d, next state stay: %d", priv->state, n_next_state, n_next_state_stay));
    priv->state = n_next_state; //enter next state
    priv->state_stay = n_next_state_stay;

done:
    if (L)
        lua_close(L);
    if (input)
        free(input);
    if (root)
        cJSON_Delete(root);

    return result.code;
}

static void state_update(struct mg_mgr *mgr) {

    struct fsm_private *priv = (struct fsm_private *)mgr->userdata;
    if (priv->state < priv->cfg.opts->state_begin || priv->state > priv->cfg.opts->state_end) {
        MG_ERROR(("invalid state: %d", priv->state));
        return;
    }

    if (priv->state == priv->cfg.opts->state_end) {
        MG_DEBUG(("end state, finished"));
        return;
    }

    if (priv->state_stay > 0) { // wait
        priv->state_stay = priv->state_stay - 1;
        return;
    }

    if ( do_state_update(mgr) ) {
        MG_ERROR(("do_state_update failed"));
    }
}


static void timer_state_fn(void *arg) {
    struct mg_mgr *mgr = (struct mg_mgr *)arg;

    state_update(mgr);
}

static int fsm_init(void **priv, void *opts) {

    struct fsm_private *p = NULL;
    int timer_opts = MG_TIMER_REPEAT | MG_TIMER_RUN_NOW;

    signal(SIGINT, signal_handler);   // Setup signal handlers - exist event
    signal(SIGTERM, signal_handler);  // manager loop on SIGINT and SIGTERM
    signal(SIGUSR1, signal_handler);  // SIGUSR1 for reset state

    p = calloc(1, sizeof(struct fsm_private));
    if (!p)
        return -1;

    p->cfg.opts = opts;
    mg_log_set(p->cfg.opts->debug_level);

    mg_mgr_init(&p->mgr);
    p->mgr.userdata = p;

    p->state = p->cfg.opts->state_begin;

    mg_timer_add(&p->mgr, 3000, timer_opts, timer_state_fn, &p->mgr); //3s

    *priv = p;

    return 0;

}

static void fsm_run(void *handle) {
    struct fsm_private *priv = (struct fsm_private *)handle;
    while (s_signo == 0) {
        if (s_sig_reset) {
            s_sig_reset = 0;
            priv->state = priv->cfg.opts->state_begin;
            priv->state_stay = 0;
            if (priv->data.ptr) { //free data
                free((void *)priv->data.ptr);
                priv->data.ptr = NULL;
                priv->data.len = 0;
            }
        }
        mg_mgr_poll(&priv->mgr, 1000);  // Event loop, 1000ms timeout
    }
}

static void fsm_exit(void *handle) {
    struct fsm_private *priv = (struct fsm_private *)handle;
    mg_mgr_free(&priv->mgr);
    if (priv->data.ptr) { //free data
        free((void *)priv->data.ptr);
    }
    free(handle);
}

int fsm_main(void *user_options) {

    struct fsm_option *opts = (struct fsm_option *)user_options;
    void *fsm_handle;
    int ret;

    ret = fsm_init(&fsm_handle, opts);
    if (ret)
        exit(EXIT_FAILURE);

    fsm_run(fsm_handle);

    fsm_exit(fsm_handle);

    return 0;

}