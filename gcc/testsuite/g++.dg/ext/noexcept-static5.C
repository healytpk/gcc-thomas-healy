// "extern noexcept(static)" declaration regions.
// { dg-do compile { target c++11 } }
// { dg-options "" }

void may_throw ();
void wont_throw () noexcept;

extern noexcept(static) {

// A declaration in the region is implicitly noexcept.
void decl ();
static_assert (noexcept (decl ()), "declaration should be noexcept");

// A definition in the region is implicitly noexcept(static).
void good_def ()
{
  wont_throw ();
}

void bad_def ()
{
  may_throw ();				// { dg-error "not .noexcept." }
}

// An explicit exception-specification always wins, so this opts out.
void opted_out () noexcept(false)
{
  may_throw ();
}

// Function pointers, parameters and typedefs are not touched, so ordinary
// C callbacks still work.
typedef void callback_t ();
static_assert (!noexcept (((callback_t *) 0) ()), "typedef untouched");

void takes_ptr (void (*p) ());
void uses_ptr (void (*p) ())
{
  p ();					// { dg-error "not .noexcept." }
}

// Regions nest, and combine with a linkage-specification.
extern "C" noexcept(static) {
  void c_callback (void *)
  {
    wont_throw ();
  }
}

}  // extern noexcept(static)

// Outside the region nothing changes.
void outside ()
{
  may_throw ();
}

// The other spelling, with the linkage-specification first.
extern "C" noexcept(static) {
void c_bad ()
{
  may_throw ();				// { dg-error "not .noexcept." }
}
}
