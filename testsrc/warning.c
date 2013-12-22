extern int x = 5;  // Warning (even without -W or -Wall): initialized extern.

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  /* int x; return x ? 1 : 2; */
  return 0;
}

/* static int answer(); */  /* Warning: unused. */
