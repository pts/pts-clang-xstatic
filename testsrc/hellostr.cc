//: Hello, kC++ World!
#include <iostream>
#include <string>

const std::string kHello("Hello");
static const std::string kWorld("World");

int main(int argc, char **argv) {
  static const std::string kLang("kC++");
  (void)argc;
  (void)argv;
  std::cout << kHello << ", " << kLang << ' ' << kWorld << '!' << std::endl;
  return 0;
}
