//: BA
//: BA
//: NA
//: NB
#include <stdio.h>

namespace myns {

class A {
 public:
  virtual ~A() {}  // g++-4.1 would warn about missing virtual destructor.
  const char *bname() const { return "BA"; }
  virtual const char *name() const { return "NA"; }
};
class B : public A {
 public:
  virtual ~B() {}  // g++-4.1 would warn about missing virtual destructor.
  const char *bname() const { return "BB"; }
  virtual const char *name() const { return "NB"; }
};

}  // namespace myns

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  myns::A a;
  myns::B b;
  const myns::A *ap = &a, *bp = &b;
  printf("%s\n", ap->bname());
  printf("%s\n", bp->bname());
  printf("%s\n", ap->name());
  printf("%s\n", bp->name());
  return 0;
}
