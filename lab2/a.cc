#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <climits>
#include <map>
#include <sstream>

#define DIR_APP_LOCKID (ULLONG_MAX - 1)

class A {
 public:
  A() : x(2) { x++; }
  int x;
};

int main() {
  // std::map<int, A> m;
  printf("%d\n", DIR_APP_LOCKID);
}
