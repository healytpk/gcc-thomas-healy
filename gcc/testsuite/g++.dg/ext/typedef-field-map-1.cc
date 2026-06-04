// Tests for the typedef<> / typedef<tmpl> field-mapping extension.
// { dg-do compile }
// { dg-options "-std=c++17" }

#include <optional>
#include <string>
#include <vector>
#include <memory>

// ── 1. Basic typedef<tmpl> mapping ──────────────────────────────────────────

struct Basic {
  int    a;
  double b;
};

typedef< std::optional > Basic OptBasic;

void test_basic ()
{
  OptBasic o;
  o.a = std::optional<int>   {42};
  o.b = std::optional<double>{3.14};
}

// ── 2. Identity typedef<> (newtype / strong typedef) ────────────────────────

struct Metres { double value; };

typedef<> Metres Feet;

void test_identity ()
{
  Feet f;
  f.value = 1.0;
  // Metres m = f;  -- would be ill-formed; tested in error suite
}

// ── 3. Multiple fields including std::string ─────────────────────────────────

struct Person {
  int         age;
  std::string name;
  double      salary;
};

typedef< std::optional > Person MaybePerson;

void test_person ()
{
  MaybePerson mp;
  mp.age    = std::optional<int>{30};
  mp.name   = std::optional<std::string>{"Alice"};
  mp.salary = std::optional<double>{50000.0};
}

// ── 4. Inherited member variables are included ───────────────────────────────

struct Base {
  int x;
};
struct Derived : Base {
  double y;
};

typedef< std::optional > Derived OptDerived;

void test_inheritance ()
{
  OptDerived od;
  od.x = std::optional<int>   {1};   // from Base
  od.y = std::optional<double>{2.0}; // from Derived
}

// ── 5. Multiple inheritance ──────────────────────────────────────────────────

struct MixinA { int   ma; };
struct MixinB { float mb; };
struct Multi  : MixinA, MixinB { char mc; };

typedef< std::optional > Multi OptMulti;

void test_multi_inheritance ()
{
  OptMulti om;
  om.ma = std::optional<int>  {1};
  om.mb = std::optional<float>{2.f};
  om.mc = std::optional<char> {'x'};
}

// ── 6. Virtual inheritance — vx must appear exactly once ────────────────────

struct VBase   { int vx; };
struct VLeft   : virtual VBase { int lx; };
struct VRight  : virtual VBase { int rx; };
struct Diamond : VLeft, VRight { int dx; };

typedef< std::optional > Diamond OptDiamond;

void test_virtual_inheritance ()
{
  OptDiamond od;
  od.vx = std::optional<int>{0};
  od.lx = std::optional<int>{1};
  od.rx = std::optional<int>{2};
  od.dx = std::optional<int>{3};
}

// ── 7. Static members are excluded ───────────────────────────────────────────

struct WithStatic {
  int         instance_field;
  static int  class_field;
};
int WithStatic::class_field = 0;

typedef< std::optional > WithStatic OptWithStatic;

void test_static_excluded ()
{
  OptWithStatic ows;
  ows.instance_field = std::optional<int>{7};
  // ows.class_field would be ill-formed — static was not mapped
}

// ── 8. Nested struct field is wrapped as a whole ─────────────────────────────

struct Inner { int p; double q; };
struct Outer { Inner nested; int flat; };

typedef< std::optional > Outer OptOuter;

void test_nested_struct ()
{
  OptOuter oo;
  oo.nested = std::optional<Inner>{Inner{1, 2.0}}; // Inner wrapped, not recursed
  oo.flat   = std::optional<int>{42};
}

// ── 9. Works at both namespace scope and block scope ─────────────────────────

struct BlockBase { int z; };

void test_block_scope ()
{
  typedef< std::optional > BlockBase OptBlock;
  OptBlock ob;
  ob.z = std::optional<int>{99};

  typedef<> BlockBase StrongBlock;
  StrongBlock sb;
  sb.z = 1;
}

// ── 10. Other templates: std::vector, std::shared_ptr ────────────────────────

struct Scalars { int i; double d; };

typedef< std::vector     > Scalars VecScalars;
typedef< std::shared_ptr > Scalars PtrScalars;

void test_other_templates ()
{
  VecScalars vs;
  vs.i = std::vector<int>   {1, 2, 3};
  vs.d = std::vector<double>{1.0, 2.0};

  PtrScalars ps;
  ps.i = std::make_shared<int>(1);
  ps.d = std::make_shared<double>(2.0);
}
