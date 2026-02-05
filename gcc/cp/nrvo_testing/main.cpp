#include <mutex>

using std::mutex;

mutex Func(void)
{
  [[nrvo]] mutex m;
  m.lock();
  [[gnu::nrvo]] mutex m2;
  return m;
}

int main(void){}
