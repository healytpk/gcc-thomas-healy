// "noexcept (static)" on lambdas and templates.
// { dg-do compile { target c++11 } }
// { dg-options "" }

void may_throw ();
void wont_throw () noexcept;

void lambdas ()
{
  auto good = [] () noexcept(static) { wont_throw (); };
  auto bad = [] () noexcept(static) { may_throw (); };	// { dg-error "not .noexcept." }
  good ();
  bad ();

  // A lambda that may throw is fine inside a noexcept(static) function so
  // long as it is not called there.
  auto thrower = [] () { may_throw (); };
  (void) thrower;
}

template <typename T>
void tmpl_ok () noexcept(static)
{
  T t;
  (void) t;
  wont_throw ();
}

template <typename T>
void tmpl_bad () noexcept(static)
{
  may_throw ();				// { dg-error "not .noexcept." }
}

void use () { tmpl_ok<int> (); tmpl_bad<int> (); }
