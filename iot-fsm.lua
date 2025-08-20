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


M.handle_state = function (state, data)
    -- handle response with state and agent id

    --- return cjson.encode({code = 0, next_state = 1, next_state_stay = 6, data = ""}) --- 成功，进入指定状态，并在该状态停留6*3秒
    --- return cjson.encode({code = -1})  --- 失败，回到前一状态  
    return cjson.encode({code = 0})  --- 成功，默认进入下一状态
end

return M