/* A null dereference in C throws rather than faulting.  C has no catch of
   its own, so nothing here handles it, and an exception with no handler
   reaches std::terminate -- the same outcome C++ gives.

   Note that the cleanup handler of a C frame cannot be observed running by
   a C-only test: with no handler anywhere, the standard permits
   terminating without unwinding at all, and GCC does, so phase one of the
   search fails and no cleanup runs.  Observing that requires a C++ frame
   with a handler further up the stack.  */
/* { dg-do run } */
/* { dg-options "-fnullptr-exceptions" } */
/* { dg-additional-options "-lsupc++" } */
/* { dg-shouldfail "uncaught std::nullptr_error" } */
/* { dg-output "terminate called after throwing an instance of" } */

volatile int *p;

int
main (void)
{
  return *p;
}
