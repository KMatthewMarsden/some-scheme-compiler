#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#include "base.h"
#include "gc.h"
#include "vec.h"

MAKE_VECTOR(struct env_elem *, env_elem_nexts)

static bool stack_check(void);

static struct thunk *current_thunk;
static void *stack_initial;
static jmp_buf setjmp_env_buf;

void call_closure_one(struct object *rator, struct object *rand) {
    if (rator->tag != 0) {
        RUNTIME_ERROR("Called object was not a closure");
    }

    struct closure *closure = (struct closure *)rator;

    if (closure->size == CLOSURE_TWO) {
        RUNTIME_ERROR("Called a closure that takes two args with one arg");
    }

    // ADD_ENV(rand_id, rand, &closure->env);
    if (stack_check()) {
        closure->fn_1(rand, closure->env);
    } else {
        // TODO, move to our own gc allocator?
        struct thunk thnk = {
            .closr = closure,
            .one = {rand},
        };
        struct thunk *thnk_heap = malloc(sizeof(struct thunk));
        memcpy(thnk_heap, &thnk, sizeof(struct thunk));
        run_minor_gc(thnk_heap);
    }
}

void call_closure_two(struct object *rator, struct object *rand, struct object *cont) {
    if (rator->tag != 0) {
        RUNTIME_ERROR("Called object was not a closure");
    }

    struct closure *closure = (struct closure *)rator;

    if (closure->size != CLOSURE_ONE) {
        RUNTIME_ERROR("Called a closure that takes two args with one arg");
    }

    // ADD_ENV(rand_id, rand, &closure->env);
    // ADD_ENV(cont_id, cont, &closure->env);

    if (stack_check()) {
        closure->fn_2(rand, cont, closure->env);
    } else {
        // TODO, move to our own gc allocator?
        struct thunk thnk = {
            .closr = closure, // copy the closure
            .two = {rand, cont},
        };
        struct thunk *thnk_heap = malloc(sizeof(struct thunk));
        memcpy(thnk_heap, &thnk, sizeof(struct thunk));
        run_minor_gc(thnk_heap);
    }
}

void halt_func(struct object *cont, struct env_elem *env) {
    (void)cont; // mmh
    (void)env;
    printf("Halt");
    exit(0);
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
    static size_t stack_buffer = 1024 * 32;
    return (uintptr_t)stack_ptr() >
           (uintptr_t)(stack_initial - get_stack_limit() - stack_buffer);
}

void scheme_start(struct thunk *initial_thunk) {
    stack_initial = stack_ptr();
    current_thunk = initial_thunk;

    gc_init();

    // This is our trampoline, when we come back from a longjmp a different
    // current_thunk will be set and we will just trampoline into the new
    // thunk
    setjmp(setjmp_env_buf);

    if (current_thunk->closr->size == CLOSURE_ONE) {
        struct closure *closr = current_thunk->closr;
        struct object *rand = current_thunk->one.rand;
        struct env_elem *env = current_thunk->closr->env;
        free(current_thunk);
        closr->fn_1(rand, env);
    } else {
        struct closure *closr = current_thunk->closr;
        struct object *rand = current_thunk->two.rand;
        struct object *cont = current_thunk->two.cont;
        struct env_elem *env = current_thunk->closr->env;
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
    };
}

struct closure object_closure_one_new(size_t env_id,
                                      void (*const fn)(struct object *,
                                                       struct env_elem *),
                                      struct env_elem *env) {
    return (struct closure){
        .base = object_base_new(OBJ_CLOSURE),
        .size = CLOSURE_ONE,
        .env_id = env_id,
        .fn_1 = fn,
        .env = env
    };
}

struct closure object_closure_two_new(size_t env_id,
                                      void (*const fn)(struct object *,
                                                       struct object *,
                                                       struct env_elem *),
                                      struct env_elem *env) {
    return (struct closure){
        .base = object_base_new(OBJ_CLOSURE),
        .size = CLOSURE_TWO,
        .env_id = env_id,
        .fn_2 = fn,
        .env = env
    };
}

struct int_obj object_int_obj_new(int64_t val) {
    return (struct int_obj){
        .base = object_base_new(OBJ_INT),
        .val = val
    };
}


struct object *env_get(size_t ident_id, struct env_elem *env) {
    while (env->prev != NULL) {
        if (env->ident_id == ident_id) {
            return env->val;
        }
    }
    return NULL;
}

struct object *env_set(size_t ident_id, struct env_elem *env, struct object *obj) {
    while (env->prev != NULL) {
        if (env->ident_id == ident_id) {
            struct object *prev =  env->val;
            env->val = obj;
            return prev;
        }
    }
    return NULL;
}
