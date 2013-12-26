//: All OK.
#include <sstream>
#include <iostream>
#include <string>

using std::cerr;
using std::cout;
using std::endl;
using std::ostringstream;
using std::string;

static string i2s(int i) { ostringstream s; s << i; return s.str(); } 
static string i2s(long long i) { ostringstream s; s << i; return s.str(); } 

class C {
 public:
  C(int n): n_(n) {}
  C(const C &other): n_(other.n_ + 1) {}
  ~C() {}
  int n() const { return n_; }
  string s() const { return i2s(n_); }
 private:
  C& operator=(const C &other);

  int n_;
};

static inline int bad200(int n) {
  if (n < 2) throw C(200);
  return bad200(n - 1) + bad200(n - 2);
}

static inline int bad300(int n) {
  if (n < 2) throw 300;
  return bad300(n - 1) + bad300(n - 2);
}

static inline int bad400(int n) {
  if (n < 2) throw 400LL;
  return bad400(n - 1) + bad400(n - 2);
}

static string try1() {
  try {
    bad200(20);
  } catch (int) {
    return "INT";
  } catch (C &c) {
    return "REF=" + c.s();
#if USE_EARLY
  } catch (C c) {  // Warning OK.
    return "VAL=" + c.s();
#endif
  } catch (...) {
    return "OTHER";
  }
  return "OK";
}

static string try2() {
  try {
    bad200(20);
  } catch (C c) {
    return "VAL=" + c.s();
#if USE_EARLY
  } catch (C &c) {  // Warning OK.
    return "REF=" + c.s();
#endif
  } catch (int i) {
    return "INT=" + i2s(i);
  } catch (...) {
    return "OTHER";
  }
  return "OK";
}

static string try3() {
  try {
    bad300(20);
  } catch (C c) {
    return "REF=" + c.s();
#if USE_EARLY
  } catch (C &c) {  // Warning OK.
    return "VAL=" + c.s();
#endif
  } catch (long long i) {
    return "LLO=" + i2s(i);
  } catch (int i) {
    return "INT=" + i2s(i);
  } catch (...) {
    return "OTHER";
  }
  return "OK";
}

static string try4() {
  try {
    bad400(20);
  } catch (C c) {
    return "REF=" + c.s();
#if USE_EARLY
  } catch (C &c) {  // Warning OK.
    return "VAL=" + c.s();
#endif
  } catch (int i) {
    return "INT=" + i2s(i);
  } catch (long long i) {
    return "LLO=" + i2s(i);
  } catch (...) {
    return "OTHER";
  }
  return "OK";
}

class Expecter {
 public:
  Expecter(): error_count_(0) {}
  ~Expecter() {}
  operator int() { return error_count_; }
  Expecter& PrintCount() {
    if (error_count_ == 0) {
      cout << "All OK." << endl;
    } else if (error_count_ == 1) {
      cout << "Had 1 error." << endl;
    } else {
      cout << "Had " << error_count_ << " errors." << endl;
    }
    return *this;
  }
  Expecter& ExpectEq(const string &location, const string &expr,
                     const string &exprval, const string &expected) {
    if (exprval != expected) {
      cout << location << ": value of " << expr << " should be " << expected
           << ", but it was " << exprval << endl; 
      ++error_count_;
    }
    return *this;
  }
 private:
  Expecter(const Expecter &other);
  Expecter& operator=(const Expecter &other);

  int error_count_;
};
#define EXPECTEQ(expected, expr) ExpectEq( \
    (string(__FILE__) + ":" + i2s(__LINE__)), \
    #expr, expr, expected)

int main(int argc, char **argv) {
  (void)argc; (void)argv; 
  return Expecter()
      .EXPECTEQ("REF=200", try1())
      .EXPECTEQ("VAL=201", try2())
      .EXPECTEQ("INT=300", try3())
      .EXPECTEQ("LLO=400", try4())
      .PrintCount();
}
