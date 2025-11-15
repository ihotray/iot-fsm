local cjson = require 'cjson.safe'
local M = {}

---state: number, state of agent
---id: string, id of agent
---return: string, request
---state example:
---1: agent is connected
---2: agent get system info..., if timeout, back to state 1
---3: agent get system info success
---4: agent get system status..., if timeout, back to state 3
---5: agent get system status success


local handle_state_2 = function ()
    -- connected, get system info
    os.execute("sleep 30")  -- simulate processing time
    return cjson.encode({code = 0, next_state = 3, next_state_delay = 10, next_state_timeout = 0})
end

M.handle_state = function (state)
    -- handle response with state and agent id

    --- return cjson.encode({code = 0, next_state = 1, next_state_delay = 6, next_state_timeout = 0})
    --- 成功，进入指定状态，并在该状态停留6秒， next_state_timeout = 0表示不设置超时
    --- return cjson.encode({code = -1})  --- 失败，回到前一状态

    if state == 2 then
        -- connected, get system info
        return handle_state_2()
    end

    return cjson.encode({code = 0, next_state_timeout = 10})  --- 成功，默认进入下一状态，状态超时10秒后进入下一状态
end

return M