//: AP
//: BP
//: AP
//: BP
//: NULL
//: BP

#include <stdio.h>

namespace myns {

class A {
 public:
  virtual ~A() {}  // g++-4.1 would warn about missing virtual destructor.
  virtual const char *name() const { return "NA"; }
};
class B : public A {
 public:
  virtual ~B() {}  // g++-4.1 would warn about missing virtual destructor.
  virtual const char *name() const { return "NB"; }
};

}  // namespace myns

static const char *which(const void *ap, const void *bp, const void *xp) {
  return !xp ? "NULL" : xp == ap ? "AP" : xp == bp ? "BP" : "OTHER";
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  myns::A a;
  myns::B b;
  const myns::A *ap = &a, *bp = &b;
  printf("%s\n", which(ap, bp, ap));
  printf("%s\n", which(ap, bp, bp));
  printf("%s\n", which(ap, bp, dynamic_cast<const myns::A*>(ap)));
  printf("%s\n", which(ap, bp, dynamic_cast<const myns::A*>(bp)));
  // Compile error with g++ -fno-rtti: this dynamic_cast is no permitted.
  // clang++-3.0 -fno-rtti compiles it, but segfaults at runtime.
  printf("%s\n", which(ap, bp, dynamic_cast<const myns::B*>(ap)));
  // Ditto for this cast.
  printf("%s\n", which(ap, bp, dynamic_cast<const myns::B*>(bp)));
  return 0;
}
