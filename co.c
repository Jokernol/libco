#include "co.h"
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STACK_SIZE 1024 * 64
#define NCOROUTINE 128

enum co_status {
  CO_DESTORY = 0,
  CO_NEW,
  CO_RUNNING,
  CO_WAITING,
  CO_DEAD,
};

struct co {
  char *name;
  void *arg;
  void (*func)(void *);
  void (*ret )(void *);

  enum co_status status;
  struct co *    prev;
  struct co *    next;
  struct co *    waiter;
  jmp_buf        context;
  uint8_t        stack[STACK_SIZE] __attribute__((__aligned__(16)));
};

struct cpool {
  struct co *    head;
  struct co *    tail;
  struct co *    free;
  struct co *    used;
  struct co *    dead;
  int            free_num;
  int            used_num;
  int            dead_num;
};

struct cpool *   cptr;
struct co *      current;
struct co *      mainco;

static inline void stack_switch_call(void *sp, void *entry, void *ret, uintptr_t arg) {
  asm volatile (
#if __x86_64__
    "movq %0, %%rsp; push %2; movq %3, %%rdi; jmp *%1"
      : : "b"((uintptr_t)sp), "d"(entry), "c"(ret), "a"(arg) : "memory"
#else
    "movl %0, %%esp; movl %2, (%0); movl %3, 4(%0); jmp *%1"
      : : "b"((uintptr_t)sp - 8), "d"(entry), "c"(ret), "a"(arg) : "memory"
#endif
  );
}

static void pick(struct co *co, struct co **src) {
  struct co * ptr = *src;

  while (co != ptr) {
    ptr = ptr->next;
  }

  if (ptr != *src) {
    ptr->prev->next = ptr->next;

    if (ptr->next) ptr->next->prev = ptr->prev;
  } else {
    *src = ptr->next;

    if (ptr->next) (*src)->prev = NULL;
  }
}

static void insert(struct co *co, struct co **dst) {
  co->next = *dst;
  if (*dst) (*dst)->prev = co;
  *dst = co;
}

static struct co *co_get() {
  cptr->free_num--;
  cptr->used_num++;

  assert(cptr->free_num >= 0);

  struct co *co = cptr->free;
  //pick from free list
  pick(co, &cptr->free);

  //insert into used list
  insert(co, &cptr->used);

  return co;
}

static void co_dead(struct co *co) {
  cptr->used_num--;
  cptr->dead_num++;

  //pick from used list
  pick(co, &cptr->used);

  co->status = CO_DEAD;

  //insert into dead list
  insert(co, &cptr->dead);
}

static void co_free(struct co *co) {
  cptr->dead_num--;
  cptr->free_num++;

  //pick from dead list
  pick(co, &cptr->dead);

  memset(co, 0, sizeof(struct co));

  //insert into free list
  insert(co, &cptr->free);
}

static void init_cpool() {
  // init cpool
  cptr = malloc(sizeof(struct cpool));
  cptr->head = calloc(NCOROUTINE, sizeof(struct co)),
  cptr->tail = cptr->head;
  cptr->free = cptr->head;
  cptr->used = NULL;
  cptr->dead = NULL;
  cptr->free_num = NCOROUTINE;
  cptr->used_num = 0;
  cptr->dead_num = 0;

  //init linklist point
  cptr->head[0].prev = NULL;
  cptr->head[0].next = &cptr->head[1];

  cptr->head[NCOROUTINE - 1].prev = &cptr->head[NCOROUTINE - 2];
  cptr->head[NCOROUTINE - 1].next = NULL;

  for (size_t i = 1; i < NCOROUTINE - 1; i ++) {
    cptr->head[i].prev = &cptr->head[i - 1];
    cptr->head[i].next = &cptr->head[i + 1];
  }
}

static void init_mainco() {
  mainco = co_get();
  mainco->status = CO_RUNNING;
  current = mainco;
}

__attribute__((constructor)) static void init() {
  srand((unsigned)time(NULL));
  init_cpool();
  init_mainco();
}

__attribute__((destructor)) static void destory() {
  struct co *ptr = cptr->dead;

  while (ptr) {
    if (ptr->next) {
      ptr = ptr->next;
      free(ptr->prev);
    } else {
      free(ptr);
    }
  }
}

static void co_schedule() {
  uint8_t flag = 1;

  while (flag) {
    size_t index = rand() % cptr->used_num;

    current = cptr->used;

    for (size_t i = 0; i < index; i ++) {
      current = current->next;
    }

    switch (current->status) {
      case CO_NEW:
        current->status = CO_RUNNING;
        stack_switch_call(&current->stack[STACK_SIZE], current->func, current->ret, (uintptr_t)current->arg);
        flag = 0;
        break;
      case CO_RUNNING:
        longjmp(current->context, 0);
        flag = 0;
        break;
      default: break;
    }
  }
}

static void co_ret() {
  if (current != mainco) {
    co_dead(current);
    co_schedule();
  }
}

struct co *co_start(const char *name, void (*func)(void *), void *arg) {
  struct co *co = co_get();

  co->func   = func;
  co->ret    = co_ret;
  co->arg    = arg;
  co->status = CO_NEW;
  co->waiter = NULL;

  co->name = malloc(strlen(name) + 1);
  strcpy(co->name, name);
  co->name[strlen(name)] = '\0';

  return co;
}

void co_wait(struct co *co) {
  switch (co->status) {
    case CO_NEW:
    case CO_RUNNING:
      // current->status = CO_WAITING;
      // co->waiter = &mainco;
      co_yield();
      co_wait(co);
      break;
    case CO_DEAD:
      co_free(co);
      break;
    default: break;
  }
}

void co_yield() {
  int val = setjmp(current->context);

  if (val == 0) co_schedule();
}
