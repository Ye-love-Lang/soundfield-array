#include "json_min.h"
#include <string.h>

static void skip_ws(JsonDoc *d)
{
    while (d->cursor < d->length) {
        char c = d->text[d->cursor];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        d->cursor++;
    }
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int read_hex4(const char *s, size_t length, size_t *p, unsigned *out)
{
    unsigned value = 0;
    int h;
    for (int i = 0; i < 4; i++) {
        if (*p >= length || (h = hex_value(s[*p])) < 0) return 0;
        value = value * 16U + (unsigned)h;
        (*p)++;
    }
    *out = value;
    return 1;
}

/* Validate a single raw UTF-8 character; rejects overlong encodings,
 * surrogate scalar values and code points above U+10FFFF. */
static int skip_utf8(const char *s, size_t n, size_t *p)
{
    unsigned char c = (unsigned char)s[*p];
    size_t count;
    unsigned cp;
    unsigned min_cp;
    if (c < 0x80U) { (*p)++; return 1; }
    if (c >= 0xC2U && c <= 0xDFU) { count = 2; cp = c & 0x1FU; min_cp = 0x80U; }
    else if (c >= 0xE0U && c <= 0xEFU) { count = 3; cp = c & 0x0FU; min_cp = 0x800U; }
    else if (c >= 0xF0U && c <= 0xF4U) { count = 4; cp = c & 0x07U; min_cp = 0x10000U; }
    else return 0;
    if (*p + count > n) return 0;
    for (size_t i = 1; i < count; i++) {
        unsigned char cont = (unsigned char)s[*p + i];
        if ((cont & 0xC0U) != 0x80U) return 0;
        cp = (cp << 6) | (unsigned)(cont & 0x3FU);
    }
    if (cp < min_cp || cp > 0x10FFFFU || (cp >= 0xD800U && cp <= 0xDFFFU)) return 0;
    *p += count;
    return 1;
}

static int add_token(JsonDoc *d, JsonType kind, size_t start)
{
    if (d->count >= JSON_MAX_TOKENS) { d->error = 2; return -1; }
    int index = d->count++;
    d->tokens[index].type = kind;
    d->tokens[index].start = start;
    d->tokens[index].end = start;
    d->tokens[index].next = 0;
    return index;
}

static int parse_string(JsonDoc *d)
{
    const char *s = d->text;
    size_t p = d->cursor;
    if (p >= d->length || s[p] != '"') { d->error = 1; return -1; }
    p++;
    int id = add_token(d, JSON_STRING, p);
    if (id < 0) return -1;
    while (p < d->length) {
        unsigned char c = (unsigned char)s[p];
        if (c == '"') {
            d->tokens[id].end = p;
            d->tokens[id].next = d->count;
            d->cursor = p + 1;
            return id;
        }
        if (c < 0x20U) break;
        if (c == '\\') {
            p++;
            if (p >= d->length) break;
            c = (unsigned char)s[p++];
            if (c == '"' || c == '\\' || c == '/' || c == 'b' || c == 'f' ||
                c == 'n' || c == 'r' || c == 't') continue;
            if (c == 'u') {
                unsigned code;
                if (!read_hex4(s, d->length, &p, &code)) break;
                if (code >= 0xDC00U && code <= 0xDFFFU) break;
                if (code >= 0xD800U && code <= 0xDBFFU) {
                    unsigned lo;
                    if (p + 2 > d->length || s[p] != '\\' || s[p+1] != 'u') break;
                    p += 2;
                    if (!read_hex4(s, d->length, &p, &lo) || lo < 0xDC00U || lo > 0xDFFFU) break;
                }
                continue;
            }
            break;
        }
        if (!skip_utf8(s, d->length, &p)) break;
    }
    d->error = 1;
    return -1;
}

static int parse_value(JsonDoc *d, int depth);

static int parse_object(JsonDoc *d, int depth)
{
    int id = add_token(d, JSON_OBJECT, d->cursor++);
    if (id < 0) return -1;
    skip_ws(d);
    if (d->cursor < d->length && d->text[d->cursor] == '}') {
        d->cursor++;
    } else {
        for (;;) {
            skip_ws(d);
            if (parse_string(d) < 0) return -1;
            skip_ws(d);
            if (d->cursor >= d->length || d->text[d->cursor++] != ':') { d->error = 1; return -1; }
            if (parse_value(d, depth + 1) < 0) return -1;
            skip_ws(d);
            if (d->cursor >= d->length) { d->error = 1; return -1; }
            char c = d->text[d->cursor++];
            if (c == '}') break;
            if (c != ',') { d->error = 1; return -1; }
        }
    }
    d->tokens[id].end = d->cursor;
    d->tokens[id].next = d->count;
    return id;
}

static int parse_array(JsonDoc *d, int depth)
{
    int id = add_token(d, JSON_ARRAY, d->cursor++);
    if (id < 0) return -1;
    skip_ws(d);
    if (d->cursor < d->length && d->text[d->cursor] == ']') {
        d->cursor++;
    } else {
        for (;;) {
            if (parse_value(d, depth + 1) < 0) return -1;
            skip_ws(d);
            if (d->cursor >= d->length) { d->error = 1; return -1; }
            char c = d->text[d->cursor++];
            if (c == ']') break;
            if (c != ',') { d->error = 1; return -1; }
        }
    }
    d->tokens[id].end = d->cursor;
    d->tokens[id].next = d->count;
    return id;
}

static int parse_number(JsonDoc *d)
{
    const char *s = d->text;
    size_t p = d->cursor;
    int id = add_token(d, JSON_NUMBER, p);
    if (id < 0) return -1;
    if (p < d->length && s[p] == '-') p++;
    if (p == d->length) goto invalid;
    if (s[p] == '0') p++;
    else if (s[p] >= '1' && s[p] <= '9') {
        do { p++; } while (p < d->length && s[p] >= '0' && s[p] <= '9');
    } else goto invalid;
    if (p < d->length && s[p] == '.') {
        p++;
        if (p >= d->length || s[p] < '0' || s[p] > '9') goto invalid;
        do { p++; } while (p < d->length && s[p] >= '0' && s[p] <= '9');
    }
    if (p < d->length && (s[p] == 'e' || s[p] == 'E')) {
        p++;
        if (p < d->length && (s[p] == '-' || s[p] == '+')) p++;
        if (p >= d->length || s[p] < '0' || s[p] > '9') goto invalid;
        do { p++; } while (p < d->length && s[p] >= '0' && s[p] <= '9');
    }
    d->cursor = p;
    d->tokens[id].end = p;
    d->tokens[id].next = d->count;
    return id;
invalid:
    d->error = 1;
    return -1;
}

static int parse_value(JsonDoc *d, int depth)
{
    skip_ws(d);
    if (depth > JSON_MAX_DEPTH) { d->error = 2; return -1; }
    if (d->cursor >= d->length) { d->error = 1; return -1; }
    char c = d->text[d->cursor];
    if (c == '{') return parse_object(d, depth);
    if (c == '[') return parse_array(d, depth);
    if (c == '"') return parse_string(d);
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(d);
    const char *literal = NULL;
    JsonType type = JSON_NULL;
    size_t size = 0;
    if (c == 't') { literal = "true"; type = JSON_TRUE; size = 4; }
    else if (c == 'f') { literal = "false"; type = JSON_FALSE; size = 5; }
    else if (c == 'n') { literal = "null"; type = JSON_NULL; size = 4; }
    if (literal && d->cursor + size <= d->length &&
        memcmp(d->text + d->cursor, literal, size) == 0) {
        int id = add_token(d, type, d->cursor);
        if (id < 0) return -1;
        d->cursor += size;
        d->tokens[id].end = d->cursor;
        d->tokens[id].next = d->count;
        return id;
    }
    d->error = 1;
    return -1;
}

int json_parse(JsonDoc *d, const char *s, size_t n)
{
    memset(d, 0, sizeof(*d));
    d->text = s;
    d->length = n;
    if (parse_value(d, 0) < 0) return d->error ? d->error : 1;
    skip_ws(d);
    if (d->cursor != d->length) return 1;
    return 0;
}

int json_string_equals(const JsonDoc *d, int idx, const char *ascii)
{
    if (idx < 0 || d->tokens[idx].type != JSON_STRING) return 0;
    size_t p = d->tokens[idx].start;
    size_t end = d->tokens[idx].end;
    size_t q = 0;
    while (p < end) {
        unsigned code = (unsigned char)d->text[p++];
        if (code == '\\') {
            if (p >= end) return 0;
            char e = d->text[p++];
            switch (e) {
                case '"': code = '"'; break;
                case '\\': code = '\\'; break;
                case '/': code = '/'; break;
                case 'b': code = '\b'; break;
                case 'f': code = '\f'; break;
                case 'n': code = '\n'; break;
                case 'r': code = '\r'; break;
                case 't': code = '\t'; break;
                case 'u': if (!read_hex4(d->text, end, &p, &code)) return 0; break;
                default: return 0;
            }
        }
        if (code > 127U || ascii[q] == '\0' || (unsigned char)ascii[q++] != code) return 0;
    }
    return ascii[q] == '\0';
}

int json_find(const JsonDoc *d, int obj, const char *name)
{
    if (obj < 0 || d->tokens[obj].type != JSON_OBJECT) return -1;
    int found = -1;
    int end = d->tokens[obj].next;
    for (int key = obj + 1; key < end;) {
        int val = key + 1;
        if (val >= end) return -1;
        if (json_string_equals(d, key, name)) {
            if (found >= 0) return -2;
            found = val;
        }
        key = d->tokens[val].next;
    }
    return found;
}

int json_get_uint32(const JsonDoc *d, int idx, uint32_t *out)
{
    if (idx < 0 || d->tokens[idx].type != JSON_NUMBER) return 0;
    size_t start = d->tokens[idx].start;
    size_t end = d->tokens[idx].end;
    if (start == end) return 0;
    uint32_t v = 0;
    for (size_t i = start; i < end; i++) {
        unsigned char c = (unsigned char)d->text[i];
        if (c < '0' || c > '9') return 0;
        unsigned digit = c - '0';
        if (v > (UINT32_MAX - digit) / 10U) return 0;
        v = v * 10U + digit;
    }
    *out = v;
    return 1;
}

int json_object_empty(const JsonDoc *d, int idx)
{
    return idx >= 0 && d->tokens[idx].type == JSON_OBJECT &&
           d->tokens[idx].next == idx + 1;
}
