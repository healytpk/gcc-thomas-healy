// The address of a library function has to mean the same thing in static
// data as it does in a statement, or the same expression would denote
// different things depending on where it was written.
// { dg-do run }
// { dg-options "-fnullptr-exceptions" }

#include <exception>
#include <cstring>

const char *s;			// null

std::size_t (*file_scope) (const char *) = std::strlen;

int
main ()
{
  std::size_t (*local) (const char *) = std::strlen;

  if (file_scope != local)
    return 2;

  try
    {
      std::size_t n = file_scope (s);
      (void) n;
    }
  catch (const std::nullptr_error &)
    {
      return 0;
    }
  return 1;
}
