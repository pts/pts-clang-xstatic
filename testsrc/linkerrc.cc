/* A link error for new and delete if not linked against libstdc++.
 * If compiled with clang, compile with -O0, otherwise the dynamic memory
 * allocation will be optimized away.
 */

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  int *answerp = new int(42);
  int retval = *answerp != 42;
  delete answerp;
  return retval;
}
