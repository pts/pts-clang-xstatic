// TODO(pts): Why doesn't libstdc++ work in clang?
// $ xstatic ~/d/clang/bin/clang++ -std=c++0x -W -Wall -s -O2 intersection_update2.cc
// In file included from intersection_update2.cc:1:
// In file included from /usr/lib/gcc/i486-linux-gnu/4.4/../../../../include/c++/4.4/set:60:
// In file included from /usr/lib/gcc/i486-linux-gnu/4.4/../../../../include/c++/4.4/bits/stl_tree.h:63:
// In file included from /usr/lib/gcc/i486-linux-gnu/4.4/../../../../include/c++/4.4/bits/allocator.h:48:
// In file included from /usr/lib/gcc/i486-linux-gnu/4.4/../../../../include/c++/4.4/i486-linux-gnu/bits/c++allocator.h:34:
// In file included from /usr/lib/gcc/i486-linux-gnu/4.4/../../../../include/c++/4.4/ext/new_allocator.h:33:
// In file included from /usr/lib/gcc/i486-linux-gnu/4.4/../../../../include/c++/4.4/new:40:
// In file included from /usr/lib/gcc/i486-linux-gnu/4.4/../../../../include/c++/4.4/exception:148:
// /usr/lib/gcc/i486-linux-gnu/4.4/../../../../include/c++/4.4/exception_ptr.h:143:13: error: 
//       unknown type name 'type_info'
//       const type_info*
//             ^
// 1 error generated.

#include <set>
#include <vector>
#include <list>
#include <typeinfo>
#include <type_traits>

using namespace std;

// Remove elements from bc which are missing from ac.
//
// TODO(pts): Slow if bc is a vector.
template<typename Input, typename Output>
static void IntersectionUpdate(const Input &ac, Output *bc) {
  static_assert(std::is_same<typename Input::value_type,
                             typename Output::value_type>::value,
                "the containers passed to Process() need to "
                "have the same value_type");
  typename Input::const_iterator a = ac.begin();
  const typename Input::const_iterator a_end = ac.end();
  typename Output::iterator b = bc->begin();
  // Can't be a const interator, similarly to b_old.
  const typename Output::iterator b_end = bc->begin();
  while (a != a_end && b != b_end) {
    if (*a < *b) {
      ++a;
    } else if (*a > *b) {
      // set<...>::const_iterator, multiset<...>::const_iterator would work,
      // but not for list or vector.
      const typename Output::iterator b_old = b++;
      bc->erase(b_old);  // erase doesn't invalidate b.
    } else {  // Elements are equal, keep them in the intersection.
      ++a;
      ++b;
    }
  }
  bc->erase(b, b_end);  // Remove remaining elements in bc but not in ac.
}      

int main(int, char**) {
  set<int> s;
  multiset<int> ms;
  vector<int> v;
  list<int> l;
  IntersectionUpdate(s, &ms);
  IntersectionUpdate(ms, &v);
  IntersectionUpdate(v, &l);
  IntersectionUpdate(l, &s);
  return 0;
}
