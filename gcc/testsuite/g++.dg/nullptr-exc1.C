// Basic -fnullptr-exceptions test: dereferencing a null pointer throws
// std::nullptr_error, and the exception is catchable.
// { dg-do run }
// { dg-options "-fnullptr-exceptions" }

#include <exception>
#include <cstring>

int *p;

int
main ()
{
  try
    {
      return *p;
    }
  catch (const std::nullptr_error &e)
    {
      if (std::strcmp (e.what (), "nullptr dereference") != 0)
	return 2;
      return 0;
    }
  return 1;
}
