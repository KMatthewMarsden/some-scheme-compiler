#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "base.h"
#include "common.h"
#include "gc.h"
#include "queue.h"
#include "tree.h"
#include "vec.h"

MAKE_VECTOR(struct object *, gc_heap_nodes)
MAKE_VECTOR(size_t, size_t)
MAKE_QUEUE(struct object *, gc_grey_nodes)
MAKE_QUEUE(struct ptr_toupdate_pair, ptr_toupdate_pair)

static struct gc_data gc_global_data;

// array of gc_funcs for each object type
static struct gc_funcs gc_func_map[] = {
    [OBJ_CLOSURE] = (struct gc_funcs){.toheap = toheap_closure,
                                      .mark = mark_closure,
                                      .free = gc_free_noop},
    [OBJ_ENV] = (struct gc_funcs){.toheap = toheap_env,
                                  .mark = mark_env,
                                  .free = gc_free_noop},
    [OBJ_INT] = (struct gc_funcs){.toheap = toheap_int_obj,
                                  .mark = gc_mark_noop,
                                  .free = gc_free_noop},
    [OBJ_VOID] = (struct gc_funcs){.toheap = toheap_void_obj,
                                   .mark = gc_mark_noop,
                                   .free = gc_free_noop},
    [OBJ_STR] =
        (struct gc_funcs){
            .toheap = toheap_string_obj,
            .mark = gc_mark_noop,
            .free = gc_free_noop,
        },
};

// This does nothing, the gc will call free() on the object if it was heap
// allocated
void gc_free_noop(struct object *obj) { (void)obj; }

// This does nothing, for objects where marking them should mark them as black
// and nothing more
void gc_mark_noop(struct object *obj, struct gc_context *ctx) {
  (void)obj;
  (void)ctx;
}

// Mark an object as grey and add it to the queue of grey nodes 'if' it is not
// already grey or black
static bool maybe_mark_grey_and_queue(struct gc_context *ctx,
                                      struct object *obj) {
  switch (obj->mark) {
  case BLACK:
  case GREY:
    return false;
  case WHITE:
    obj->mark = GREY;
    queue_gc_grey_nodes_enqueue(&ctx->grey_nodes, obj);
    return true;
  }
}

void queue_ptr_toupdate_pair_enqueue_checked(
    struct queue_ptr_toupdate_pair *queue, struct ptr_toupdate_pair elem) {
  assert(elem.on_stack != NULL);
  assert(elem.toupdate != NULL);

  // if the target is already on the heap, no need to copy it
  if (!elem.on_stack->on_stack) {
    return;
  }

  queue_ptr_toupdate_pair_enqueue(queue, elem);
}

struct object *toheap_closure(struct object *obj, struct gc_context *ctx) {
  struct closure *clos = (struct closure *)obj;

  if (obj->on_stack) {
    TOUCH_OBJECT(obj, "toheap_closure");
    struct closure *heap_clos = gc_malloc(sizeof(struct closure));
    memcpy(heap_clos, obj, sizeof(struct closure));
    clos = heap_clos;
  }

  if (clos->env->base.on_stack) {
    TOUCH_OBJECT(&clos->env->base, "toheap_closure");
    struct env_table *heap_env = gc_malloc(ENV_TABLE_SIZE);
    memcpy(heap_env, clos->env, ENV_TABLE_SIZE);
    heap_env->base.on_stack = false;
    clos->env = heap_env;

    struct env_table_id_map id_map = global_env_table[clos->env_id];

    for (size_t i = 0; i < id_map.num_ids; i++) {
      if (clos->env->vals[id_map.var_ids[i]] == NULL) {
        continue;
      }

      queue_ptr_toupdate_pair_enqueue_checked(&ctx->pointers_toupdate, (struct ptr_toupdate_pair){
        .on_stack = clos->env->vals[id_map.var_ids[i]],
        .toupdate = &clos->env->vals[id_map.var_ids[i]]});
    }
  }

  return (struct object *)clos;
}

void mark_closure(struct object *obj, struct gc_context *ctx) {
  struct closure *clos = (struct closure *)obj;

  struct env_table_id_map id_map = global_env_table[clos->env_id];

  for (size_t i = 0; i < id_map.num_ids; i++) {
    struct object *val = clos->env->vals[id_map.var_ids[i]];
    if (val) {
      maybe_mark_grey_and_queue(ctx, val);
    }
  }

  // manually mark the env as black (it's been so long idk if this matters)
  clos->env->base.mark = BLACK;
}

struct object *toheap_env(struct object *obj, struct gc_context *ctx) {
  RUNTIME_ERROR("Actually calling toheap_env!");
}

void mark_env(struct object *obj, struct gc_context *ctx) {
  RUNTIME_ERROR("Actually calling mark_env!");
}

struct object *toheap_int_obj(struct object *obj, struct gc_context *ctx) {
  struct int_obj *intobj = (struct int_obj *)obj;

  if (obj->on_stack) {
    TOUCH_OBJECT(obj, "toheap_int");
    struct int_obj *heap_intobj = gc_malloc(sizeof(struct int_obj));
    memcpy(heap_intobj, intobj, sizeof(struct int_obj));
    intobj = heap_intobj;
  }

  return (struct object *)intobj;
}

struct object *toheap_void_obj(struct object *obj, struct gc_context *ctx) {
  return (struct object *) global_void_obj;
}

struct object *toheap_string_obj(struct object *obj, struct gc_context *ctx) {
  struct string_obj *strobj = (struct string_obj *)obj;

  if (obj->on_stack) {
    TOUCH_OBJECT(obj, "toheap_string");
    size_t total_size = sizeof(struct string_obj) + strobj->len;

    struct string_obj *heap_stringobj = gc_malloc(total_size);

    memcpy(heap_stringobj, strobj, total_size);

    strobj = heap_stringobj;
  }

  return (struct object *)strobj;
}

struct gc_context gc_make_context(void) {
  return (struct gc_context){
      .grey_nodes = queue_gc_grey_nodes_new(10),
      .pointers_toupdate = queue_ptr_toupdate_pair_new(10),
      .updated_pointers = ptr_bst_new(),
  };
}

void gc_free_context(struct gc_context *ctx) {
  queue_gc_grey_nodes_free(&ctx->grey_nodes);
  queue_ptr_toupdate_pair_free(&ctx->pointers_toupdate);
  ptr_bst_free(&ctx->updated_pointers);
}

static void gc_mark_obj(struct gc_context *ctx, struct object *obj) {
  obj->mark = BLACK;
  gc_func_map[obj->tag].mark(obj, ctx);
}

// Moves all live objects on the stack over to the heap
struct object *gc_toheap(struct gc_context *ctx, struct object *obj) {
  assert(obj != NULL);

  // if we've already copied this object,
  // we know that anything it points to must also be sorted
  struct object *maybe_copied = ptr_bst_get(&ctx->updated_pointers, obj);
  if (maybe_copied != NULL) {
    return maybe_copied;
  }

  struct object *new_obj = gc_func_map[obj->tag].toheap(obj, ctx);

  // mark the object as now being on the heap
  new_obj->on_stack = false;

  // Add it to the updated tree
  // Even if it was on the heap already we still insert
  // since we then won't process child objects further
  struct ptr_pair pair = {.old = obj, .new = new_obj};
  ptr_bst_insert(&ctx->updated_pointers, pair);

  return new_obj;
}

// The minor gc, moves all stack objects to the heap
// The parameter 'thnk' is the current thunk holding everything together
// The thunk should be heap allocated and freed after being called
void gc_minor(struct gc_context *ctx, struct thunk *thnk) {

  DEBUG_FPRINTF(stderr, "before gc, thnk->closr->env = %p\n", thnk->closr->env);

  // initially mark the closure and it's arguments to be applied
  thnk->closr = (struct closure *)gc_toheap(ctx, (struct object *)thnk->closr);

  switch (thnk->closr->size) {
  case CLOSURE_ONE:
    if (thnk->one.rand != NULL) {
      thnk->one.rand = gc_toheap(ctx, thnk->one.rand);
    }
    break;
  case CLOSURE_TWO:
    if (thnk->two.rand != NULL) {
      thnk->two.rand = gc_toheap(ctx, thnk->two.rand);
    }
    if (thnk->two.cont != NULL) {
      thnk->two.cont = gc_toheap(ctx, thnk->two.cont);
    }
    break;
  }

  // work through each pointer that needs to be updated
  while (queue_ptr_toupdate_pair_len(&ctx->pointers_toupdate) > 0) {
    struct ptr_toupdate_pair to_update =
        queue_ptr_toupdate_pair_dequeue(&ctx->pointers_toupdate);
    struct object *maybe_copied =
        ptr_bst_get(&ctx->updated_pointers, to_update.on_stack);
    if (maybe_copied != NULL) {
      // we've already updated this pointer, just update the pointer that
      // needs to be updated
      *to_update.toupdate = maybe_copied;
    } else {
      // we haven't seen this yet, perform a copy and update

      assert(to_update.on_stack != NULL);

      struct object *on_heap = gc_toheap(ctx, to_update.on_stack);
      ptr_bst_insert(&ctx->updated_pointers,
                     (struct ptr_pair){to_update.on_stack, on_heap});

      *to_update.toupdate = on_heap;
    }
  }

  DEBUG_FPRINTF(stderr, "after gc, thnk->closr->env = %p\n", thnk->closr->env);

  gc_major(ctx, thnk);
}

// The major gc, collects objects on the heap
void gc_major(struct gc_context *ctx, struct thunk *thnk) {
  size_t num_freed = 0;

  gc_mark_obj(ctx, &thnk->closr->base);

  switch (thnk->closr->size) {
  case CLOSURE_ONE:
    gc_mark_obj(ctx, thnk->one.rand);
    break;
  case CLOSURE_TWO:
    gc_mark_obj(ctx, thnk->two.rand);
    gc_mark_obj(ctx, thnk->two.cont);
    break;
  }

  while (queue_gc_grey_nodes_len(&ctx->grey_nodes) > 0) {
    struct object *next_obj = queue_gc_grey_nodes_dequeue(&ctx->grey_nodes);
    gc_mark_obj(ctx, next_obj);
  }

  // go through each heap allocated object and gc them
  // not really the best, but it would be easy to improve
  for (size_t i = 0; i < gc_global_data.nodes.length; i++) {
    struct object **ptr =
        vector_gc_heap_nodes_index_ptr(&gc_global_data.nodes, i);
    struct object *obj = *ptr;

    if (obj == NULL) {
      continue;
    }

    if (obj->mark == WHITE) {
      // free it, should this be done if the object is on the stack?
      if (DEBUG_ONLY(obj->on_stack)) {
        DEBUG_ONLY(RUNTIME_ERROR("Object (%p, tag: %d, %s) was on the stack during a major GC!", (void *)obj, obj->tag, obj->last_touched_by));
      }

      // execute this object's free function
      gc_func_map[obj->tag].free(obj);

      free(obj);
      num_freed++;

      // set the pointer in the vector to null
      *ptr = NULL;
    } else if (DEBUG_ONLY(obj->mark == GREY)) {
      // this shouldn't happen, but just incase
      RUNTIME_ERROR("Object was marked grey at time of major GC!");
    } else {
      // reset marker now
      obj->mark = WHITE;
    }
  }

  printf("freed %zu objects\n", num_freed);
  gc_heap_maintain();
}

void gc_init(void) { gc_global_data.nodes = vector_gc_heap_nodes_new(100); }

// wrapped malloc that adds allocated stuff to the bookkeeper
void *gc_malloc(size_t size) {
  void *ptr = malloc(size);

  vector_gc_heap_nodes_push(&gc_global_data.nodes, ptr);
  return ptr;
}

void gc_heap_maintain(void) {
  struct vector_gc_heap_nodes new_nodes = vector_gc_heap_nodes_new(gc_global_data.nodes.length);

  for (size_t i = 0; i < gc_global_data.nodes.length; i++) {
    struct object *obj = vector_gc_heap_nodes_index(&gc_global_data.nodes, i);
    if (obj != NULL) {
      vector_gc_heap_nodes_push(&new_nodes, obj);
    }
  }
}
