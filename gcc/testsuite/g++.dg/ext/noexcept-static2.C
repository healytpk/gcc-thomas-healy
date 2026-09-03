// "noexcept (static)" is only permitted on a function definition.
// { dg-do compile { target c++11 } }
// { dg-options "" }

void wont_throw () noexcept;

void f () noexcept(static);	// { dg-error "only permitted" }

struct S
{
  void g () noexcept(static);	// { dg-error "only permitted" }
  void h () noexcept(static) { wont_throw (); }		// OK
};

// The declaration uses plain "noexcept"; the definition may use the
// extension.
void ok () noexcept;
void ok () noexcept(static)
{
  wont_throw ();
}

typedef void (*fp) () noexcept(static);		// { dg-error "only permitted" }

void param (void (*p) () noexcept(static));	// { dg-error "only permitted" }
