# IoT-FSM

一个基于有限状态机（FSM）的IoT设备状态管理框架，使用C语言开发并支持Lua脚本扩展。

## 项目概述

IoT-FSM是一个轻量级的有限状态机实现，专门为IoT设备设计。它提供了一个灵活的状态管理框架，允许设备在不同状态之间进行转换，并通过Lua脚本来定义状态处理逻辑。每个状态的处理都在独立的子进程中执行，支持超时控制和进程管理。

## 特性

- **有限状态机引擎**: 完整的FSM实现，支持灵活的状态转换
- **Lua脚本支持**: 通过Lua脚本定义状态处理逻辑，提供高度的灵活性
- **进程隔离**: 每个状态处理在独立的子进程中执行，保证稳定性
- **超时控制**: 支持状态处理超时管理，超时自动终止进程
- **延迟控制**: 支持状态停留延迟，控制状态转换节奏
- **信号处理**: 支持SIGINT、SIGTERM和SIGUSR1信号
- **状态重置**: 通过SIGUSR1信号可以重置状态机到初始状态
- **进程间通信**: 使用管道实现父子进程通信
- **JSON数据交换**: 使用cJSON库进行数据序列化
- **可配置调试**: 支持5个级别的日志输出

## 系统架构

项目由以下核心组件组成：

### 核心文件

- `fsm.h` - 头文件，定义了FSM的数据结构和接口
- `fsm.c` - FSM引擎的核心实现，包含进程管理和状态转换逻辑
- `main.c` - 主程序入口和命令行参数解析
- `iot-fsm.lua` - 默认的Lua状态处理脚本示例

### 核心数据结构

```c
struct fsm_option {
    const char *callback_lua;    // Lua回调脚本路径
    int debug_level;             // 调试级别 (0-4)
    int state_begin;             // 起始状态
    int state_end;               // 结束状态
};

struct fsm_private {
    struct fsm_config cfg;       // 配置信息
    struct mg_mgr mgr;           // Mongoose事件管理器
    int state;                   // 当前状态
    int state_delay;             // 状态延迟计数器（秒）
    int state_timeout;           // 状态超时时间（秒）
    int state_stay;              // 当前状态已停留时间（秒）
    pid_t pid;                   // 子进程ID
    int fd_read;                 // 管道读端文件描述符
    int fd_write;                // 管道写端文件描述符
};
```

## 编译和构建

### 依赖项

项目依赖以下库：
- `liblua` - Lua解释器库
- `liot-base-nossl` - IoT基础库（不含SSL）
- `liot-json` - IoT JSON处理库（cJSON）

### 编译

使用提供的Makefile进行编译：

```bash
make
```

或者手动编译：

```bash
gcc main.c fsm.c -llua -liot-base-nossl -liot-json -Wall -Werror -o iot-fsm
```

### 清理

```bash
make clean
```

## 使用方法

### 命令行选项

```bash
./iot-fsm [OPTIONS]

选项：
  -x PATH     指定IoT-FSM回调Lua脚本路径（默认：/www/iot/handler/iot-fsm.lua）
  -b n        设置状态机起始状态（默认：1，最小值：1）
  -e n        设置状态机结束状态（默认：5，最小值：起始状态+1）
  -v LEVEL    设置调试级别，0-4（默认：2 - MG_LL_INFO）
```

### 运行示例

```bash
# 使用默认配置运行
./iot-fsm

# 自定义Lua脚本和状态范围
./iot-fsm -x ./my-handler.lua -b 1 -e 10 -v 3

# 重置状态机到初始状态
kill -USR1 `pidof iot-fsm`

# 优雅退出
kill -TERM `pidof iot-fsm`
```

## Lua脚本开发

### 脚本接口

Lua脚本需要实现`handle_state`函数：

```lua
local M = {}

M.handle_state = function(state)
    -- 状态处理逻辑
    -- 参数：
    --   state: 当前状态编号（整数）
    
    -- 返回JSON格式字符串：
    -- {
    --   "code": 0,                    -- 处理结果码（0=成功，非0=失败）
    --   "next_state": 3,              -- 下一个状态（可选，不指定则为当前状态+1）
    --   "next_state_delay": 10,       -- 进入下一状态前的延迟秒数（可选，默认0）
    --   "next_state_timeout": 30      -- 下一状态处理的超时秒数（可选，默认0表示不超时）
    -- }
    
    return cjson.encode({code = 0})
end

return M
```

### 返回参数详解

- **code**: 状态处理结果
  - `0`: 处理成功，状态机将转换到下一个状态
  - 非`0`: 处理失败，状态机保持当前状态，下次继续尝试

- **next_state**: 下一个目标状态（可选）
  - 如果指定，状态机转换到指定状态
  - 如果不指定，状态机转换到 `当前状态 + 1`

- **next_state_delay**: 状态延迟（可选，单位：秒）
  - 进入下一状态后，延迟指定秒数再开始处理
  - 默认值为 0，表示立即处理
  - 用于控制状态转换节奏

- **next_state_timeout**: 状态超时（可选，单位：秒）
  - 下一状态处理的最大允许时间
  - 超时后子进程将被强制终止（SIGKILL）
  - 默认值为 0，表示不设置超时限制
  - 用于防止状态处理挂起

### 状态转换规则

1. **成功处理** (`code = 0`)：
   - 状态机转换到 `next_state`（如指定）或 `当前状态 + 1`
   - 应用 `next_state_delay` 延迟
   - 设置 `next_state_timeout` 超时

2. **处理失败** (`code ≠ 0`)：
   - 保持当前状态不变
   - 清除延迟和超时设置
   - 等待下一个周期重新尝试

3. **到达结束状态**：
   - 当 `state == state_end` 时，状态机停止转换
   - 程序继续运行但不再处理状态

### 示例：基本状态流程

```lua
local cjson = require 'cjson.safe'
local M = {}

M.handle_state = function(state)
    if state == 1 then
        -- 状态1: 初始化
        print("Initializing...")
        return cjson.encode({
            code = 0,
            next_state = 2,
            next_state_delay = 5,      -- 5秒后进入状态2
            next_state_timeout = 30    -- 状态2最多执行30秒
        })
    elseif state == 2 then
        -- 状态2: 获取系统信息
        os.execute("sleep 10")  -- 模拟耗时操作
        return cjson.encode({
            code = 0,
            next_state_timeout = 60    -- 下一状态超时60秒
        })
    elseif state == 3 then
        -- 状态3: 处理数据
        local success = process_data()
        if success then
            return cjson.encode({code = 0})  -- 成功，进入下一状态
        else
            return cjson.encode({code = -1})  -- 失败，保持当前状态重试
        end
    end
    
    -- 默认：成功进入下一状态，10秒超时
    return cjson.encode({code = 0, next_state_timeout = 10})
end

return M
```

### 示例状态流程图

```
状态1 (初始化)
  ↓ [延迟5秒]
状态2 (获取系统信息，超时30秒)
  ↓ [如果超时，子进程被终止，重试状态2]
状态3 (系统信息获取成功)
  ↓
状态4 (获取系统状态，超时60秒)
  ↓ [如果超时，子进程被终止，重试状态4]
状态5 (完成 - 结束状态)
```

## 工作原理

### 初始化阶段

1. 解析命令行参数
2. 设置信号处理器（SIGINT, SIGTERM, SIGUSR1）
3. 初始化事件管理器
4. 创建1秒周期的定时器
5. 设置初始状态为 `state_begin`

### 运行阶段

每秒定时器触发，执行以下流程：

1. **状态范围检查**：验证当前状态在 `[state_begin, state_end]` 范围内
2. **结束状态检查**：如果 `state == state_end`，跳过处理
3. **延迟检查**：如果 `state_delay > 0`，递减计数器并等待
4. **子进程管理**：
   - 如果子进程存在，检查其状态：
     - 仍在运行：递增 `state_stay`，检查是否超时
     - 超时：发送SIGKILL终止子进程
     - 已退出：读取结果，更新状态
   - 如果无子进程，fork新进程处理当前状态

### 状态处理流程（子进程）

1. **创建管道**：用于父子进程通信
2. **Fork子进程**：
   - 子进程：
     - 设置独立进程组（便于批量终止）
     - 创建Lua虚拟机
     - 加载并执行Lua脚本
     - 调用 `handle_state(state)` 函数
     - 解析JSON返回结果
     - 通过管道发送结果给父进程
     - 退出子进程
   - 父进程：
     - 保存子进程PID
     - 保持管道读端打开
     - 继续事件循环

3. **结果处理**（父进程）：
   - 等待子进程退出（非阻塞）
   - 读取管道中的结果数据
   - 根据返回的 `code` 决定是否更新状态
   - 应用 `next_state`、`next_state_delay`、`next_state_timeout`

### 超时机制

- 每秒检查子进程状态
- `state_stay` 递增记录已停留时间
- 当 `state_stay >= state_timeout` 时：
  - 发送 `SIGKILL` 到进程组（`-pid`）
  - 强制终止子进程及其所有子进程
  - 等待进程完全退出
  - 清理资源（关闭管道）

## 信号处理

- **SIGINT/SIGTERM**: 设置退出标志，优雅退出程序
- **SIGUSR1**: 重置状态机
  - 状态设置为 `state_begin`
  - 清除 `state_delay`
  - 记录日志

## 调试

程序支持5个调试级别（0-4），通过`-v`参数设置：

- **0 (MG_LL_NONE)**: 无日志输出
- **1 (MG_LL_ERROR)**: 仅错误信息
- **2 (MG_LL_INFO)**: 信息级别（默认）
- **3 (MG_LL_DEBUG)**: 调试信息
- **4 (MG_LL_VERBOSE)**: 详细调试信息

日志示例：
```
INFO: state 2 starting process
INFO: state: 2, ret: {"code":0,"next_state":3,"next_state_delay":10,"next_state_timeout":0}
INFO: state update, state: 2 -> 3, next state delay: 10, next state timeout: 0
INFO: state process 12345 exited with code 0 from state 2, next state 3, next state delay 10, next state timeout 0
```

## 技术特点

### 进程隔离

- 每个状态处理在独立子进程中执行
- 子进程崩溃不影响主进程
- 通过进程组管理，可以清理所有子进程

### 资源管理

- 使用管道进行进程间通信
- 子进程退出后自动清理资源
- Lua虚拟机在每次状态处理后关闭

### 健壮性

- 超时保护机制防止状态挂起
- 信号处理确保优雅退出
- 错误处理和日志记录完善

## 使用场景

- IoT设备初始化流程管理
- 设备固件升级流程控制
- 网络连接和认证状态管理
- 数据采集和上报流程编排
- 设备状态监控和故障恢复

## 许可证

本项目使用GNU General Public License v3.0许可证。详见[LICENSE](LICENSE)文件。

## 贡献

欢迎提交问题报告和功能请求。如需贡献代码，请先fork项目并创建pull request。

## 项目信息

- **项目仓库**: https://github.com/ihotray/iot-fsm.git
- **版本**: 基于IoT-SDK
- **维护者**: ihotray
