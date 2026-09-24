// Each of the three situations throws its own type, so a handler can name
// the one it cares about, and all three derive from std::nullptr_error so
// a handler can still catch them together.
// { dg-do run }
// { dg-options "-fnullptr-exceptions" }

#include <exception>
#include <cstring>

int *p;				// null
char *q;			// null
char *nulldst;			// null
const char *src = "x";
int one = 1;

// In each of these the derived handler comes first, so reaching the
// std::nullptr_error handler would mean the specific type was not thrown.

static int
dereference_throws_its_own_type ()
{
  try
    {
      return *p;
    }
  catch (const std::nullptr_dereference &) { return 0; }
  catch (const std::nullptr_error &) { return 1; }
  return 2;
}

static int
arithmetic_throws_its_own_type ()
{
  try
    {
      char *r = q + one;
      (void) r;
    }
  catch (const std::nullptr_arithmetic &) { return 0; }
  catch (const std::nullptr_error &) { return 1; }
  return 2;
}

static int
argument_throws_its_own_type ()
{
  try
    {
      std::strcpy (nulldst, src);
    }
  catch (const std::nullptr_argument &) { return 0; }
  catch (const std::nullptr_error &) { return 1; }
  return 2;
}

// The base still catches a derived one, and carries its message.

static int
base_catches_a_derived_one ()
{
  try
    {
      return *p;
    }
  catch (const std::nullptr_error &e)
    {
      return std::strcmp (e.what (), "nullptr dereference") == 0 ? 0 : 1;
    }
  return 2;
}

// And so does std::exception, two levels up.

static int
std_exception_catches_it_too ()
{
  try
    {
      return *p;
    }
  catch (const std::exception &) { return 0; }
  return 1;
}

int
main ()
{
  if (dereference_throws_its_own_type ())
    return 1;
  if (arithmetic_throws_its_own_type ())
    return 2;
  if (argument_throws_its_own_type ())
    return 3;
  if (base_catches_a_derived_one ())
    return 4;
  if (std_exception_catches_it_too ())
    return 5;
  return 0;
}
