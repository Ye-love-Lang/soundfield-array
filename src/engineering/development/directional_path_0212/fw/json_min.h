#ifndef JSON_MIN_H
#define JSON_MIN_H

#include <stddef.h>
#include <stdint.h>

#define JSON_MAX_TOKENS 400
#define JSON_MAX_DEPTH 12

typedef enum {
    JSON_OBJECT, JSON_ARRAY, JSON_STRING, JSON_NUMBER,
    JSON_TRUE, JSON_FALSE, JSON_NULL
} JsonType;

typedef struct {
    JsonType type;
    size_t start;      /* JSON_STRING excludes surrounding quotes */
    size_t end;
    int next;          /* first token index after this value and its descendants */
} JsonToken;

typedef struct {
    const char *text;
    size_t length;
    size_t cursor;
    JsonToken tokens[JSON_MAX_TOKENS];
    int count;
    int error;         /* 1: invalid JSON, 2: max depth/tokens exceeded */
} JsonDoc;

/* Returns 0 on success, 1 on invalid JSON, 2 on complexity limit. */
int json_parse(JsonDoc *doc, const char *text, size_t length);

/* Looks up object property: >=0 token index; -1 absent; -2 duplicate. */
int json_find(const JsonDoc *doc, int object_index, const char *name);
int json_string_equals(const JsonDoc *doc, int token_index, const char *ascii);
int json_get_uint32(const JsonDoc *doc, int token_index, uint32_t *value);
int json_object_empty(const JsonDoc *doc, int token_index);

#endif
