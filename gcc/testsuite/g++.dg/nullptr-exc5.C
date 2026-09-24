// A null pointer passed to a nonnull parameter throws in the caller's
// frame, before the callee is entered.  This is what covers the C library,
// whose string routines are hand-written assembly: there is nothing there
// to instrument, and nothing to throw out of.
// { dg-do run }
// { dg-options "-fnullptr-exceptions" }

#include <exception>
#include <cstring>

char *null_dst;
const char *src = "x";

int
main ()
{
  // memcpy is nonnull only when the length is non-zero, and that
  // condition has to be honored.
  std::memcpy (null_dst, null_dst, 0);

  try
    {
      std::strcpy (null_dst, src);
    }
  catch (const std::nullptr_error &)
    {
      return 0;
    }
  return 1;
}
