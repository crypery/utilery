/*
 * Git: https://github.com/crypery
 * Author: https://crypery.com
 * License: GNU AGPL v3 (Affero GPL)
 */

/*
 * Settings module.
 *
 * Loads and parses config.json into a JSON tree and provides typed getters
 * (int, bool, string) with default values. All functions use the sett_ prefix.
 * Strings returned by sett_get_string are heap-allocated; the caller must free
 * them. The Sett handle is opaque.
 *
 * Example:
 *   Sett *s = sett_init("config.json");
 *   int port = sett_get_int(s, "port", 443);
 *   bool ssl = sett_get_bool(s, "ssl_use", true);
 *   char *root = sett_get_string(s, "root", "www");
 *   free(root);
 *   sett_free(s);
 */

#include "sett.h"
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Cross-platform compatibility shims.
 *
 * Maps _stricmp() to strcasecmp() on POSIX systems. On Windows the CRT
 * provides both _stricmp() and strdup() natively.
 */

#ifndef COMPAT_P
#define COMPAT_P

#include <string.h>

#ifdef _WIN32
// _stricmp() and strdup() are provided by the Windows CRT
#else
#include <strings.h>
#define _stricmp strcasecmp
#endif

#endif // COMPAT_P

struct Sett {
    JsonValue *root;            // Parsed JSON tree (owned)
};

// Return 1 if s represents a truthy string ("1", "true", "yes", "on")
static int sett_str_truthy(const char *s) {
    if (!s) return 0;
    if (!strcmp(s, "1") || !_stricmp(s, "true") || !_stricmp(s, "yes") || !_stricmp(s, "on")) return 1;
    return 0;
}

// Return 1 if s represents a falsy string ("0", "false", "no", "off")
static int sett_str_falsy(const char *s) {
    if (!s) return 0;
    if (!strcmp(s, "0") || !_stricmp(s, "false") || !_stricmp(s, "no") || !_stricmp(s, "off")) return 1;
    return 0;
}

// Load and parse config file into Sett handle
Sett *sett_init(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > SETT_MAX_FILE) { fclose(f); return NULL; }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);

    JsonValue *root = json_parse_buf(buf, n);
    free(buf);
    if (!root) return NULL;

    Sett *s = (Sett *)calloc(1, sizeof(Sett));
    if (!s) { json_free_value(root); return NULL; }
    s->root = root;
    return s;
}

// Release Sett handle and JSON tree
void sett_free(Sett *s) {
    if (!s) return;
    if (s->root) json_free_value(s->root);
    free(s);
}

// Get integer config value with default
int sett_get_int(Sett *s, const char *key, int def) {
    if (!s || !s->root || !key) return def;
    JsonValue *v = json_get(s->root, key);
    if (!v) return def;
    if (json_is_number(v)) return (int)json_number(v);
    return def;
}

// Get boolean config value with default
bool sett_get_bool(Sett *s, const char *key, bool def) {
    if (!s || !s->root || !key) return def;
    JsonValue *v = json_get(s->root, key);
    if (!v) return def;
    if (json_is_bool(v)) return json_bool(v);
    return def;
}

// Get string config value with default (caller must free)
char *sett_get_string(Sett *s, const char *key, const char *def) {
    const char *val = NULL;
    if (s && s->root && key) {
        JsonValue *v = json_get(s->root, key);
        if (v && json_is_string(v)) {
            val = json_string(v);
        }
    }
    if (!val || !val[0]) {
        val = def;
    }
    return strdup(val);
}
