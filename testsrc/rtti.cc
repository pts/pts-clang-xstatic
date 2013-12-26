//: PKN4myns1AE = myns::A const*
//: PKN4myns1AE = myns::A const*
//: N4myns1AE = myns::A
//: N4myns1BE = myns::B

#include <stdio.h>
#include <stdlib.h>

#include <typeinfo>
#include <cxxabi.h>

namespace myns {

class A {
 public:
  virtual ~A() {}  // g++-4.1 would warn about missing virtual destructor.
  virtual const char *name() { return "NA"; }
};
class B : public A {
 public:
  virtual ~B() {}  // g++-4.1 would warn about missing virtual destructor.
  virtual const char *name() { return "NB"; }
};

}  // namespace myns

class PtrFreer {
 public:
  PtrFreer(const char *ptr): ptr_(ptr) {}
  ~PtrFreer() { free((void*)ptr_); }
  const char *ptr() { return ptr_; }
 private:
  const char *ptr_;
};

static PtrFreer demangle(const char *name) {
  int status;
  name = abi::__cxa_demangle(name, 0, 0, &status);
  return PtrFreer(status == 0 ? name : NULL);
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  myns::A a;
  myns::B b;
  const myns::A *ap = &a, *bp = &b;
  // Compile error with -fno-rtti.
  printf("%s = %s\n", typeid(ap).name(), demangle(typeid(ap).name()).ptr());
  // Compile error with -fno-rtti.
  printf("%s = %s\n", typeid(bp).name(), demangle(typeid(bp).name()).ptr());
  // Compile error with -fno-rtti.
  printf("%s = %s\n", typeid(*ap).name(), demangle(typeid(*ap).name()).ptr());
  // Compile error with -fno-rtti.
  printf("%s = %s\n", typeid(*bp).name(), demangle(typeid(*bp).name()).ptr());
  return 0;
}
