#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "co-test.h"

int count = 1;

void foo(void *arg) {
  int i;
  for (int i = 0; i < 1; i++) {
    printf("%s %d\n", (char *)arg, count++);
    co_yield();
  }
}

void test() {
  struct co *p1 = co_start("X", foo, "X");
  struct co *p2 = co_start("Y", foo, "Y");
  co_wait(p1);
  co_wait(p2);
  printf("Done\n");
}

int main() {
  test();
  return 0;
}
