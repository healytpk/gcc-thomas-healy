// Nesting of "extern noexcept(static)" regions.
// { dg-do compile { target c++11 } }
// { dg-options "" }

void may_throw ();

extern "C" noexcept(static) {

void a ()
{
  may_throw ();				// { dg-error "not .noexcept." }
}

// A nested region with no specifier of its own inherits the enclosing
// one: changing the linkage does not turn the check off.
extern "C++" {
  void b ()
  {
    may_throw ();			// { dg-error "not .noexcept." }
  }
}

// !noexcept(static) is how a nested region exempts itself, so that a header
// included inside the outer region is left alone.
extern "C++" !noexcept(static) {
  void c ()
  {
    may_throw ();
  }
  void c_decl ();
  static_assert (!noexcept (c_decl ()), "should not be noexcept");
}

// ... and it nests back on again.
extern "C++" !noexcept(static) {
  extern noexcept(static) {
    void d ()
    {
      may_throw ();			// { dg-error "not .noexcept." }
    }
  }
}

}

// The specifier is restored on the way out.
void outside ()
{
  may_throw ();
}

// The escaper also works without a linkage-specification.
extern noexcept(static) {
  extern !noexcept(static) {
    void e ()
    {
      may_throw ();
    }
  }
  void f ()
  {
    may_throw ();			// { dg-error "not .noexcept." }
  }
}
