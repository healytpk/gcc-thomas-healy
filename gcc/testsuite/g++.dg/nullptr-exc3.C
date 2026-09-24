// Pointer arithmetic on a null pointer throws, but adding zero does not:
// that case is well defined in C++ and ordinary code relies on it.
// { dg-do run }
// { dg-options "-fnullptr-exceptions" }

#include <exception>
#include <cstring>

char *p;
int zero = 0;
int one = 1;

int
main ()
{
  // p + 0 must not throw, and must still compare equal to null.
  char *q = p + zero;
  if (q != 0)
    return 1;

  // An empty range whose two ends are both null must not throw either.
  if (p != p + zero)
    return 2;

  try
    {
      char *r = p + one;
      (void) r;
    }
  catch (const std::nullptr_error &e)
    {
      if (std::strcmp (e.what (), "nullptr arithmetic") != 0)
	return 4;
      return 0;
    }
  return 3;
}
