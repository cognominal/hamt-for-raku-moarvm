#ifndef HAMT_TYPES_INTERNAL_H
#define HAMT_TYPES_INTERNAL_H

#include <stdint.h>

// #define HAMT_REFCOUNTED 0

/* HAMT node structure */

#define TABLE(a) a->as.table.ptr

#define INDEX(a) a->as.table.index
#define VALUE(a) a->as.kv.value
#define KEY(a) a->as.kv.key

typedef struct hamt_kv {
    void *value; /* tagged pointer */
    void *key;
} hamt_kv;

typedef struct hamt_table {
            struct hamt_node *ptr; 
            uint32_t index;
} hamt_table;
typedef struct hamt_node {
    union {
        struct hamt_kv kv;
        struct hamt_table table;
    } as;
#ifdef HAMT_REFCOUNTED
    uint32_t count;
#endif    

} hamt_node;
#endif
