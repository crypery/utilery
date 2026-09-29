/*
 * Git: https://github.com/crypery
 * Author: https://crypery.com
 * License: GNU AGPL v3 (Affero GPL)
 */

/*
 * Settings module.
 *
 * Loads and parses a JSON configuration file into an opaque Sett handle and
 * exposes typed getters (int, bool, string) that fall back to default values.
 * Strings returned by sett_get_string() are heap-allocated and owned by the
 * caller, who must release them with free().
 */

#ifndef SETT_H
#define SETT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

// Maximum allowed config file size in bytes
#define SETT_MAX_FILE (1024 * 1024)

// Opaque settings handle wrapping a parsed JSON tree
typedef struct Sett Sett;

/* Initialize settings from a JSON config file.
 * @param path Path to the config file.
 * @return A Sett handle, or NULL on critical failure. */
Sett *sett_init(const char *path);

/* Free all resources associated with a Sett handle.
 * @param s Settings handle, or NULL. */
void sett_free(Sett *s);

/* Get an integer config value.
 * @param s Settings handle.
 * @param key Config key name.
 * @param def Default value if missing or of the wrong type.
 * @return The stored value or the default. */
int sett_get_int(Sett *s, const char *key, int def);

/* Get a boolean config value.
 * @param s Settings handle.
 * @param key Config key name.
 * @param def Default value if missing or of the wrong type.
 * @return The stored value or the default. */
bool sett_get_bool(Sett *s, const char *key, bool def);

/* Get a string config value; caller must free() the result.
 * @param s Settings handle.
 * @param key Config key name.
 * @param def Default value if missing, empty, or of the wrong type.
 * @return A heap-allocated copy of the value. */
char *sett_get_string(Sett *s, const char *key, const char *def);

#ifdef __cplusplus
}
#endif

#endif // SETT_H
