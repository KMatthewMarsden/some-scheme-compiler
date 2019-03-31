#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#include "base.h"
#include "common.h"
#include "gc.h"
#include "vec.h"

static bool stack_check(void);

static struct thunk *current_thunk;
static void *stack_initial;
static jmp_buf setjmp_env_buf;

void call_closure_one(struct object *rator, struct object *rand) {
  if (DEBUG_ONLY(rator->tag != OBJ_CLOSURE)) {
    RUNTIME_ERROR("Called object (%p) was not a closure but was: %d", rator,
                  rator->tag);
  }

  struct closure *closure = (struct closure *)rator;

  if (DEBUG_ONLY(closure->size != CLOSURE_ONE)) {
    printf("Trying to call: %p\n", closure->fn_1);
    RUNTIME_ERROR("Called a closure that takes two args with one arg");
  }

  if (stack_check()) {
    closure->fn_1(rand, closure->env);
  } else {
    // TODO: move to our own gc allocator?
    struct thunk thnk = {
        .closr = closure,
        .one = {rand},
    };
    struct thunk *thnk_heap = malloc(sizeof(struct thunk));
    memcpy(thnk_heap, &thnk, sizeof(struct thunk));
    run_minor_gc(thnk_heap);
  }
}

void call_closure_two(struct object *rator, struct object *rand,
                      struct object *cont) {
  if (DEBUG_ONLY(rator->tag != OBJ_CLOSURE)) {
    RUNTIME_ERROR("Called object (%p) was not a closure but was: %d", rator,
                  rator->tag);
  }

  struct closure *closure = (struct closure *)rator;

  if (DEBUG_ONLY(closure->size != CLOSURE_TWO)) {
    RUNTIME_ERROR("Called a closure that takes one arg with two args");
  }

  if (stack_check()) {
    closure->fn_2(rand, cont, closure->env);
  } else {
    // TODO: move to our own gc allocator?
    struct thunk thnk = {
        .closr = closure,
        .two = {rand, cont},
    };
    struct thunk *thnk_heap = malloc(sizeof(struct thunk));
    memcpy(thnk_heap, &thnk, sizeof(struct thunk));
    run_minor_gc(thnk_heap);
  }
}

struct void_obj halt_func(struct object *inp) {
  (void)inp; // mmh
  printf("Halt");
  exit(0);

  // unreachable
  return object_void_obj_new();
}

static size_t get_stack_limit(void) {
  static size_t cached_limit = 0;

  if (cached_limit != 0) {
    return cached_limit;
  }

  struct rlimit limit;
  getrlimit(RLIMIT_STACK, &limit);
  cached_limit = limit.rlim_cur;
  return cached_limit;
}

static void *stack_ptr(void) { return __builtin_frame_address(0); }

/*
 * Are we above the stack limit
 */
static bool stack_check(void) {
  // buffer area at the end of the stack since idk how accurate this is
  // so reserve 256K for anything we might do after getting to the 'limit'
  static size_t stack_buffer = 1024 * 256;
  uintptr_t stack_ptr_val = (uintptr_t)stack_ptr();
  uintptr_t stack_end_val =
      (uintptr_t)(stack_initial - get_stack_limit() + stack_buffer);

  return stack_ptr_val > stack_end_val;
}

struct void_obj *global_void_obj;

void scheme_start(struct thunk *initial_thunk) {
  struct void_obj v_obj = object_void_obj_new();
  global_void_obj = &v_obj;
  printf("global_void_obj = %p\n", (void *)global_void_obj);

  stack_initial = stack_ptr();
  current_thunk = initial_thunk;

  gc_init();

  // This is our trampoline, when we come back from a longjmp a different
  // current_thunk will be set and we will just trampoline into the new
  // thunk
  setjmp(setjmp_env_buf);

  DEBUG_FPRINTF(stderr, "bouncing\n");

  if (current_thunk->closr->size == CLOSURE_ONE) {
    struct closure *closr = current_thunk->closr;
    struct object *rand = current_thunk->one.rand;
    struct env_table *env = current_thunk->closr->env;
    free(current_thunk);
    closr->fn_1(rand, env);
  } else {
    struct closure *closr = current_thunk->closr;
    struct object *rand = current_thunk->two.rand;
    struct object *cont = current_thunk->two.cont;
    struct env_table *env = current_thunk->closr->env;
    free(current_thunk);
    closr->fn_2(rand, cont, env);
  }

  RUNTIME_ERROR("Control flow returned from trampoline function.");
}

void run_minor_gc(struct thunk *thnk) {
  current_thunk = thnk;

  struct gc_context ctx = gc_make_context();
  gc_minor(&ctx, thnk);
  gc_free_context(&ctx);

  // Jump back to the start
  longjmp(setjmp_env_buf, 1);
}

struct object object_base_new(enum object_tag tag) {
  return (struct object){
      .tag = tag,
      .mark = WHITE,
      .on_stack = true,
#ifndef NDEBUG
      .last_touched_by = "object_init",
#endif
  };
}

struct closure object_closure_one_new(size_t env_id,
                                      void (*const fn)(struct object *,
                                                       struct env_table *),
                                      struct env_table *env) {
  return (struct closure){.base = object_base_new(OBJ_CLOSURE),
                          .size = CLOSURE_ONE,
                          .env_id = env_id,
                          .fn_1 = fn,
                          .env = env};
}

struct closure object_closure_two_new(size_t env_id,
                                      void (*const fn)(struct object *,
                                                       struct object *,
                                                       struct env_table *),
                                      struct env_table *env) {
  return (struct closure){.base = object_base_new(OBJ_CLOSURE),
                          .size = CLOSURE_TWO,
                          .env_id = env_id,
                          .fn_2 = fn,
                          .env = env};
}

struct int_obj object_int_obj_new(int64_t val) {
  return (struct int_obj){.base = object_base_new(OBJ_INT), .val = val};
}

struct void_obj object_void_obj_new(void) {
  return (struct void_obj){.base = object_base_new(OBJ_VOID)};
}

struct object *env_get(size_t ident_id, struct env_table *env) {
  DEBUG_LOG("looking for %ld in env: %p\n", ident_id, env);

  if (env->vals[ident_id] != NULL) {
    struct object *val = env->vals[ident_id];

    DEBUG_FPRINTF(stderr, "getting %p tag: %d, id: %ld from env %p\n",
                  (void *)val, val->tag, ident_id, (void *)env);

    if (DEBUG_ONLY(val->tag > OBJ_STR)) {
      RUNTIME_ERROR(
          "Invalid object tag in env: %p tag: %d, id: %ld from env %p\n",
          (void *)val, val->tag, ident_id, (void *)env);
    }

    return val;
  }

  RUNTIME_ERROR("Value not present in env: %ld");
}

struct object *env_set(size_t ident_id, struct env_table *env,
                       struct object *obj) {
  struct object *prev = env->vals[ident_id];

  env->vals[ident_id] = obj;

  return prev;
}
