/*
 * Git: https://github.com/crypery
 * Author: https://crypery.com
 * License: GNU AGPL v3 (Affero GPL)
 */

/* ecma.h
 *
 * Shared support library used by orfo:
 *
 *   - a UTF-8 codec (decode/encode, code point helpers)
 *   - a small ECMAScript-like regex engine (compile, search, global replace
 *     with literal or callback replacements)
 *   - a dynamic string buffer and small string helpers
 *
 * Pure C, no external dependencies.
 */

#ifndef ECMA_H
#define ECMA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* UTF-8 codec                                                         */
/* ------------------------------------------------------------------ */

/* Decode UTF-8 bytes s[0..len) into code points.  `out` may be NULL to just
   count.  Invalid sequences decode to U+FFFD.  *out_count receives the number
   of code points. */
int ecma_utf8_decode(const unsigned char *s, size_t len, int *out, size_t *out_count);

/* Encode one code point into buf (must hold 4 bytes).  *nbytes receives the
   number of bytes written (1..4). */
void ecma_utf8_encode(int cp, char *buf, int *nbytes);

/* Decode into a malloc'd array of code points.  Caller frees with free().
   Returns NULL on allocation failure. */
int *ecma_utf8_decode_alloc(const unsigned char *s, size_t len, size_t *n);

/* Encode code points into a malloc'd NUL-terminated UTF-8 string.
   Caller frees with free().  Returns NULL on allocation failure. */
char *ecma_utf8_encode_alloc(const int *cp, int n);

/* Byte length of the UTF-8 sequence starting at p. */
size_t ecma_utf8_seq_len(const char *p);

/* Decode one code point at p (must be a valid sequence). */
int ecma_utf8_cp_at(const char *p);

/* ------------------------------------------------------------------ */
/* Code point classification and case folding                          */
/* ------------------------------------------------------------------ */

int ecma_is_space_cp(int cp);
int ecma_is_word_cp(int cp);
int ecma_is_digit_cp(int cp);

/* Case fold (lower) for ASCII and Cyrillic. */
int ecma_cp_fold(int cp);

/* Case upper for ASCII and Cyrillic. */
int ecma_cp_upper(int cp);

/* ------------------------------------------------------------------ */
/* Dynamic string buffer (byte oriented)                               */
/* ------------------------------------------------------------------ */

typedef struct EcmaSBuf {
    char *data;
    size_t len;
    size_t cap;
} EcmaSBuf;

void  ecma_sbuf_init(EcmaSBuf *b);
void  ecma_sbuf_free(EcmaSBuf *b);
int   ecma_sbuf_append(EcmaSBuf *b, const char *s, size_t n);
int   ecma_sbuf_puts(EcmaSBuf *b, const char *s);
int   ecma_sbuf_putc(EcmaSBuf *b, char c);

/* Take ownership of the buffer (NUL-terminated).  The SBuf is reset. */
char *ecma_sbuf_take(EcmaSBuf *b);

/* Duplicate a C string (malloc'd).  Returns NULL on allocation failure. */
char *ecma_strdup(const char *s);

/* ------------------------------------------------------------------ */
/* ECMAScript-like regex engine                                        */
/* ------------------------------------------------------------------ */

/* Supported syntax: literals,., ^, $, \b, \s \S \w \d, character classes
   [..] with ranges and negation, groups (..), (?:..), alternation |,
   quantifiers * +? {n} {n,} {n,m}, lookarounds (?=..) (?!..) (?<=..) (?<!..),
   escapes \n \r \t \xHH \uHHHH.  Matching is over code points; \w includes
   Cyrillic and Greek letters. */

typedef struct EcmaNode EcmaNode;

typedef struct EcmaRegex {
    EcmaNode *root;
    int ngroup;
    int ci;
} EcmaRegex;

/* Compile a pattern given as code points pat[0..len).  `ci` enables
   case-insensitive matching.  On failure returns 0 and fills errbuf (if
   given).  On success the caller must release the tree with
   ecma_regex_free(). */
int ecma_regex_compile(const int *pat, int len, EcmaRegex *rx, int ci,
                       char *errbuf, size_t errbufsz);

/* Release the parse tree of a compiled regex.  NULL is safe. */
void ecma_regex_free(EcmaRegex *rx);

/* Search for the first match starting at or after `start`.  `caps` must hold
    2 * rx->ngroup ints.  Returns 1 on match and sets *mstart / *mend. */
int ecma_regex_search(const EcmaRegex *rx, const int *t, int len, int start,
                      int *mstart, int *mend, int *caps);

/* A match: whole-match span plus capture group spans. */
typedef struct EcmaMatch {
    const int *t;      /* text code points */
    int len;
    int mstart, mend;  /* whole-match span */
    int *caps;         /* 2*ngroup capture spans */
    int ngroup;
} EcmaMatch;

/* UTF-8 text of group g (0 = whole match).  Returns malloc'd UTF-8. */
char *ecma_match_text(const EcmaMatch *m, int g);

/* Replacement callback: returns a malloc'd UTF-8 string for the match. */
typedef char *(*EcmaReplFn)(const EcmaMatch *m, void *user);

/* Global replace of rx in t[0..len) with the literal UTF-8 replacement
   repl[0..repl_len) ($1..$9 and $& are expanded).  Returns a malloc'd code
   point array (caller frees with free()); *out_len receives its length. */
int *ecma_replace_all(const EcmaRegex *rx, const int *t, int len,
                      const char *repl, size_t repl_len, int *out_len);

/* Global replace of rx in t[0..len); each match is replaced by the UTF-8
   string returned by fn (malloc'd, freed here). */
int *ecma_replace_all_cb(const EcmaRegex *rx, const int *t, int len,
                         EcmaReplFn fn, void *user, int *out_len);

/* Evaluate a replacement template with $1..$9 / $& references.
   Returns malloc'd UTF-8. */
char *ecma_eval_repl(const EcmaMatch *m, const char *repl);

/* Replacement callback that expands a fixed template (EcmaReplStr in user). */
typedef struct { const char *repl; } EcmaReplStr;
char *ecma_replstr_fn(const EcmaMatch *m, void *user);

/* Convenience: decode `t`, compile `pattern` (ci = case-insensitive),
   globally replace with fn, re-encode.  Returns malloc'd UTF-8; on failure
   returns a copy of `t`. */
char *ecma_sub(const char *t, const char *pattern, int ci, EcmaReplFn fn, void *user);

#ifdef __cplusplus
}
#endif

#endif /* ECMA_H */
