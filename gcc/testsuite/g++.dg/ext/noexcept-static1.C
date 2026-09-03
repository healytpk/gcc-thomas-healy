// Test the GNU "noexcept (static)" extension: the body is checked.
// { dg-do compile { target c++11 } }
// { dg-options "" }

void may_throw ();
void wont_throw () noexcept;

struct ThrowingCtor { ThrowingCtor (); };

void ok () noexcept(static)
{
  wont_throw ();
}

void bad_call () noexcept(static)
{
  may_throw ();				// { dg-error "not .noexcept." }
}

void bad_throw () noexcept(static)
{
  throw 1;				// { dg-error "throw" }
}

void bad_ctor () noexcept(static)
{
  ThrowingCtor c;			// { dg-error "not .noexcept." }
  (void) c;
}

void indirect (void (*p) ()) noexcept(static)
{
  p ();					// { dg-error "not .noexcept." }
}

void indirect_ok (void (*p) () noexcept) noexcept(static)
{
  p ();
}

void caught () noexcept(static)
{
  try { may_throw (); } catch (...) { }
}

// The cleanup that ends a handler calls __cxa_end_catch, which
// do_end_catch leaves potentially-throwing when the caught type's
// destructor is not known to be non-throwing -- always, for catch(...).
// It is compiler-generated, so it must not be diagnosed.
struct Big { char pad[64]; };

void caught_typed_object () noexcept(static)
{
  try { may_throw (); } catch (const Big &) { } catch (...) { }
}

void caught_typed () noexcept(static)
{
  // A typed handler is not enough: the try-block may throw something else.
  try { may_throw (); }			// { dg-error "not .noexcept." }
  catch (int) { }
}

void rethrown () noexcept(static)
{
  try { may_throw (); }
  catch (...) { throw; }		// { dg-error "throw" }
}

// An unevaluated operand cannot throw.
void unevaluated () noexcept(static)
{
  static_assert (!noexcept (may_throw ()), "");
  (void) sizeof (may_throw ());
}

// A function-try-block on an ordinary function: reaching the end of the
// handler just returns, so the catch-all does stop everything.
void fn_try () noexcept(static)
try { may_throw (); }
catch (...) { }

// But reaching the end of a handler of a constructor's or destructor's
// function-try-block rethrows, so the catch-all stops nothing.
struct CtorFnTry
{
  CtorFnTry () noexcept(static)
  try { may_throw (); }			// { dg-error "not .noexcept." }
  catch (...) { }
};

struct DtorFnTry
{
  ~DtorFnTry () noexcept(static)
  try { may_throw (); }			// { dg-error "not .noexcept." }
  catch (...) { }
};

// The body of a noexcept function is wrapped in a MUST_NOT_THROW_EXPR by
// begin_eh_spec_block.  Since noexcept(static) implies noexcept, that is
// the region we have to look inside, not skip over.  Without that, every
// check above silently passes.
void not_skipped () noexcept(static)
{
  may_throw ();				// { dg-error "not .noexcept." }
}
