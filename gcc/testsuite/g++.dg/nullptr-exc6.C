// Taking the address of a C library function that declares a parameter
// nonnull yields a checked entry point, so a call through the pointer
// throws just as a direct call would.  Without this, the nonnull check
// has nothing to attach to: the pointer may be called from anywhere.
// { dg-do run }
// { dg-options "-fnullptr-exceptions" }

#include <exception>
#include <cstring>

const char *s;			// null

int
main ()
{
  std::size_t (*fp) (const char *) = std::strlen;

  // Two ways of naming the same function must still give the same
  // address, or pointer comparison breaks inside the ABI.
  if (fp != std::strlen)
    return 2;

  try
    {
      std::size_t n = fp (s);
      (void) n;
    }
  catch (const std::nullptr_error &)
    {
      return 0;
    }
  return 1;
}
