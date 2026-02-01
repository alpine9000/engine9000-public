#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
  ULONG a;
  ULONG b;
} example_t;

volatile example_t example = {
  .a = 0xDEADBEEF,
  .b = 0xF00DD00F,
};

int function(int i)
{
  printf("function: %d %08x\n", i, example.b);
  example.a++;
  example.b++;
  return 1;
}

int main(int argc, char **argv)
{

  for (volatile int i = 0; i < 10; i++) {
    printf("main: %d\n", i);
  }

  for (volatile int i = 0; i < 10; i++) {
    function(i);
  }

  while(1) {
    function(1);
  }
  
  return 0;
}
