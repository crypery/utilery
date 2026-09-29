/*
 * Git: https://github.com/crypery
 * Author: https://crypery.com
 * License: GNU AGPL v3 (Affero GPL)
 */

/*
 * JSON parser module.
 *
 * Parses JSON text into a tree of JsonValue nodes. Supports objects, arrays,
 * strings, numbers, booleans, and null. Obtain the root with json_parse(),
 * release it with json_free(), and access nested values via json_get() and
 * the typed accessors below.
 */

#ifndef JSON_H
#define JSON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

// JSON value type discriminator
typedef enum {
    JSON_NULL,     // null literal
    JSON_BOOL,     // true / false
    JSON_NUMBER,   // double
    JSON_STRING,   // NUL-terminated char*
    JSON_ARRAY,    // Ordered children
    JSON_OBJECT    // Key -> child mapping
} JsonType;

typedef struct JsonValue JsonValue;

// A single JSON value node in the parse tree
struct JsonValue {
    JsonType type;              // Value type discriminator
    const char *string_val;     // JSON_STRING payload
    double number_val;          // JSON_NUMBER payload
    int bool_val;               // JSON_BOOL payload
    JsonValue **children;       // JSON_ARRAY / JSON_OBJECT children
    char **keys;                // JSON_OBJECT member names
    int count;                  // Number of children / members
};

// Wrapper holding a single root JsonValue
typedef struct {
    JsonValue root;
} JsonObject;

/* Initialize an empty JsonObject.
 * @param obj Object to initialize. */
void json_init(JsonObject *obj);

/* Destroy a JsonObject and free all nested values.
 * @param obj Object to destroy. */
void json_destroy(JsonObject *obj);

/* Parse JSON text into a new JsonValue tree.
 * @param json JSON text buffer.
 * @param len Buffer length.
 * @return Allocated root value, or NULL on parse failure. */
JsonValue *json_parse_buf(const char *json, size_t len);

/* Free a JsonValue tree.
 * @param val Root value to free. */
void json_free_value(JsonValue *val);

/* Get the child value of an object by key.
 * @param obj Object value.
 * @param key Member name.
 * @return Matching child value, or NULL. */
JsonValue *json_get(const JsonValue *obj, const char *key);

/* Check whether an object has a given key.
 * @param obj Object value.
 * @param key Member name.
 * @return 1 if present, 0 otherwise. */
int json_has(const JsonValue *obj, const char *key);

/* Type checks.
 * @param v Value to test.
 * @return 1 if the value is of the given type, 0 otherwise. */
int json_is_string(const JsonValue *v);
int json_is_number(const JsonValue *v);
int json_is_bool(const JsonValue *v);
int json_is_null(const JsonValue *v);
int json_is_array(const JsonValue *v);
int json_is_object(const JsonValue *v);

/* Get a string value.
 * @param v Value to read.
 * @return The string, or NULL if not a string. */
const char *json_string(const JsonValue *v);

/* Get a numeric value.
 * @param v Value to read.
 * @return The number, or 0.0 if not a number. */
double json_number(const JsonValue *v);

/* Get a boolean value.
 * @param v Value to read.
 * @return 1 or 0, or 0 if not a boolean. */
int json_bool(const JsonValue *v);

/* Get the length of an array.
 * @param v Array value.
 * @return Number of elements, or 0 if not an array. */
int json_array_len(const JsonValue *v);

/* Get an array element by index.
 * @param v Array value.
 * @param idx Element index.
 * @return The element, or NULL if out of range. */
const JsonValue *json_array_item(const JsonValue *v, int idx);

/* Get the member count of an object.
 * @param obj Object value.
 * @return Number of members, or 0 if not an object. */
int json_object_len(const JsonValue *obj);

#ifdef __cplusplus
}
#endif

#endif // JSON_H
