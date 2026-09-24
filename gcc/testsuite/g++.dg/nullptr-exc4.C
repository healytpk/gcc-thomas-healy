// A null dereference inside a noexcept function terminates, as any other
// throw from a noexcept function does.
// { dg-do run { target c++11 } }
// { dg-options "-fnullptr-exceptions" }
// { dg-shouldfail "std::terminate" }

int *p;

static void
f () noexcept
{
  *p = 1;
}

int
main ()
{
  f ();
  return 0;
}
