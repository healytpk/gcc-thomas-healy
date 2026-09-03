// "noexcept (static)" and new-expressions.
// { dg-do compile { target c++11 } }
// { dg-options "" }

#include <new>

struct Trivial { int i; };
struct ThrowingCtor { ThrowingCtor (); };
struct NoexceptCtor { NoexceptCtor () noexcept; };

// Plain new can throw std::bad_alloc, even for a trivial type, so it is
// rejected.  This matches noexcept (new Trivial) being false.
void plain_new () noexcept(static)
{
  Trivial *p = new Trivial;		// { dg-error "not .noexcept." }
  delete p;
}

void array_new () noexcept(static)
{
  int *p = new int[4];			// { dg-error "not .noexcept." }
  delete[] p;
}

// The nothrow forms are declared noexcept, so the allocation is fine.
void nothrow_new () noexcept(static)
{
  Trivial *p = new (std::nothrow) Trivial;
  delete p;
}

void nothrow_new_good_ctor () noexcept(static)
{
  NoexceptCtor *p = new (std::nothrow) NoexceptCtor;
  delete p;
}

// ... but the constructor still has to be non-throwing.
void nothrow_new_bad_ctor () noexcept(static)
{
  ThrowingCtor *p			// { dg-error "not .noexcept." }
    = new (std::nothrow) ThrowingCtor;
  delete p;
}

// Placement new is noexcept.
void placement (void *buf) noexcept(static)
{
  NoexceptCtor *p = new (buf) NoexceptCtor;
  (void) p;
}

// operator delete is noexcept and a trivial destructor cannot throw.
void plain_delete (Trivial *p) noexcept(static)
{
  delete p;
}

struct ThrowingDtor { ~ThrowingDtor () noexcept(false); };
struct VirtualThrowingDtor { virtual ~VirtualThrowingDtor () noexcept(false); };

// operator delete is noexcept, but the destructor it calls first is not.
void delete_throwing_dtor (ThrowingDtor *p) noexcept(static)
{
  delete p;				// { dg-error "not .noexcept." }
}

// The same through the vtable.  Here the callee is not a FUNCTION_DECL,
// so the diagnostic names the function type rather than the destructor.
void delete_virtual_throwing_dtor (VirtualThrowingDtor *p) noexcept(static)
{
  delete p;				// { dg-error "not .noexcept." }
}

// The implicit destructor call for a local is checked too.  The
// declaration and the end of scope are on one line so that this does not
// depend on which of the two the cleanup's location resolves to.
void local_throwing_dtor () noexcept(static)
{ ThrowingDtor d; (void) d; }		// { dg-error "not .noexcept." }

// A class-specific operator delete gets an implicit non-throwing
// exception specification ([except.spec]/9), so this is accepted.
struct OwnDelete
{
  static void operator delete (void *);
};

void delete_own (OwnDelete *p) noexcept(static)
{
  delete p;
}
