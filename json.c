/*
 * Git: https://github.com/crypery
 * Author: https://crypery.com
 * License: GNU AGPL v3 (Affero GPL)
 */

/*
 * JSON parser module.
 *
 * Lightweight JSON parser supporting objects, arrays, strings, numbers,
 * booleans, and null. Parses JSON text into a tree of JsonValue nodes and
 * provides typed accessors. Parsed strings are heap-allocated; call
 * json_free() to release the whole tree.
 *
 * Example:
 *   JsonValue *root = json_parse(text, len);
 *   JsonValue *v = json_get(root, "port");
 *   int port = json_is_number(v)? (int)json_number(v): 443;
 *   json_free(root);
 */
#include "json.h"
#include "ecma.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Zero-initialize a JsonValue as JSON_NULL
static void json_init_val(JsonValue *v) {
    memset(v, 0, sizeof(JsonValue));
    v->type = JSON_NULL;
}

// Initialize empty JSON object
void json_init(JsonObject *obj) {
    memset(obj, 0, sizeof(JsonObject));
    json_init_val(&obj->root);
}

static void json_free_val(JsonValue *v);

// Free JSON object and all children
void json_destroy(JsonObject *obj) {
    json_free_val(&obj->root);
    memset(obj, 0, sizeof(JsonObject));
}

// Recursively free a JsonValue and its children
static void json_free_val(JsonValue *v) {
    if (!v) return;
    if (v->type == JSON_STRING) { free((void *)v->string_val); }
    else if (v->type == JSON_OBJECT || v->type == JSON_ARRAY) {
        if (v->children) {
            for (int i = 0; i < v->count; i++) {
                json_free_val(v->children[i]);
                free(v->children[i]);
            }
            free(v->children);
        }
        if (v->keys) {
            for (int i = 0; i < v->count; i++) free(v->keys[i]);
            free(v->keys);
        }
    }
}

// Free a single JsonValue node
void json_free_value(JsonValue *val) { json_free_val(val); }

// Internal JSON text cursor
typedef struct {
    const char *data;
    size_t len;
    size_t pos;
} Parser;

// Skip whitespace characters
static void skip_ws(Parser *p) {
    while (p->pos < p->len && isspace((unsigned char)p->data[p->pos])) p->pos++;
}

static int parse_value(Parser *p, JsonValue *out);

// Growable string pool owned by the parser
typedef struct { char **arr; int count; int cap; } StrPool;

// Initialize an empty string pool
static void pool_init(StrPool *p) { p->arr = NULL; p->count = 0; p->cap = 0; }
// Free all strings in the pool
static void pool_destroy(StrPool *p) {
    if (p->arr) { for (int i = 0; i < p->count; i++) free(p->arr[i]); free(p->arr); }
}

// Add a copy of s to the pool; returns the stored pointer or NULL
static char *pool_add(StrPool *pool, const char *s, size_t len) {
    char *copy = (char *)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len); copy[len] = '\0';

    if (pool->count >= pool->cap) {
        int nc = pool->cap == 0 ? 64 : pool->cap * 2;
        char **na = (char **)realloc(pool->arr, (size_t)nc * sizeof(char *));
        if (!na) { free(copy); return NULL; }
        memset((char *)na + pool->cap * sizeof(char *), 0, (size_t)(nc - pool->cap) * sizeof(char *));
        pool->arr = na; pool->cap = nc;
    }
    pool->arr[pool->count++] = copy;
    return copy;
}

// Parse a JSON string and store the decoded value in the pool
static int parse_json_string(Parser *p, StrPool *pool) {
    if (p->pos >= p->len || p->data[p->pos] != '"') return 0;
    p->pos++;

    size_t start = p->pos;
    while (p->pos < p->len && p->data[p->pos] != '"') {
        if (p->data[p->pos] == '\\') p->pos++;
        p->pos++;
    }
    if (p->pos >= p->len) return 0;

    size_t len = p->pos - start;
    char *raw = (char *)malloc(len + 1);
    memcpy(raw, p->data + start, len); raw[len] = '\0';
    p->pos++;

    size_t r = 0, w = 0;
    while (r < len) {
        if (raw[r] == '\\' && r + 1 < len) {
            r++;
            switch (raw[r]) {
                case '"':  raw[w++] = '"'; break;
                case '\\': raw[w++] = '\\'; break;
                case '/':  raw[w++] = '/'; break;
                case 'b':  raw[w++] = '\b'; break;
                case 'f':  raw[w++] = '\f'; break;
                case 'n':  raw[w++] = '\n'; break;
                case 'r':  raw[w++] = '\r'; break;
                case 't':  raw[w++] = '\t'; break;
                case 'u': {
                    int cp = 0;
                    for (int i = 1; i <= 4 && (r + i) < len; i++) {
                        cp <<= 4;
                        char c = raw[r+i];
                        if (c>='0'&&c<='9') cp += c-'0';
                        else if (c>='a'&&c<='f') cp += 10+c-'a';
                        else if (c>='A'&&c<='F') cp += 10+c-'A';
                        else { free(raw); return 0; }
                    }
                    r += 4;
                    char ubuf[4]; int un;
                    ecma_utf8_encode(cp, ubuf, &un);
                    for (int k = 0; k < un; k++) raw[w++] = ubuf[k];
                    break;
                }
                default: raw[w++] = raw[r]; break;
            }
        } else { raw[w++] = raw[r]; }
        r++;
    }

    return pool_add(pool, raw, w) != NULL ? 1 : 0;
}

// Parse one JSON value (null/bool/number/string/array/object)
static int parse_value(Parser *p, JsonValue *out) {
    skip_ws(p);
    if (p->pos >= p->len) return 0;

    json_init_val(out);

    char c = p->data[p->pos];

    if (strncmp(p->data + p->pos, "null", 4) == 0) {
        p->pos += 4; out->type = JSON_NULL; return 1;
    }
    if (strncmp(p->data + p->pos, "true", 4) == 0) {
        p->pos += 4; out->type = JSON_BOOL; out->bool_val = 1; return 1;
    }
    if (strncmp(p->data + p->pos, "false", 5) == 0) {
        p->pos += 5; out->type = JSON_BOOL; out->bool_val = 0; return 1;
    }

    if (c == '-' || isdigit((unsigned char)c)) {
        size_t start = p->pos;
        if (c == '-') p->pos++;
        while (p->pos < p->len && isdigit((unsigned char)p->data[p->pos])) p->pos++;
        if (p->pos < p->len && p->data[p->pos] == '.') {
            p->pos++;
            while (p->pos < p->len && isdigit((unsigned char)p->data[p->pos])) p->pos++;
        }
        if (p->pos < p->len && (p->data[p->pos] == 'e' || p->data[p->pos] == 'E')) {
            p->pos++;
            if (p->pos < p->len && (p->data[p->pos] == '+' || p->data[p->pos] == '-')) p->pos++;
            while (p->pos < p->len && isdigit((unsigned char)p->data[p->pos])) p->pos++;
        }
        size_t len = p->pos - start;
        out->type = JSON_NUMBER;
        char num_buf[64];
        if (len < sizeof(num_buf)) {
            memcpy(num_buf, p->data + start, len); num_buf[len] = '\0';
            out->number_val = atof(num_buf);
        } else {
            char *nb = (char *)malloc(len + 1);
            memcpy(nb, p->data + start, len); nb[len] = '\0';
            out->number_val = atof(nb); free(nb);
        }
        return 1;
    }

    if (c == '"') {
        StrPool pool; pool_init(&pool);
        int ok = parse_json_string(p, &pool);
        if (ok && pool.count > 0) {
            out->type = JSON_STRING;
            out->string_val = pool.arr[0];
            pool.arr[0] = NULL; // detach so pool_destroy does not free it
        }
        pool_destroy(&pool);
        return ok ? 1 : 0;
    }

    if (c == '[') {
        p->pos++;
        skip_ws(p);
        if (p->pos < p->len && p->data[p->pos] == ']') { p->pos++; out->type = JSON_ARRAY; return 1; }

        int cap = 8, cnt = 0;
        JsonValue **children = (JsonValue **)malloc((size_t)cap * sizeof(JsonValue *));
        char **keys_arr = (char **)malloc((size_t)cap * sizeof(char *));

        while (1) {
            skip_ws(p);
            if (p->pos >= p->len) { free(children); free(keys_arr); return 0; }
            if (p->data[p->pos] == ']') { p->pos++; break; }
            if (p->data[p->pos] == ',') { p->pos++; continue; }

            if (cnt >= cap) {
                cap *= 2;
                children = (JsonValue **)realloc(children, (size_t)cap * sizeof(JsonValue *));
                keys_arr = (char **)realloc(keys_arr, (size_t)cap * sizeof(char *));
            }

            JsonValue *child = (JsonValue *)malloc(sizeof(JsonValue));
            if (!parse_value(p, child)) { free(child); free(children); free(keys_arr); return 0; }
            children[cnt] = child; keys_arr[cnt] = NULL; cnt++;
        }

        out->type = JSON_ARRAY;
        out->children = children;
        out->keys = keys_arr;
        out->count = cnt;
        return 1;
    }

    if (c == '{') {
        p->pos++;
        skip_ws(p);
        if (p->pos < p->len && p->data[p->pos] == '}') { p->pos++; out->type = JSON_OBJECT; return 1; }

        int cap = 8, cnt = 0;
        JsonValue **children = (JsonValue **)malloc((size_t)cap * sizeof(JsonValue *));
        char **keys_arr = (char **)malloc((size_t)cap * sizeof(char *));

        while (1) {
            skip_ws(p);
            if (p->pos >= p->len) { free(children); free(keys_arr); return 0; }
            if (p->data[p->pos] == '}') { p->pos++; break; }

            StrPool kpool; pool_init(&kpool);
            if (!parse_json_string(p, &kpool)) { free(children); free(keys_arr); return 0; }

            skip_ws(p);
            if (p->pos >= p->len || p->data[p->pos] != ':') {
                pool_destroy(&kpool); free(children); free(keys_arr); return 0;
            }
            p->pos++;

            if (cnt >= cap) {
                cap *= 2;
                children = (JsonValue **)realloc(children, (size_t)cap * sizeof(JsonValue *));
                keys_arr = (char **)realloc(keys_arr, (size_t)cap * sizeof(char *));
            }

            keys_arr[cnt] = kpool.arr[0];
            kpool.arr = NULL;
            pool_destroy(&kpool);
            JsonValue *child = (JsonValue *)malloc(sizeof(JsonValue));
            if (!parse_value(p, child)) { free(child); free(children); free(keys_arr); return 0; }
            children[cnt] = child; cnt++;

            skip_ws(p);
            if (p->pos < p->len && p->data[p->pos] == ',') { p->pos++; continue; }
        }

        out->type = JSON_OBJECT;
        out->children = children;
        out->keys = keys_arr;
        out->count = cnt;
        return 1;
    }

    return 0;
}

// Parse JSON text into value tree
JsonValue *json_parse_buf(const char *json, size_t len) {
    Parser p; p.data = json; p.len = len; p.pos = 0;
    JsonValue *val = (JsonValue *)malloc(sizeof(JsonValue));
    if (!val) return NULL;
    if (!parse_value(&p, val)) { free(val); return NULL; }
    return val;
}

// Get child value by key from object
struct JsonValue *json_get(const struct JsonValue *o, const char *key) {
    if (o->type != JSON_OBJECT) return NULL;
    for (int i = 0; i < o->count; i++) {
        if (o->keys[i] && strcmp(o->keys[i], key) == 0) return o->children[i];
    }
    return NULL;
}

// Check if key exists in object
int json_has(const JsonValue *o, const char *key) { return json_get(o, key) != NULL; }

// Check if value is a string
int json_is_string(const JsonValue *v)    { return v && v->type == JSON_STRING; }
// Check if value is a number
int json_is_number(const JsonValue *v)    { return v && v->type == JSON_NUMBER; }
// Check if value is a boolean
int json_is_bool(const JsonValue *v)      { return v && v->type == JSON_BOOL; }
// Check if value is null
int json_is_null(const JsonValue *v)      { return v && v->type == JSON_NULL; }
// Check if value is an array
int json_is_array(const JsonValue *v)     { return v && v->type == JSON_ARRAY; }
// Check if value is an object
int json_is_object(const JsonValue *v)    { return v && v->type == JSON_OBJECT; }

// Get string value
const char *json_string(const JsonValue *v) { return json_is_string(v) ? v->string_val : NULL; }
// Get number value
double json_number(const JsonValue *v)      { return json_is_number(v) ? v->number_val : 0.0; }
// Get boolean value
int json_bool(const JsonValue *v)           { return json_is_bool(v) ? v->bool_val : 0; }
// Get array length
int json_array_len(const JsonValue *v)      { return json_is_array(v) ? v->count : 0; }
// Get array element by index
const JsonValue *json_array_item(const JsonValue *v, int idx) {
    return (json_is_array(v) && idx >= 0 && idx < v->count) ? v->children[idx] : NULL;
}
// Get object member count
int json_object_len(const JsonValue *o)     { return json_is_object(o) ? o->count : 0; }
