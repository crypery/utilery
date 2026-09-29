# A Collection of Basic C/CPP Libraries

> [Russian version](README_RU.md)

> [China version](README_CN.md)

A self-contained set of small basic C/CPP libraries:
a JSON parser, a logging module, a settings module and the ecma library
(UTF-8 codec, regular expressions, string buffer). There are no external
dependencies - only the standard C/CPP library is used.

## Project Features

- Self-contained: no third-party libraries required, only the standard C library.
- Pure C with `extern "C"` wrappers - the code can be included directly in CPP projects.
- Cross-platform: Windows and POSIX (Linux/macOS).
- JSON: objects, arrays, strings, numbers, booleans, null; support for
  escape sequences, including `\uXXXX` with UTF-8 encoding.
- ecma: UTF-8 codec (decoding/encoding, code point handling),
  an ECMAScript-style regular expression engine (groups, lookahead/lookbehind,
  global replacement with a template or a callback) and a dynamic string buffer.
  Used by the json parser and the orfo normalizer (Russian text for TTS).
- Logging: DEBUG/INFO/WARN/ERROR levels, output to console, file, both
  at the same time or disabled; timestamps; a callback for each
  formatted line (e.g. for broadcasting over WebSocket).
- Settings: reading a JSON configuration, typed getters with default
  values; configuration file size limit - 1 MB.
- Thread-safe retrieval of local time (`localtime_s` / `localtime_r`).

## Modules

- **json** - a lightweight JSON parser: parses text into a tree of values and
  provides typed access to elements.
- **log** - a logging module with importance levels, console/file output and
  a callback for each log line.
- **sett** - a settings module: loads a JSON configuration and returns values
  (int, bool, string) with fallback default values.
- **ecma** - a general-purpose support library: UTF-8 codec, an ECMAScript-style
  regular expression engine and a dynamic string buffer. A dependency of the
  json module and the foundation of the orfo normalizer.

## Usage

### Building

Compile the sources you need together with your `main.c`:

- MSVC (Windows):

  ```
  cl /W4 json.c log.c sett.c ecma.c main.c
  ```

- GCC / Clang (Windows, Linux, macOS):

  ```
  gcc -Wall -Wextra json.c log.c sett.c ecma.c main.c -o app
  ```

### Configuration File

Example `config.json`:

```json
{
    "port": 443,
    "ssl_use": true,
    "root": "www"
}
```

### Code Example

```c
#include <stdlib.h>
#include "log.h"
#include "sett.h"

int main(void) {
    log_init(LOG_OUTPUT_BOTH, LOG_LEVEL_INFO, "server.log");

    Sett *s = sett_init("config.json");
    if (!s) {
        LOG_ERROR("failed to load config.json\n");
        return 1;
    }

    int port = sett_get_int(s, "port", 443);
    bool ssl = sett_get_bool(s, "ssl_use", true);
    char *root = sett_get_string(s, "root", "www");

    LOG_INFO("port=%d ssl=%d root=%s\n", port, ssl, root);

    free(root);   // strings from sett_get_string are heap-allocated
    sett_free(s);
    log_close();
    return 0;
}
```

### Workflow

1. `log_init()` - initialize logging: where to output, minimum level, file name.
2. `sett_init()` - load and parse the configuration file.
3. Read values via `sett_get_int()`, `sett_get_bool()`, `sett_get_string()`.
4. `free()` the strings obtained from `sett_get_string()`.
5. `sett_free()` - release the settings.
6. `log_close()` - close the log.

## License

GNU AGPL v3 (Affero GPL).
