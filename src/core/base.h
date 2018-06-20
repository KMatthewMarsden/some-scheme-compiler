#ifndef SOMESCHEME_H
#define SOMESCHEME_H

#include <stdlib.h>
#include <stdbool.h>

#include "vec.h"

#define RUNTIME_ERROR(S) do { fprintf(stderr, "Runtime Error (%s:%d): %s\n", __func__, __LINE__, (S)); exit(1); } while (0)

DEFINE_VECTOR(struct env_elem *, env_elem_nexts)

enum closure_size {
    CLOSURE_ONE = 0,
    CLOSURE_TWO,
};

enum object_tag {
    OBJ_CLOSURE = 0,
    OBJ_ENV,
    OBJ_INT,
};

enum gc_mark_type {
    WHITE = 0,
    GREY,
    BLACK
};

struct object {
    enum object_tag tag;
    enum gc_mark_type mark;
    bool on_stack;
};


// builtin objects


// TODO: change this to a tree
// gc_env_elem_free should thus unlink the node, etc
// the lhs should be a single pointer, the rhs an array of pointers and a length
struct env_elem {
    struct object base;
    const size_t ident_id;
    struct object *val;    // shared
    struct env_elem *prev;
    struct vector_env_elem_nexts nexts;
};

struct closure {
    struct object base;
    const enum closure_size size;
    const size_t env_id;
    union {
        void (*const fn_1)(struct object *, struct env_elem *);
        void (*const fn_2)(struct object *, struct object *, struct env_elem *);
    };
    struct env_elem *env;
};

struct int_obj {
    struct object base;
    int64_t val;
};


struct env_table_entry {
    const size_t env_id;
    const size_t num_ids;
    size_t * const var_ids;
};


// get an object from the environment
struct object *env_get(size_t, struct env_elem *);

// set an existing value in the environment, returning the previous value
struct object *env_set(size_t, struct env_elem *, struct object *);

// The map of env ids to an array of var ids
extern struct env_table_entry global_env_table[];

#define NUM_ARGS(...) (sizeof((size_t []){__VA_ARGS__})/sizeof(size_t))
#define ENV_ENTRY(ID, ...) (struct env_table_entry){ID, NUM_ARGS(__VA_ARGS__), (size_t []){__VA_ARGS__}}

struct thunk {
    struct closure *closr;
    union {
        struct {
            struct object *rand;
        } one;
        struct {
            struct object *rand;
            struct object *cont;
        } two;
    };
};

void call_closure_one(struct object *, size_t, struct object *);
void call_closure_two(struct object *, size_t, struct object *, size_t, struct object *);
void halt_func(struct object *, struct env_elem *);
void scheme_start(struct thunk *);
void run_minor_gc(struct thunk *);

struct object object_base_new(enum object_tag);
struct closure object_closure_one_new(size_t, void (*const)(struct object *, struct env_elem *), struct env_elem *);
struct closure object_closure_two_new(size_t, void (*const)(struct object *, struct object *, struct env_elem *), struct env_elem *);
struct int_obj object_int_obj_new(int64_t);

#endif /* SOMESCHEME_H */
