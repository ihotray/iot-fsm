#include <sys/wait.h>
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
    int next_state_delay;
    int next_state_timeout;
};

static int do_state_update(struct mg_mgr *mgr) {
    struct fsm_private *priv = (struct fsm_private *)mgr->userdata;
    const char *ret = NULL;
    cJSON *root = NULL, *code = NULL, *next_state = NULL, *next_state_delay = NULL, *next_state_timeout = NULL;
    struct handle_result result = {
        .code = -1,
        .next_state = -1,
        .next_state_delay = -1,
        .next_state_timeout = -1,
    };

    lua_State *L = luaL_newstate();
    if (!L) {
        MG_ERROR(("Failed to create Lua state"));
        return result.code;
    }

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

    if (lua_pcall(L, 1, 1, 0)) {//one params, one return values, zero error func
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
    next_state_delay = cJSON_GetObjectItem(root, "next_state_delay");
    if ( cJSON_IsNumber(next_state_delay) ) {
       result.next_state_delay = (int)cJSON_GetNumberValue(next_state_delay);
    }

    next_state_timeout = cJSON_GetObjectItem(root, "next_state_timeout");
    if ( cJSON_IsNumber(next_state_timeout) ) {
       result.next_state_timeout = (int)cJSON_GetNumberValue(next_state_timeout);
    }
    int n_next_state = priv->state + 1;
    if (result.next_state > 0 ) {
        n_next_state = result.next_state;
    }

    int n_next_state_delay = 0;
    if (result.next_state_delay > 0) {
        n_next_state_delay = result.next_state_delay;
    }

    int n_next_state_timeout = 0;
    if (result.next_state_timeout > 0) {
        n_next_state_timeout = result.next_state_timeout;
    }

    MG_INFO(("state update, state: %d -> %d, next state delay: %d, next state timeout: %d", priv->state, n_next_state, n_next_state_delay, n_next_state_timeout));
    priv->state = n_next_state; //enter next state
    priv->state_delay = n_next_state_delay;
    priv->state_timeout = n_next_state_timeout;

    result.next_state = n_next_state;
    result.next_state_delay = n_next_state_delay;
    result.next_state_timeout = n_next_state_timeout;

done:
    if (L)
        lua_close(L);
    if (root)
        cJSON_Delete(root);

    write(priv->fd_write, &result, sizeof(result));
    return result.code;
}

static void fork_state_process(struct mg_mgr *mgr) {
    struct fsm_private *priv = (struct fsm_private *)mgr->userdata;
    int pipefd[2] = {-1, -1};
    pid_t pid = 0;

    // Create a pipe for inter-process communication
    if (pipe(pipefd) == -1) {
        return;
    }

    pid = fork();

    if (pid == 0) {
        // child process
        setpgid(0, 0); // set new process group

        close(pipefd[0]); // close unused read end
        priv->fd_write = pipefd[1];

        if ( do_state_update(mgr) ) {
            MG_ERROR(("do_state_update failed"));
        }
        close(priv->fd_write);
        priv->fd_write = -1;
        _exit(0);
    } else if (pid > 0) {
        // parent process
        close(pipefd[1]); // close unused write end
        priv->pid = pid;
        priv->fd_read = pipefd[0];
        priv->state_stay = 0;
    } else {
        // fork failed
        MG_ERROR(("fork failed"));
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }

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

    if (priv->state_delay > 0) { // wait
        priv->state_delay = priv->state_delay - 1;
        return;
    }

    if (priv->pid > 0) { //check prev process
        int status;
        pid_t pid = waitpid(-priv->pid, &status, WNOHANG);
        if (pid == 0) {
            // child is still running
            if (priv->state_timeout > 0) {
                if (++priv->state_stay >= priv->state_timeout) {
                    // timeout, kill child process
                    MG_INFO(("state %d process %d timeout, killing", priv->state, priv->pid));
                    kill(-priv->pid, SIGKILL);
                    waitpid(-priv->pid, &status, 0); // wait for it to exit
                    priv->pid = 0;
                    close(priv->fd_read);
                    priv->fd_read = -1;
                }
            }
        } else if (pid == priv->pid) {
            if (WIFEXITED(status)) {
                // child exited normally
                struct handle_result result = {
                    .code = -1,
                    .next_state = -1,
                    .next_state_delay = -1,
                    .next_state_timeout = -1,
                };
                read(priv->fd_read, &result, sizeof(result));
                MG_INFO(("state process %d exited with code %d from state %d, next state %d, next state delay %d, next state timeout %d", priv->pid, result.code, priv->state,
                    result.next_state, result.next_state_delay, result.next_state_timeout));
                if (!result.code) {
                    priv->state = result.next_state;
                    priv->state_delay = result.next_state_delay;
                    priv->state_timeout = result.next_state_timeout;
                }
            }
            priv->pid = 0;
            close(priv->fd_read);
            priv->fd_read = -1;
        } else {
            // error occurred
            MG_ERROR(("waitpid error for pid: %d", priv->pid));
            priv->pid = 0;
            close(priv->fd_read);
            priv->fd_read = -1;
        }
    } else {
        MG_INFO(("state %d starting process", priv->state));
        fork_state_process(mgr);
    }


}


static void timer_state_fn(void *arg) {
    struct mg_mgr *mgr = (struct mg_mgr *)arg;

    state_update(mgr);
}

static int fsm_init(void **priv, void *opts) {

    struct fsm_private *p = NULL;
    int timer_opts = MG_TIMER_REPEAT | MG_TIMER_RUN_NOW;

    signal(SIGINT, signal_handler);   // Setup signal handlers - exit event
    signal(SIGTERM, signal_handler);  // manager loop on SIGINT and SIGTERM
    signal(SIGUSR1, signal_handler);  // SIGUSR1 for reset state
    signal(SIGPIPE, SIG_IGN);  // Ignore SIGPIPE

    p = calloc(1, sizeof(struct fsm_private));
    if (!p)
        return -1;

    p->cfg.opts = opts;
    mg_log_set(p->cfg.opts->debug_level);

    mg_mgr_init(&p->mgr);
    p->mgr.userdata = p;

    p->state = p->cfg.opts->state_begin;

    mg_timer_add(&p->mgr, 1000, timer_opts, timer_state_fn, &p->mgr); //1s

    *priv = p;

    return 0;

}

static void fsm_run(void *handle) {
    struct fsm_private *priv = (struct fsm_private *)handle;
    while (s_signo == 0) {
        if (s_sig_reset) {
            s_sig_reset = 0;
            priv->state = priv->cfg.opts->state_begin;
            priv->state_delay = 0;
            if (priv->pid > 0) {
                kill(-priv->pid, SIGKILL);
                priv->pid = 0;
                close(priv->fd_read);
                priv->fd_read = -1;
            }
            MG_INFO(("reset state to %d", priv->state));
        }
        mg_mgr_poll(&priv->mgr, 1000);  // Event loop, 1000ms timeout
    }
}

static void fsm_exit(void *handle) {
    struct fsm_private *priv = (struct fsm_private *)handle;
    if (priv->pid > 0) {
        kill(-priv->pid, SIGKILL);
        priv->pid = 0;
        close(priv->fd_read);
        priv->fd_read = -1;
    }
    mg_mgr_free(&priv->mgr);
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