/*
 * Git: https://github.com/crypery
 * Author: https://crypery.com
 * License: GNU AGPL v3 (Affero GPL)
 */

/* ecma.c
 *
 * Shared support library used by orfo:
 *
 *   - a UTF-8 codec (decode/encode, code point helpers)
 *   - a small ECMAScript-like regex engine (compile, search, global replace
 *     with literal or callback replacements)
 *   - a dynamic string buffer and small string helpers
 *
 * Pure C, no external dependencies.
 *
 * Example:
 *   // Global regex replace with a callback (returns malloc'd UTF-8)
 *   char *out = ecma_sub(text, "\\b\\d+\\b", 0, my_repl_fn, NULL);
 *   free(out);
 *
 *   // Dynamic string buffer
 *   EcmaSBuf b; ecma_sbuf_init(&b);
 *   ecma_sbuf_puts(&b, "hello ");
 *   ecma_sbuf_putc(&b, 'w');
 *   char *s = ecma_sbuf_take(&b);   // malloc'd, NUL-terminated
 *   free(s);
 *
 *   // UTF-8 codec
 *   size_t n;
 *   int *cp = ecma_utf8_decode_alloc((const unsigned char *)text,
 *                                    strlen(text), &n);
 *   char *utf8 = ecma_utf8_encode_alloc(cp, (int)n);
 *   free(cp);
 *   free(utf8);
 */

#include "ecma.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* Dynamic string buffer (UTF-8 aware but byte oriented)               */
/* ================================================================== */

void ecma_sbuf_init(EcmaSBuf *b) { b->data = NULL; b->len = 0; b->cap = 0; }
void ecma_sbuf_free(EcmaSBuf *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }

static int sbuf_grow(EcmaSBuf *b, size_t need) {
    if (b->len + need + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap : 64;
        while (ncap < b->len + need + 1) ncap *= 2;
        char *nd = (char *)realloc(b->data, ncap);
        if (!nd) return -1;
        b->data = nd;
        b->cap = ncap;
    }
    return 0;
}

int ecma_sbuf_append(EcmaSBuf *b, const char *s, size_t n) {
    if (sbuf_grow(b, n) != 0) return -1;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
    return 0;
}
int ecma_sbuf_puts(EcmaSBuf *b, const char *s) { return ecma_sbuf_append(b, s, strlen(s)); }
int ecma_sbuf_putc(EcmaSBuf *b, char c) { return ecma_sbuf_append(b, &c, 1); }

char *ecma_sbuf_take(EcmaSBuf *b) {
    if (!b->data) { char *e = (char *)malloc(1); if (e) e[0] = 0; return e; }
    char *d = b->data;
    b->data = NULL;
    b->len = b->cap = 0;
    return d;
}

/* Duplicate a C string (malloc'd). */
char *ecma_strdup(const char *s) {
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

/* ================================================================== */
/* UTF-8 codec                                                         */
/* ================================================================== */

int ecma_utf8_decode(const unsigned char *s, size_t len, int *out, size_t *out_count) {
    size_t n = 0;
    size_t i = 0;
    while (i < len) {
        unsigned char c = s[i];
        int cp;
        size_t need;
        if (c < 0x80) { cp = c; need = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; need = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; need = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; need = 4; }
        else { cp = 0xFFFD; need = 1; }

        if (i + need > len) { cp = 0xFFFD; need = 1; }
        else {
            int bad = 0;
            for (size_t k = 1; k < need; k++) {
                unsigned char cc = s[i + k];
                if ((cc & 0xC0) != 0x80) { bad = 1; break; }
                cp = (cp << 6) | (cc & 0x3F);
            }
            if (bad) { cp = 0xFFFD; need = 1; }
        }
        if (out) out[n] = cp;
        n++;
        i += need;
    }
    *out_count = n;
    return 0;
}

void ecma_utf8_encode(int cp, char *buf, int *nbytes) {
    if (cp < 0) cp = 0xFFFD;
    if (cp < 0x80) { buf[0] = (char)cp; *nbytes = 1; }
    else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        *nbytes = 2;
    }
    else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        *nbytes = 3;
    }
    else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        *nbytes = 4;
    }
}

int *ecma_utf8_decode_alloc(const unsigned char *s, size_t len, size_t *n) {
    int *cp = NULL;
    ecma_utf8_decode(s, len, NULL, n);
    cp = (int *)malloc((*n ? *n : 1) * sizeof(int));
    if (!cp) return NULL;
    ecma_utf8_decode(s, len, cp, n);
    return cp;
}

char *ecma_utf8_encode_alloc(const int *cp, int n) {
    size_t total = 0;
    for (int i = 0; i < n; i++) {
        int c = cp[i] < 0 ? 0xFFFD : cp[i];
        if (c < 0x80) total += 1;
        else if (c < 0x800) total += 2;
        else if (c < 0x10000) total += 3;
        else total += 4;
    }
    char *out = (char *)malloc(total + 1);
    if (!out) return NULL;
    size_t o = 0;
    char buf[4];
    int nb;
    for (int i = 0; i < n; i++) {
        ecma_utf8_encode(cp[i] < 0 ? 0xFFFD : cp[i], buf, &nb);
        for (int k = 0; k < nb; k++) out[o++] = buf[k];
    }
    out[o] = 0;
    return out;
}

/* Skip a full UTF-8 sequence at p; returns byte length. */
size_t ecma_utf8_seq_len(const char *p) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

/* Decode one code point at p (must be a valid sequence). */
int ecma_utf8_cp_at(const char *p) {
    size_t n = ecma_utf8_seq_len(p);
    if (n == 1) return (unsigned char)p[0];
    int cp;
    if (n == 2) cp = ((unsigned char)p[0] & 0x1F);
    else if (n == 3) cp = ((unsigned char)p[0] & 0x0F);
    else cp = ((unsigned char)p[0] & 0x07);
    for (size_t k = 1; k < n; k++) cp = (cp << 6) | ((unsigned char)p[k] & 0x3F);
    return cp;
}

/* ================================================================== */
/* Code point classification and case folding                          */
/* ================================================================== */

int ecma_is_space_cp(int cp) { return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\v' || cp == '\f'; }
int ecma_is_word_cp(int cp) {
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') || cp == '_') return 1;
    if (cp >= 0x0400 && cp <= 0x04FF) return 1;  /* Cyrillic */
    if (cp >= 0x0370 && cp <= 0x03FF) return 1;  /* Greek */
    return 0;
}
int ecma_is_digit_cp(int cp) { return cp >= '0' && cp <= '9'; }

/* Case fold for ASCII and Cyrillic (used by case-insensitive matching). */
int ecma_cp_fold(int cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;
    if (cp == 0x0401) return 0x0451;
    return cp;
}

int ecma_cp_upper(int cp) {
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    if (cp >= 0x0430 && cp <= 0x044F) return cp - 0x20;
    if (cp == 0x0451) return 0x0401;
    return cp;
}

/* ================================================================== */
/* ECMAScript-like regex engine                                        */
/* ================================================================== */

enum {
    NODE_CHAR, NODE_ANY, NODE_CLASS, NODE_STAR, NODE_PLUS, NODE_QUEST,
    NODE_REPEAT, NODE_SEQ, NODE_ALT, NODE_GROUP, NODE_START, NODE_END,
    NODE_BOUND, NODE_WS, NODE_WORD, NODE_DIGIT,
    NODE_LOOKAHEAD, NODE_NLOOKAHEAD, NODE_LOOKBEHIND, NODE_NLOOKBEHIND
};

typedef struct { int lo, hi; } EcmaRange;

typedef struct {
    int negate;
    EcmaRange *ranges;
    int nranges, cap;
    int has_s, has_w, has_d;
} EcmaCharClass;

struct EcmaNode {
    int type;
    int c;
    struct EcmaNode *child;
    struct EcmaNode **kids;
    int nkids, capkids;
    int idx;
    int min, max;
    EcmaCharClass *cls;
};

static EcmaNode *node_new(int type) {
    EcmaNode *n = (EcmaNode *)calloc(1, sizeof(EcmaNode));
    if (n) n->type = type;
    return n;
}

static void seq_add(EcmaNode *seq, EcmaNode *kid) {
    if (seq->nkids >= seq->capkids) {
        seq->capkids = seq->capkids ? seq->capkids * 2 : 8;
        seq->kids = (EcmaNode **)realloc(seq->kids, seq->capkids * sizeof(EcmaNode *));
    }
    seq->kids[seq->nkids++] = kid;
}

static void class_add_range(EcmaCharClass *cls, int lo, int hi) {
    if (lo > hi) { int t = lo; lo = hi; hi = t; }
    if (cls->nranges >= cls->cap) {
        cls->cap = cls->cap ? cls->cap * 2 : 8;
        cls->ranges = (EcmaRange *)realloc(cls->ranges, cls->cap * sizeof(EcmaRange));
    }
    cls->ranges[cls->nranges].lo = lo;
    cls->ranges[cls->nranges].hi = hi;
    cls->nranges++;
}

static void free_node(EcmaNode *n) {
    if (!n) return;
    if (n->type == NODE_SEQ || n->type == NODE_ALT) {
        for (int i = 0; i < n->nkids; i++) free_node(n->kids[i]);
        free(n->kids);
    } else if (n->child) free_node(n->child);
    if (n->cls) { free(n->cls->ranges); free(n->cls); }
    free(n);
}

typedef struct {
    const int *pat;
    int len, pos, ngroup, err;
    char errmsg[256];
} EcmaParser;

static int read_hex(EcmaParser *p, int digits) {
    int v = 0;
    for (int k = 0; k < digits; k++) {
        if (p->pos >= p->len) { p->err = 1; return 0; }
        int h = p->pat[p->pos++];
        if (h >= '0' && h <= '9') v = (v << 4) | (h - '0');
        else if (h >= 'a' && h <= 'f') v = (v << 4) | (h - 'a' + 10);
        else if (h >= 'A' && h <= 'F') v = (v << 4) | (h - 'A' + 10);
        else { p->err = 1; return 0; }
    }
    return v;
}

static EcmaNode *parse_atom(EcmaParser *p);
static EcmaNode *parse_expr(EcmaParser *p);

static EcmaNode *parse_seq(EcmaParser *p) {
    EcmaNode *seq = node_new(NODE_SEQ);
    if (!seq) { p->err = 1; return NULL; }
    while (p->pos < p->len) {
        int c = p->pat[p->pos];
        if (c == ')' || c == '|') break;
        EcmaNode *atom = parse_atom(p);
        if (p->err || !atom) { free_node(seq); return NULL; }
        if (p->pos < p->len) {
            int q = p->pat[p->pos];
            if (q == '*' || q == '+' || q == '?' || q == '{') {
                EcmaNode *qn = NULL;
                if (q == '*') { qn = node_new(NODE_STAR); qn->min = 0; qn->max = INT_MAX; p->pos++; }
                else if (q == '+') { qn = node_new(NODE_PLUS); qn->min = 1; qn->max = INT_MAX; p->pos++; }
                else if (q == '?') { qn = node_new(NODE_QUEST); qn->min = 0; qn->max = 1; p->pos++; }
                else {
                    p->pos++;
                    int mn = 0, mx = INT_MAX, ok = 1;
                    if (p->pos < p->len && p->pat[p->pos] >= '0' && p->pat[p->pos] <= '9') {
                        mn = 0;
                        while (p->pos < p->len && p->pat[p->pos] >= '0' && p->pat[p->pos] <= '9')
                            mn = mn * 10 + (p->pat[p->pos++] - '0');
                        if (mn > 1000000) ok = 0;
                    } else ok = 0;
                    if (ok && p->pos < p->len && p->pat[p->pos] == ',') {
                        p->pos++;
                        if (p->pos < p->len && p->pat[p->pos] == '}') { mx = INT_MAX; p->pos++; }
                        else {
                            mx = 0; int any = 0;
                            while (p->pos < p->len && p->pat[p->pos] >= '0' && p->pat[p->pos] <= '9') {
                                mx = mx * 10 + (p->pat[p->pos++] - '0'); any = 1;
                            }
                            if (!any || mx > 1000000) ok = 0;
                            if (ok && p->pos < p->len && p->pat[p->pos] == '}') p->pos++;
                            else ok = 0;
                        }
                    } else if (ok && p->pos < p->len && p->pat[p->pos] == '}') { mx = mn; p->pos++; }
                    else ok = 0;
                    if (!ok) {
                        snprintf(p->errmsg, sizeof(p->errmsg), "bad repeat '{...}'");
                        p->err = 1; free_node(atom); free_node(seq); return NULL;
                    }
                    qn = node_new(NODE_REPEAT);
                    qn->min = mn; qn->max = mx;
                }
                qn->child = atom;
                seq_add(seq, qn);
            } else seq_add(seq, atom);
        } else seq_add(seq, atom);
    }
    return seq;
}

static EcmaNode *parse_escape(EcmaParser *p) {
    p->pos++;
    if (p->pos >= p->len) { snprintf(p->errmsg, sizeof(p->errmsg), "trailing backslash"); p->err = 1; return NULL; }
    int c = p->pat[p->pos++];
    EcmaNode *n;
    switch (c) {
        case 'n': n = node_new(NODE_CHAR); n->c = '\n'; return n;
        case 'r': n = node_new(NODE_CHAR); n->c = '\r'; return n;
        case 't': n = node_new(NODE_CHAR); n->c = '\t'; return n;
        case 's': return node_new(NODE_WS);
        case 'S': {
            EcmaCharClass *cls = (EcmaCharClass *)calloc(1, sizeof(EcmaCharClass));
            if (!cls) { p->err = 1; return NULL; }
            cls->negate = 1; cls->has_s = 1;
            n = node_new(NODE_CLASS); n->cls = cls; return n;
        }
        case 'w': return node_new(NODE_WORD);
        case 'd': return node_new(NODE_DIGIT);
        case 'b': return node_new(NODE_BOUND);
        case 'x': {
            int v = read_hex(p, 2);
            if (p->err) return NULL;
            n = node_new(NODE_CHAR); n->c = v; return n;
        }
        case 'u': {
            int v = read_hex(p, 4);
            if (p->err) return NULL;
            n = node_new(NODE_CHAR); n->c = v; return n;
        }
        default:
            n = node_new(NODE_CHAR); n->c = c; return n;
    }
}

static EcmaNode *parse_class(EcmaParser *p) {
    p->pos++;
    EcmaCharClass *cls = (EcmaCharClass *)calloc(1, sizeof(EcmaCharClass));
    if (!cls) { p->err = 1; return NULL; }
    if (p->pos < p->len && p->pat[p->pos] == '^') { cls->negate = 1; p->pos++; }
    int closed = 0;
    while (p->pos < p->len) {
        int lo = 0, hi = 0, special = 0;
        if (p->pat[p->pos] == ']') { closed = 1; p->pos++; break; }
        if (p->pat[p->pos] == '\\') {
            p->pos++;
            if (p->pos >= p->len) break;
            int e = p->pat[p->pos++];
            if (e == 's') { cls->has_s = 1; special = 1; }
            else if (e == 'S') { cls->negate = 1; cls->has_s = 1; special = 1; }
            else if (e == 'w') { cls->has_w = 1; special = 1; }
            else if (e == 'd') { cls->has_d = 1; special = 1; }
            else if (e == 'x') { lo = hi = read_hex(p, 2); if (p->err) break; }
            else if (e == 'u') { lo = hi = read_hex(p, 4); if (p->err) break; }
            else lo = hi = e;
        } else { lo = p->pat[p->pos++]; hi = lo; }
        if (!special && p->pos + 1 < p->len && p->pat[p->pos] == '-' && p->pat[p->pos + 1] != ']') {
            p->pos++;
            int nx = p->pat[p->pos];
            if (nx == '\\') {
                p->pos++;
                if (p->pos < p->len) {
                    int e2 = p->pat[p->pos++];
                    if (e2 == 'x') hi = read_hex(p, 2);
                    else if (e2 == 'u') hi = read_hex(p, 4);
                    else hi = e2;
                }
            } else hi = p->pat[p->pos++];
        }
        if (!special) class_add_range(cls, lo, hi);
    }
    if (!closed) {
        snprintf(p->errmsg, sizeof(p->errmsg), "unterminated character class");
        p->err = 1; free(cls->ranges); free(cls); return NULL;
    }
    EcmaNode *n = node_new(NODE_CLASS);
    n->cls = cls;
    return n;
}

static EcmaNode *parse_atom(EcmaParser *p) {
    if (p->pos >= p->len) { snprintf(p->errmsg, sizeof(p->errmsg), "unexpected end of pattern"); p->err = 1; return NULL; }
    int c = p->pat[p->pos];
    switch (c) {
        case '(': {
            p->pos++;
            if (p->pos + 1 < p->len && p->pat[p->pos] == '?') {
                int c2 = p->pat[p->pos + 1];
                if (c2 == '=' || c2 == '!') {
                    p->pos += 2;
                    EcmaNode *inner = parse_expr(p);
                    if (p->err || !inner) return NULL;
                    if (p->pos >= p->len || p->pat[p->pos] != ')') {
                        snprintf(p->errmsg, sizeof(p->errmsg), "missing ')'");
                        p->err = 1; free_node(inner); return NULL;
                    }
                    p->pos++;
                    EcmaNode *g = node_new(c2 == '=' ? NODE_LOOKAHEAD : NODE_NLOOKAHEAD);
                    g->child = inner;
                    return g;
                }
                if (c2 == '<' && p->pos + 2 < p->len) {
                    int c3 = p->pat[p->pos + 2];
                    if (c3 == '=' || c3 == '!') {
                        p->pos += 3;
                        EcmaNode *inner = parse_expr(p);
                        if (p->err || !inner) return NULL;
                        if (p->pos >= p->len || p->pat[p->pos] != ')') {
                            snprintf(p->errmsg, sizeof(p->errmsg), "missing ')'");
                            p->err = 1; free_node(inner); return NULL;
                        }
                        p->pos++;
                        EcmaNode *g = node_new(c3 == '=' ? NODE_LOOKBEHIND : NODE_NLOOKBEHIND);
                        g->child = inner;
                        return g;
                    }
                }
                if (c2 == ':') {
                    p->pos += 2;
                    EcmaNode *inner = parse_expr(p);
                    if (p->err || !inner) return NULL;
                    if (p->pos >= p->len || p->pat[p->pos] != ')') {
                        snprintf(p->errmsg, sizeof(p->errmsg), "missing ')'");
                        p->err = 1; free_node(inner); return NULL;
                    }
                    p->pos++;
                    EcmaNode *g = node_new(NODE_GROUP);
                    g->idx = -1; g->child = inner;
                    return g;
                }
            }
            int gidx = p->ngroup++;
            EcmaNode *inner = parse_expr(p);
            if (p->err || !inner) return NULL;
            if (p->pos >= p->len || p->pat[p->pos] != ')') {
                snprintf(p->errmsg, sizeof(p->errmsg), "missing ')'");
                p->err = 1; free_node(inner); return NULL;
            }
            p->pos++;
            EcmaNode *g = node_new(NODE_GROUP);
            g->idx = gidx; g->child = inner;
            return g;
        }
        case ')': case '|': case '*': case '+': case '?': case '{':
            snprintf(p->errmsg, sizeof(p->errmsg), "unexpected '%c'", (char)c);
            p->err = 1; return NULL;
        case '[': return parse_class(p);
        case '.': p->pos++; return node_new(NODE_ANY);
        case '^': p->pos++; return node_new(NODE_START);
        case '$': p->pos++; return node_new(NODE_END);
        case '\\': return parse_escape(p);
        default: {
            EcmaNode *n = node_new(NODE_CHAR);
            n->c = c; p->pos++; return n;
        }
    }
}

static EcmaNode *parse_expr(EcmaParser *p) {
    EcmaNode *seq = parse_seq(p);
    if (p->err || !seq) return NULL;
    if (p->pos < p->len && p->pat[p->pos] == '|') {
        EcmaNode *alt = node_new(NODE_ALT);
        if (!alt) { free_node(seq); p->err = 1; return NULL; }
        seq_add(alt, seq);
        while (p->pos < p->len && p->pat[p->pos] == '|') {
            p->pos++;
            EcmaNode *branch = parse_seq(p);
            if (p->err || !branch) { free_node(alt); return NULL; }
            seq_add(alt, branch);
        }
        return alt;
    }
    return seq;
}

int ecma_regex_compile(const int *pat, int len, EcmaRegex *rx, int ci, char *errbuf, size_t errbufsz) {
    EcmaParser p;
    memset(&p, 0, sizeof(p));
    p.pat = pat; p.len = len;
    EcmaNode *expr = parse_expr(&p);
    if (p.err) {
        if (errbuf && errbufsz) snprintf(errbuf, errbufsz, "%s", p.errmsg);
        if (expr) free_node(expr);
        return 0;
    }
    if (p.pos < p.len) {
        if (errbuf && errbufsz) snprintf(errbuf, errbufsz, "unexpected trailing characters");
        if (expr) free_node(expr);
        return 0;
    }
    EcmaNode *root = node_new(NODE_SEQ);
    if (!root) { if (expr) free_node(expr); return 0; }
    seq_add(root, expr);
    rx->root = root;
    rx->ngroup = p.ngroup;
    rx->ci = ci;
    return 1;
}

void ecma_regex_free(EcmaRegex *rx) {
    if (!rx) return;
    free_node(rx->root);
    rx->root = NULL;
}

static int class_match(const EcmaCharClass *cls, int cp, int ci) {
    int m = 0;
    for (int i = 0; i < cls->nranges; i++)
        if (cp >= cls->ranges[i].lo && cp <= cls->ranges[i].hi) m = 1;
    if (cls->has_s && ecma_is_space_cp(cp)) m = 1;
    if (cls->has_w && ecma_is_word_cp(cp)) m = 1;
    if (cls->has_d && ecma_is_digit_cp(cp)) m = 1;
    if (!m && ci) {
        int f = ecma_cp_fold(cp);
        if (f != cp) {
            for (int i = 0; i < cls->nranges; i++)
                if (f >= cls->ranges[i].lo && f <= cls->ranges[i].hi) m = 1;
            if (cls->has_s && ecma_is_space_cp(f)) m = 1;
            if (cls->has_w && ecma_is_word_cp(f)) m = 1;
            if (cls->has_d && ecma_is_digit_cp(f)) m = 1;
        }
    }
    if (cls->negate) m = !m;
    return m;
}

static int mseq(const EcmaNode *seq, int ni, const int *t, int len, int pos, int *caps, int *end, int ci);

static int mnode(const EcmaNode *n, const int *t, int len, int pos, int *caps, int *end, int ci) {
    switch (n->type) {
        case NODE_CHAR: {
            if (pos < len) {
                int m = (t[pos] == n->c) || (ci && ecma_cp_fold(t[pos]) == ecma_cp_fold(n->c));
                if (m) { *end = pos + 1; return 1; }
            }
            return 0;
        }
        case NODE_ANY:  if (pos < len && t[pos] != '\n' && t[pos] != '\r') { *end = pos + 1; return 1; } return 0;
        case NODE_CLASS: if (pos < len && class_match(n->cls, t[pos], ci)) { *end = pos + 1; return 1; } return 0;
        case NODE_WS:   if (pos < len && ecma_is_space_cp(t[pos])) { *end = pos + 1; return 1; } return 0;
        case NODE_WORD: if (pos < len && ecma_is_word_cp(t[pos])) { *end = pos + 1; return 1; } return 0;
        case NODE_DIGIT: if (pos < len && ecma_is_digit_cp(t[pos])) { *end = pos + 1; return 1; } return 0;
        case NODE_START: if (pos == 0) { *end = 0; return 1; } return 0;
        case NODE_END:   if (pos == len) { *end = len; return 1; } return 0;
        case NODE_BOUND: {
            int before = (pos > 0 && ecma_is_word_cp(t[pos - 1])) ? 1 : 0;
            int after = (pos < len && ecma_is_word_cp(t[pos])) ? 1 : 0;
            if (before != after) { *end = pos; return 1; }
            return 0;
        }
        case NODE_LOOKAHEAD: case NODE_NLOOKAHEAD: {
            int e;
            int *tc = (int *)malloc(256 * sizeof(int));
            if (!tc) return 0;
            for (int i = 0; i < 256; i++) tc[i] = -1;
            int found = mnode(n->child, t, len, pos, tc, &e, ci);
            free(tc);
            if (n->type == NODE_LOOKAHEAD) { if (found) { *end = pos; return 1; } return 0; }
            if (!found) { *end = pos; return 1; }
            return 0;
        }
        case NODE_LOOKBEHIND: case NODE_NLOOKBEHIND: {
            int found = 0;
            int *tc = (int *)malloc(256 * sizeof(int));
            if (!tc) return 0;
            for (int s = 0; s <= pos && !found; s++) {
                for (int i = 0; i < 256; i++) tc[i] = -1;
                int e;
                if (mnode(n->child, t, len, s, tc, &e, ci) && e == pos) found = 1;
            }
            free(tc);
            if (n->type == NODE_LOOKBEHIND) { if (found) { *end = pos; return 1; } return 0; }
            if (!found) { *end = pos; return 1; }
            return 0;
        }
        case NODE_SEQ: return mseq(n, 0, t, len, pos, caps, end, ci);
        case NODE_ALT:
            for (int i = 0; i < n->nkids; i++) {
                int e;
                if (mseq(n->kids[i], 0, t, len, pos, caps, &e, ci)) { *end = e; return 1; }
            }
            return 0;
        case NODE_GROUP: {
            int start = pos;
            if (n->idx >= 0) { caps[2 * n->idx] = start; caps[2 * n->idx + 1] = -1; }
            int e;
            if (mnode(n->child, t, len, pos, caps, &e, ci)) { if (n->idx >= 0) caps[2 * n->idx + 1] = e; *end = e; return 1; }
            return 0;
        }
        case NODE_STAR: case NODE_PLUS: case NODE_QUEST: case NODE_REPEAT: {
            int mn = 0, mx = INT_MAX;
            switch (n->type) {
                case NODE_PLUS: mn = 1; break;
                case NODE_QUEST: mx = 1; break;
                case NODE_REPEAT: mn = n->min; mx = n->max; break;
                default: break;
            }
            int p = pos, cnt = 0;
            while (cnt < mx) {
                int e;
                if (!mnode(n->child, t, len, p, caps, &e, ci)) break;
                if (e == p) break;
                p = e; cnt++;
            }
            if (cnt < mn) return 0;
            *end = p;
            return 1;
        }
    }
    return 0;
}

#define Q_END_CAP 4096

static int mseq(const EcmaNode *seq, int ni, const int *t, int len, int pos, int *caps, int *end, int ci) {
    if (ni >= seq->nkids) { *end = pos; return 1; }
    const EcmaNode *n = seq->kids[ni];
    switch (n->type) {
        case NODE_STAR: case NODE_PLUS: case NODE_QUEST: case NODE_REPEAT: {
            int mn = 0, mx = INT_MAX;
            switch (n->type) {
                case NODE_STAR: mn = 0; mx = INT_MAX; break;
                case NODE_PLUS: mn = 1; mx = INT_MAX; break;
                case NODE_QUEST: mn = 0; mx = 1; break;
                case NODE_REPEAT: mn = n->min; mx = n->max; break;
            }
            int cap = mx < Q_END_CAP ? mx + 1 : Q_END_CAP;
            int *ends = (int *)malloc((size_t)cap * sizeof(int));
            if (!ends) return 0;
            int cnt = 0;
            ends[cnt++] = pos;
            int p = pos;
            while (cnt < cap && cnt <= mx) {
                int e;
                if (!mnode(n->child, t, len, p, caps, &e, ci)) break;
                if (e == p) break;
                p = e; ends[cnt++] = p;
            }
            int ok = 0;
            for (int k = cnt - 1; k >= mn; k--) {
                int e;
                if (mseq(seq, ni + 1, t, len, ends[k], caps, &e, ci)) { *end = e; ok = 1; break; }
            }
            free(ends);
            return ok;
        }
        case NODE_ALT: {
            for (int i = 0; i < n->nkids; i++) {
                int e;
                if (mseq(n->kids[i], 0, t, len, pos, caps, &e, ci)) {
                    if (mseq(seq, ni + 1, t, len, e, caps, end, ci)) return 1;
                }
            }
            return 0;
        }
        case NODE_GROUP: {
            int start = pos;
            if (n->idx >= 0) { caps[2 * n->idx] = start; caps[2 * n->idx + 1] = -1; }
            if (n->child && n->child->type == NODE_ALT) {
                /* Backtracking alternation inside a group: try every branch
                   and continue the sequence only if the rest also matches. */
                for (int i = 0; i < n->child->nkids; i++) {
                    int e;
                    if (mseq(n->child->kids[i], 0, t, len, pos, caps, &e, ci)) {
                        if (n->idx >= 0) caps[2 * n->idx + 1] = e;
                        if (mseq(seq, ni + 1, t, len, e, caps, end, ci)) return 1;
                    }
                }
                return 0;
            }
            int e;
            if (mnode(n->child, t, len, pos, caps, &e, ci)) {
                if (n->idx >= 0) caps[2 * n->idx + 1] = e;
                return mseq(seq, ni + 1, t, len, e, caps, end, ci);
            }
            return 0;
        }
        default: {
            int e;
            if (!mnode(n, t, len, pos, caps, &e, ci)) return 0;
            return mseq(seq, ni + 1, t, len, e, caps, end, ci);
        }
    }
}

int ecma_regex_search(const EcmaRegex *rx, const int *t, int len, int start, int *mstart, int *mend, int *caps) {
    int ncap = rx->ngroup;
    for (int s = start; s <= len; s++) {
        for (int i = 0; i < ncap * 2; i++) caps[i] = -1;
        int e;
        if (mseq(rx->root, 0, t, len, s, caps, &e, rx->ci)) { *mstart = s; *mend = e; return 1; }
        if (s == len) break;
    }
    return 0;
}

typedef struct { int *a; int n, cap; } EcmaIBuf;

static void ibuf_push(EcmaIBuf *b, int v) {
    if (b->n >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        b->a = (int *)realloc(b->a, (size_t)b->cap * sizeof(int));
    }
    b->a[b->n++] = v;
}

/* Global replace of rx in t[0..len); replacement is UTF-8 in repl[0..repl_len). */
int *ecma_replace_all(const EcmaRegex *rx, const int *t, int len,
                      const char *repl, size_t repl_len, int *out_len) {
    int ncap = rx->ngroup;
    int *caps = (int *)malloc((size_t)(ncap * 2 + 8) * sizeof(int));
    if (!caps) { *out_len = 0; return NULL; }

    size_t rn = 0;
    int *rcp = ecma_utf8_decode_alloc((const unsigned char *)repl, repl_len, &rn);
    if (!rcp) { free(caps); *out_len = 0; return NULL; }

    EcmaIBuf out = {0};
    int pos = 0;
    while (pos < len) {
        int mstart = 0, mend = 0;
        if (ecma_regex_search(rx, t, len, pos, &mstart, &mend, caps)) {
            for (int i = pos; i < mstart; i++) ibuf_push(&out, t[i]);
            int rp = 0;
            while (rp < (int)rn) {
                int c = rcp[rp];
                if (c == '$' && rp + 1 < (int)rn) {
                    int nxt = rcp[rp + 1];
                    if (nxt >= '1' && nxt <= '9') {
                        int g = nxt - '1';
                        if (g < ncap) {
                            int s = caps[2 * g], e = caps[2 * g + 1];
                            if (s >= 0 && e >= s)
                                for (int i = s; i < e; i++) ibuf_push(&out, t[i]);
                        }
                        rp += 2;
                        continue;
                    } else if (nxt == '&') {
                        for (int i = mstart; i < mend; i++) ibuf_push(&out, t[i]);
                        rp += 2;
                        continue;
                    }
                }
                ibuf_push(&out, c);
                rp++;
            }
            if (mend > mstart) pos = mend;
            else {
                if (mstart < len) ibuf_push(&out, t[mstart]);
                pos = mstart + 1;
            }
        } else {
            for (int i = pos; i < len; i++) ibuf_push(&out, t[i]);
            pos = len;
        }
    }
    free(caps);
    free(rcp);
    *out_len = out.n;
    return out.a;
}

/* ------------------------------------------------------------------ */
/* Callback-based replacement (used by the normalisation stages)       */
/* ------------------------------------------------------------------ */

char *ecma_match_text(const EcmaMatch *m, int g) {
    int s, e;
    if (g == 0) { s = m->mstart; e = m->mend; }
    else if (g > 0 && g <= m->ngroup) { s = m->caps[2 * (g - 1)]; e = m->caps[2 * (g - 1) + 1]; }
    else return ecma_strdup("");
    /* A capture that lies outside the final match span is stale: it was set
       during a backtracked (abandoned) branch, e.g. an optional unit group
       whose trailing lookahead failed.  Treat it as "no match". */
    if (s < 0 || e < s || e > m->len || s < m->mstart || e > m->mend) return ecma_strdup("");
    return ecma_utf8_encode_alloc(m->t + s, e - s);
}

/* Global replace of rx in t[0..len); each match is replaced by the UTF-8
   string returned by fn (malloc'd, freed here). */
int *ecma_replace_all_cb(const EcmaRegex *rx, const int *t, int len,
                         EcmaReplFn fn, void *user, int *out_len) {
    int ncap = rx->ngroup;
    int *caps = (int *)malloc((size_t)(ncap * 2 + 8) * sizeof(int));
    if (!caps) { *out_len = 0; return NULL; }

    EcmaIBuf out = {0};
    int pos = 0;
    while (pos < len) {
        int mstart = 0, mend = 0;
        if (ecma_regex_search(rx, t, len, pos, &mstart, &mend, caps)) {
            for (int i = pos; i < mstart; i++) ibuf_push(&out, t[i]);
            EcmaMatch mi;
            mi.t = t; mi.len = len; mi.mstart = mstart; mi.mend = mend;
            mi.caps = caps; mi.ngroup = ncap;
            char *rep = fn(&mi, user);
            if (rep) {
                size_t rlen = strlen(rep);
                size_t rn = 0;
                int *rcp = ecma_utf8_decode_alloc((const unsigned char *)rep, rlen, &rn);
                if (rcp) {
                    for (size_t i = 0; i < rn; i++) ibuf_push(&out, rcp[i]);
                    free(rcp);
                }
                free(rep);
            }
            if (mend > mstart) pos = mend;
            else {
                if (mstart < len) ibuf_push(&out, t[mstart]);
                pos = mstart + 1;
            }
        } else {
            for (int i = pos; i < len; i++) ibuf_push(&out, t[i]);
            pos = len;
        }
    }
    free(caps);
    *out_len = out.n;
    return out.a;
}

/* Evaluate a replacement template with $1..$9 / $& references. */
char *ecma_eval_repl(const EcmaMatch *m, const char *repl) {
    EcmaSBuf b; ecma_sbuf_init(&b);
    for (const char *p = repl; *p; ) {
        if (*p == '$' && p[1]) {
            if (p[1] >= '1' && p[1] <= '9') {
                char *g = ecma_match_text(m, p[1] - '0');
                ecma_sbuf_puts(&b, g); free(g);
                p += 2; continue;
            }
            if (p[1] == '&') {
                char *g = ecma_match_text(m, 0);
                ecma_sbuf_puts(&b, g); free(g);
                p += 2; continue;
            }
        }
        ecma_sbuf_putc(&b, *p);
        p++;
    }
    return ecma_sbuf_take(&b);
}

char *ecma_replstr_fn(const EcmaMatch *m, void *user) {
    return ecma_eval_repl(m, ((EcmaReplStr *)user)->repl);
}

/* Decode `t`, apply pattern (compiled with case-insensitivity `ci`) with the
   callback fn, re-encode.  Returns malloc'd UTF-8 or NULL on failure. */
char *ecma_sub(const char *t, const char *pattern, int ci, EcmaReplFn fn, void *user) {
    if (!t) t = "";
    size_t tlen = strlen(t);
    size_t tn = 0;
    int *tcp = ecma_utf8_decode_alloc((const unsigned char *)t, tlen, &tn);
    if (!tcp) return NULL;
    size_t pn = 0;
    int *pcp = ecma_utf8_decode_alloc((const unsigned char *)pattern, strlen(pattern), &pn);
    char *result = NULL;
    if (pcp) {
        EcmaRegex rx;
        memset(&rx, 0, sizeof(rx));
        if (ecma_regex_compile(pcp, (int)pn, &rx, ci, NULL, 0)) {
            int outlen = 0;
            int *out = ecma_replace_all_cb(&rx, tcp, (int)tn, fn, user, &outlen);
            ecma_regex_free(&rx);
            if (out) {
                result = ecma_utf8_encode_alloc(out, outlen);
                free(out);
            }
        }
        free(pcp);
    }
    free(tcp);
    if (!result) result = ecma_strdup(t);
    return result;
}
