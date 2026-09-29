# C/CPP 基础库合集

> [English version](README.md)

> [Russian version](README_RU.md)

一套自包含的小型 C/CPP 基础库：
JSON 解析器、日志模块、配置模块和 ecma 库
（UTF-8 编解码、正则表达式、字符串缓冲区）。无外部依赖 -
仅使用 C/CPP 标准库。

## 项目特点

- 自包含：无需第三方库，仅使用 C 标准库。
- 纯 C 代码并带 `extern "C"` 封装 - 可直接包含到 CPP 项目中。
- 跨平台：Windows 和 POSIX（Linux/macOS）。
- JSON：对象、数组、字符串、数字、布尔值、null；支持
  转义序列，包括以 UTF-8 编码的 `\uXXXX`。
- ecma：UTF-8 编解码（解码/编码、码点处理）、
  ECMAScript 风格正则表达式引擎（分组、lookahead/lookbehind、
  使用模板或回调的全局替换）以及动态字符串缓冲区。
  被 json 解析器和 orfo 规范化器（用于 TTS 的俄语文本）使用。
- 日志：DEBUG/INFO/WARN/ERROR 级别，输出到控制台、文件、
  两者同时输出或禁用；时间戳；为每条
  生成的日志行提供回调（例如用于通过 WebSocket 广播）。
- 配置：读取 JSON 配置、带默认值的类型化 getter；配置文件大小限制 - 1 MB。
- 线程安全地获取本地时间（`localtime_s` / `localtime_r`）。

## 模块

- **json** - 轻量级 JSON 解析器：将文本解析为值树，并
  提供对元素的类型化访问。
- **log** - 带重要级别、控制台/文件输出以及
  每条日志行回调的日志模块。
- **sett** - 配置模块：加载 JSON 配置并返回
  （int、bool、string）值，带备用默认值。
- **ecma** - 通用辅助库：UTF-8 编解码、ECMAScript 风格
  正则表达式引擎和动态字符串缓冲区。是 json 模块的依赖，
  也是 orfo 规范化器的基础。

## 使用

### 构建

将所需的源文件与你的 `main.c` 一起编译：

- MSVC（Windows）：

  ```
  cl /W4 json.c log.c sett.c ecma.c main.c
  ```

- GCC / Clang（Windows、Linux、macOS）：

  ```
  gcc -Wall -Wextra json.c log.c sett.c ecma.c main.c -o app
  ```

### 配置文件

`config.json` 示例：

```json
{
    "port": 443,
    "ssl_use": true,
    "root": "www"
}
```

### 代码示例

```c
#include <stdlib.h>
#include "log.h"
#include "sett.h"

int main(void) {
    log_init(LOG_OUTPUT_BOTH, LOG_LEVEL_INFO, "server.log");

    Sett *s = sett_init("config.json");
    if (!s) {
        LOG_ERROR("无法加载 config.json\n");
        return 1;
    }

    int port = sett_get_int(s, "port", 443);
    bool ssl = sett_get_bool(s, "ssl_use", true);
    char *root = sett_get_string(s, "root", "www");

    LOG_INFO("port=%d ssl=%d root=%s\n", port, ssl, root);

    free(root);   // sett_get_string 返回的字符串在堆上分配
    sett_free(s);
    log_close();
    return 0;
}
```

### 工作流程

1. `log_init()` - 初始化日志：输出到哪里、最低级别、文件名。
2. `sett_init()` - 加载并解析配置文件。
3. 通过 `sett_get_int()`、`sett_get_bool()`、`sett_get_string()` 读取值。
4. 对从 `sett_get_string()` 获得的字符串调用 `free()`。
5. `sett_free()` - 释放配置。
6. `log_close()` - 关闭日志。

## 许可证

GNU AGPL v3（Affero GPL）。
