#ifndef SOMESCHEME_H
#define SOMESCHEME_H

#include <stdbool.h>
#include <stdlib.h>

#include "common.h"
#include "vec.h"

#define ADD_ENV(IDENT_ID, VAL, HEAD_PTR)                                       \
  do {                                                                         \
                                                                               \
    if (DEBUG_ONLY((VAL)->tag > OBJ_STR)) {                                    \
      RUNTIME_ERROR(                                                           \
          "Invalid object tag in env: tag: %d, id: %ld from env %p\n",         \
          (VAL)->tag, (IDENT_ID), (void *)*(HEAD_PTR));                        \
    }                                                                          \
    struct env_table *new_env = alloca(ENV_TABLE_SIZE);                        \
    memcpy(new_env, *(HEAD_PTR), ENV_TABLE_SIZE);                              \
    new_env->base = object_base_new(OBJ_ENV);                                  \
    new_env->vals[IDENT_ID] = VAL;                                             \
                                                                               \
    TOUCH_OBJECT(VAL, "add_env");                                              \
    DEBUG_FPRINTF(stderr, "adding tag: %d, id: %ld to env %p\n", (VAL)->tag,   \
                  (IDENT_ID), (void *)*(HEAD_PTR));                            \
                                                                               \
    (*HEAD_PTR) = new_env;                                                     \
  } while (0)

#define NUM_ARGS(...) (sizeof((size_t[]){__VA_ARGS__}) / sizeof(size_t))
#define ENV_ENTRY(ID, ...)                                                     \
  [ID] = (struct env_table_id_map) {                                           \
    ID, NUM_ARGS(__VA_ARGS__), (size_t[]) { __VA_ARGS__ }                      \
  }

#define OBJECT_STRING_OBJ_NEW(S, NAME)                                         \
  struct object *(NAME);                                                       \
  do {                                                                         \
    size_t len = strlen(S) + 1;                                                \
    /* we keep the null byte */                                                \
    struct string_obj *new_obj = alloca(sizeof(struct string_obj) + len);      \
    new_obj->base = object_base_new(OBJ_STR);                                  \
    new_obj->len = len;                                                        \
    memcpy((char *)&new_obj->buf, (S), len);                                   \
    TOUCH_OBJECT(new_obj, "string_obj_new");                                   \
    (NAME) = (struct object *)new_obj;                                         \
  } while (0)

#define OBJECT_INT_OBJ_NEW(n, NAME)                                            \
  struct object *(NAME);                                                       \
  do {                                                                         \
    struct int_obj *new_obj = alloca(sizeof(struct int_obj));                  \
    *new_obj = object_int_obj_new((n));                                        \
    TOUCH_OBJECT(new_obj, "int_obj_new");                                      \
    (NAME) = (struct object *)new_obj;                                         \
  } while (0)

#define OBJECT_VOID_OBJ_NEW(NAME)                                              \
  struct object *(NAME);                                                       \
  do {                                                                         \
    struct void_obj *new_obj = alloca(sizeof(struct void_obj));                \
    *new_obj = object_void_obj_new();                                          \
    TOUCH_OBJECT(new_obj, "void_obj_new");                                     \
    (NAME) = (struct object *)new_obj;                                         \
  } while (0)

#ifndef NDEBUG
#define TOUCH_OBJECT(OBJ, S)                                                   \
  do {                                                                         \
    fprintf(stderr,                                                            \
            "touching object %p tag: %d, last touched by %s: (%s:%d:%s)\n",    \
            (void *)(OBJ), ((struct object *)(OBJ))->tag,                      \
            ((struct object *)(OBJ))->last_touched_by, __func__, __LINE__,     \
            (S));                                                              \
    ALLOC_SPRINTF(((struct object *)(OBJ))->last_touched_by, "(%s:%d:%s)",     \
                  __func__, __LINE__, (S));                                    \
  } while (0)
#else
#define TOUCH_OBJECT(OBJ, S)                                                   \
  do {                                                                         \
  } while (0)
#endif // DEBUG

enum closure_size {
  CLOSURE_ONE = 0,
  CLOSURE_TWO,
};

enum object_tag {
  OBJ_CLOSURE = 0,
  OBJ_ENV,
  OBJ_INT,
  OBJ_VOID,
  OBJ_STR,
};

enum gc_mark_type { WHITE = 0, GREY, BLACK };

struct object {
  enum object_tag tag;
  enum gc_mark_type mark;
  bool on_stack;
#ifndef NDEBUG
  char *last_touched_by;
#endif
};

// builtin objects

struct env_table {
  struct object base;
  struct object *vals[/* env_table_map_size */];
};

extern const size_t env_table_map_size;
#define ENV_TABLE_SIZE (sizeof(struct env_table) + sizeof(struct object *) * env_table_map_size)

struct closure {
  struct object base;
  const enum closure_size size;
  const size_t env_id;
  union {
    void (*const fn_1)(struct object *, struct env_table *);
    void (*const fn_2)(struct object *, struct object *, struct env_table *);
  };
  struct env_table *env;
};

struct int_obj {
  struct object base;
  int64_t val;
};

struct void_obj {
  struct object base;
};

struct string_obj {
  struct object base;
  size_t len;
  const char buf[];
};

struct env_table_id_map {
  const size_t env_id;
  const size_t num_ids;
  size_t *const var_ids;
};

// get an object from the environment
struct object *env_get(size_t, struct env_table *);

// set an existing value in the environment, returning the previous value
struct object *env_set(size_t, struct env_table *, struct object *);

// The map of env ids to an array of var ids
extern struct env_table_id_map const global_env_table[];

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

void call_closure_one(struct object *, struct object *);
void call_closure_two(struct object *, struct object *, struct object *);
struct void_obj halt_func(struct object *);
void scheme_start(struct thunk *);
void run_minor_gc(struct thunk *);

struct object object_base_new(enum object_tag);
struct closure object_closure_one_new(size_t,
                                      void (*const)(struct object *,
                                                    struct env_table *),
                                      struct env_table *);
struct closure object_closure_two_new(
    size_t, void (*const)(struct object *, struct object *, struct env_table *),
    struct env_table *);
struct int_obj object_int_obj_new(int64_t);
struct void_obj object_void_obj_new(void);
struct string_obj object_string_obj_new(const char *);

#endif /* SOMESCHEME_H */
