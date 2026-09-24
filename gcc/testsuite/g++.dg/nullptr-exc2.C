// The throw inserted by -fnullptr-exceptions must be placed in the EH
// region enclosing the guarded statement, so that local cleanups run as
// the exception propagates.  This is what the instrumentation would get
// wrong if it ran after pass_lower_eh.
// { dg-do run }
// { dg-options "-fnullptr-exceptions" }

#include <exception>

int destroyed;

struct D
{
  ~D () { ++destroyed; }
};

int *p;

static void
inner ()
{
  D d;
  *p = 1;
}

static void
outer ()
{
  D d;
  inner ();
}

int
main ()
{
  try
    {
      outer ();
    }
  catch (const std::nullptr_error &)
    {
      // Both D objects must have been destroyed during unwinding.
      return destroyed == 2 ? 0 : 1;
    }
  return 2;
}
