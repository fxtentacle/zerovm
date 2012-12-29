/*
 * this sample is a simple demonstration of zerovm.
 * the "hello world" program as usual.
 */
#include "include/api_tools.h"

int main(int argc, char **argv)
{
  zvm_bulk = zvm_init();

  UNREFERENCED_VAR(errcount);

  /* write to zvm provided stdout */
  zput(STDOUT, "\033[1mhello, world!\033[0m\n");

  /* write to zvm provided stderr */
  zput(STDERR, "hello, world!");

  /* exit with code */
  zvm_exit(0);

  /* not reached */
  return 0;
}
