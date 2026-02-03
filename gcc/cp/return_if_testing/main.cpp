#include <iostream>
using namespace std;

struct CannotMoveCannotCopy {
  int n = 42;
  CannotMoveCannotCopy(int const arg) : n(arg) {}
  CannotMoveCannotCopy(CannotMoveCannotCopy const & ) = delete;
  CannotMoveCannotCopy(CannotMoveCannotCopy       &&) = delete;
  explicit operator bool(void) const
  {
	  static unsigned i = 0u;
	  return i++;
  }
};

int Func1(void)
{
  return 0;
}

int *Func2(void)
{
  static int x = 56;
  return &x;
}

CannotMoveCannotCopy Func3(void)
{
	return CannotMoveCannotCopy(476);
}

CannotMoveCannotCopy Func4(void)
{
	return if Func3();
	return if Func3();
	return CannotMoveCannotCopy(1242);
}

int main(void)
{
   cout << "First line in main\n";

   return if Func1();

   cout << "Middle line in main\n";

   return * if Func2();

   Func4();

   cout << "Last line in main\n";
}
