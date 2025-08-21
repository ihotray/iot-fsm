# IoT-FSM

一个基于有限状态机（FSM）的IoT设备状态管理框架，使用C语言开发并支持Lua脚本扩展。

## 项目概述

IoT-FSM是一个轻量级的有限状态机实现，专门为IoT设备设计。它提供了一个灵活的状态管理框架，允许设备在不同状态之间进行转换，并通过Lua脚本来定义状态处理逻辑。

## 特性

- **有限状态机引擎**: 完整的FSM实现，支持状态转换，当前状态转换的输出可以作为下一次转换的输入
- **Lua脚本支持**: 通过Lua脚本定义状态处理逻辑，提供高度的灵活性
- **信号处理**: 支持SIGINT、SIGTERM和SIGUSR1信号
- **状态重置**: 通过SIGUSR1信号可以重置状态机到初始状态
- **JSON数据交换**: 使用cJSON库进行数据序列化
- **调试支持**: 可配置的调试级别

## 系统架构

项目由以下核心组件组成：

### 核心文件

- `fsm.h` - 头文件，定义了FSM的数据结构和接口
- `fsm.c` - FSM引擎的核心实现
- `main.c` - 主程序入口和命令行参数解析
- `iot-fsm.lua` - 默认的Lua状态处理脚本示例

### 数据结构

```c
struct fsm_option {
    const char *callback_lua;    // Lua回调脚本路径
    int debug_level;             // 调试级别
    int state_begin;             // 起始状态
    int state_end;               // 结束状态
};
```

## 编译和构建

### 依赖项

项目依赖以下库：
- `liblua` - Lua解释器库
- `liot-base-nossl` - IoT基础库（不含SSL）
- `liot-json` - IoT JSON处理库

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
  -v LEVEL    设置调试级别，0-4（默认：2）
```

### 运行示例

```bash
# 使用默认配置运行
./iot-fsm

# 自定义Lua脚本和状态范围
./iot-fsm -x ./my-handler.lua -b 1 -e 10 -v 3

# 重置状态机到初始状态
kill -USR1 `pidof iot-fsm`
```

## Lua脚本开发

### 脚本接口

Lua脚本需要实现`handle_state`函数：

```lua
M.handle_state = function(state, data)
    -- 状态处理逻辑
    -- 参数：
    --   state: 当前状态编号
    --   data: 输入数据字符串
    
    -- 返回JSON格式字符串：
    -- {
    --   "code": 0,              -- 处理结果码（0=成功，非0=失败）
    --   "next_state": 2,        -- 下一个状态（可选）
    --   "next_state_stay": 6,   -- 在下一状态停留的周期数（可选）
    --   "data": "output"        -- 输出数据（可选），作为下一次转换的输入 
    -- }
    
    return cjson.encode({code = 0})
end
```

### 状态转换规则

1. **成功处理** (`code = 0`)：
   - 如果指定了`next_state`，转换到指定状态
   - 否则转换到下一个状态（当前状态+1）
   - 如果指定了`next_state_stay`，在新状态停留指定的周期数

2. **处理失败** (`code ≠ 0`)：
   - 保持当前状态，等待下次处理

3. **状态停留**：
   - `next_state_stay > 0`时，状态机在该状态停留指定周期
   - 每个周期为3秒

### 示例状态流程

```
状态1: 设备连接
  ↓
状态2: 获取系统信息（如果超时，返回状态1）
  ↓
状态3: 系统信息获取成功
  ↓
状态4: 获取系统状态（如果超时，返回状态3）
  ↓
状态5: 系统状态获取成功（结束状态）
```

## 工作原理

1. **初始化阶段**：
   - 解析命令行参数
   - 设置信号处理器
   - 初始化Mongoose事件管理器
   - 创建3秒定时器

2. **运行阶段**：
   - 定时器每3秒触发一次状态更新
   - 加载并执行Lua脚本处理当前状态
   - 根据脚本返回结果决定状态转换
   - 处理信号事件（重置、退出）

3. **状态处理**：
   - 检查状态范围有效性
   - 处理状态停留逻辑
   - 调用Lua脚本的`handle_state`函数
   - 解析JSON返回结果并更新状态

## 信号处理

- **SIGINT/SIGTERM**: 优雅退出程序
- **SIGUSR1**: 重置状态机到初始状态，清空数据

## 调试

程序支持5个调试级别（0-4），可通过`-v`参数设置：
- 0: 无日志输出
- 1: 错误信息
- 2: 信息级别（默认）
- 3: 调试信息
- 4: 详细调试信息

## 许可证

本项目使用GNU General Public License v3.0许可证。详见[LICENSE](LICENSE)文件。

## 贡献

欢迎提交问题报告和功能请求。如需贡献代码，请先fork项目并创建pull request。

## 联系方式

项目仓库：https://github.com/ihotray/iot-fsm.git
