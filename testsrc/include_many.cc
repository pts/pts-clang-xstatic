//: INT:1
//: T:0
//: HTD:1
/* by pts@fazekas.hu at Thu Dec 12 11:16:37 CET 2013 */

#include <vector>
#include <map>
#include <set>
#include <list>
#include <set>
#include <string>
#include <utility>
#include <algorithm>
#include <iostream>
#include <deque>
#include <stack>
#include <queue>
#include <bitset>
#include <cstddef>
#include <cstdio>
// #include <cstdint>  // Needs: -std=c++0x or -std=gnu++0x
#include <cstdlib>
#include <cstring>
// Includes <bits/builting_type_traits.h> (if available), and makes
// __is_pod, __is_empty and __has_trivial_destructor available.
#include <bits/stl_construct.h>
// #include <type_traits>  // Needs: -std=c++0x or -std=gnu++0x.
#include <complex>
#include <sstream>
#include <valarray>
#include <typeinfo>
#include <streambuf>
// #include <ratio>  // Needs: -std=c++0x or -std=gnu++0x
// #include <random>  // Needs: -std=c++0x or -std=gnu++0x
#include <numeric>
#include <memory>
#include <limits>
#include <iterator>
#include <iosfwd>
#include <functional>
#include <initializer_list>
// #include <forward_list>  // Needs: -std=c++0x or -std=gnu++0x
#include <csetjmp>
// #include <cstdbool>  // Needs: -std=c++0x or -std=gnu++0x
#include <cmath>
#include <ciso646>
// #include <array>  // Needs: -std=c++0x or -std=gnu++0x

// Backward, deprecated, compile with -Wno-deprecated:
#if USE_DEPR
#include <strstream>
#include <auto_ptr.h>
#include <hash_map>
#include <hash_set>
#include <backward/strstream>
#include <backward/auto_ptr.h>
#include <backward/hash_map>
#include <backward/hash_set>
#endif

using namespace std;

static map<int, pair<vector<string>, set<list<deque<ostringstream> > > > > m;

struct T { T() {} };

int main() {
  std::cout << "INT:" << (int)(__is_pod(int)) << std::endl;
  std::cout << "T:" << (int)(__is_pod(T)) << std::endl;
  std::cout << "HTD:" << (int)(__has_trivial_destructor(typeof 42.f))
            << std::endl;
}
