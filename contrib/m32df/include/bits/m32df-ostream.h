// Minimal output streams for the -m32df ABI            -*- C++ -*-
// Copyright (C) 2026 Free Software Foundation, Inc.
//
// This file is part of GCC.  Distributed under the terms of the GNU General
// Public License, version 3 or later, with the GCC Runtime Library Exception
// version 3.1.  See COPYING3 and COPYING.RUNTIME.

// libstdc++ is not built for -m32df, so std::cout is unavailable: the object
// lives in libstdc++.a and its construction drags in the locale machinery,
// which needs a hosted C library.  This header supplies just enough of an
// output stream to write diagnostics and demos:
//
//     std::cout << "0x" << std::hex << value << std::endl;
//
// It is deliberately NOT a conforming <ostream>.  There is no streambuf, no
// locale, no formatting of floating point, no wide characters, no input, and
// no exceptions.  Output is unbuffered: each insertion writes immediately.
// Do not mistake it for the real thing -- it exists so that freestanding
// -m32df programs can print.

#ifndef _M32DF_OSTREAM_H
#define _M32DF_OSTREAM_H 1

// From the freestanding C library in libgcc (see libgcc/config/i386/m32df-libc.c).
// The signature matches the platform's, so including <unistd.h> as well is fine.
extern "C" int write (int, const void *, unsigned);

namespace std
{
  class ostream;

  typedef ostream& (*__m32df_manip) (ostream&);

  class ostream
  {
  public:
    explicit ostream (int __fd) noexcept
      : _M_fd (__fd), _M_base (10), _M_width (0), _M_fill (' '),
	_M_upper (false), _M_showbase (false) { }

    ostream (const ostream&) = delete;
    ostream& operator= (const ostream&) = delete;

    // --- state, as far as it goes -------------------------------------
    unsigned base () const noexcept { return _M_base; }
    void base (unsigned __b) noexcept { _M_base = __b; }
    unsigned width () const noexcept { return _M_width; }
    unsigned width (unsigned __w) noexcept
    { unsigned __o = _M_width; _M_width = __w; return __o; }
    char fill () const noexcept { return _M_fill; }
    char fill (char __c) noexcept
    { char __o = _M_fill; _M_fill = __c; return __o; }
    void uppercase (bool __u) noexcept { _M_upper = __u; }
    bool showbase () const noexcept { return _M_showbase; }
    void showbase (bool __s) noexcept { _M_showbase = __s; }

    // --- raw output ----------------------------------------------------
    ostream& put (char __c) noexcept
    { ::write (_M_fd, &__c, 1); return *this; }

    ostream& write (const char *__s, unsigned __n) noexcept
    { ::write (_M_fd, __s, __n); return *this; }

    ostream& flush () noexcept { return *this; }   // unbuffered already

    // --- insertion -----------------------------------------------------
    ostream& operator<< (const char *__s) noexcept
    {
      unsigned __n = 0;
      if (!__s)
	__s = "(null)";
      while (__s[__n])
	++__n;
      _M_pad (__n);
      return write (__s, __n);
    }

    ostream& operator<< (char __c) noexcept { _M_pad (1); return put (__c); }

    ostream& operator<< (bool __b) noexcept
    { return *this << (__b ? "true" : "false"); }

    ostream& operator<< (short __v) noexcept { return _M_signed (__v); }
    ostream& operator<< (int __v) noexcept { return _M_signed (__v); }
    ostream& operator<< (long __v) noexcept { return _M_signed (__v); }
    ostream& operator<< (long long __v) noexcept { return _M_signed (__v); }

    ostream& operator<< (unsigned short __v) noexcept { return _M_unsigned (__v); }
    ostream& operator<< (unsigned int __v) noexcept { return _M_unsigned (__v); }
    ostream& operator<< (unsigned long __v) noexcept { return _M_unsigned (__v); }
    ostream& operator<< (unsigned long long __v) noexcept { return _M_unsigned (__v); }

    // A data pointer is 4 bytes here; a function pointer is 8 and does not
    // convert implicitly, so it must be cast before printing.
    ostream& operator<< (const void *__p) noexcept
    {
      unsigned __b = _M_base;
      bool __s = _M_showbase;
      _M_base = 16; _M_showbase = true;
      _M_unsigned ((unsigned long long) (unsigned) __p);
      _M_base = __b; _M_showbase = __s;
      return *this;
    }

    ostream& operator<< (__m32df_manip __m) noexcept { return __m (*this); }

  private:
    ostream& _M_signed (long long __v) noexcept
    {
      if (__v < 0 && _M_base == 10)
	{
	  char __buf[24];
	  unsigned __n = _M_digits ((unsigned long long) (-__v), __buf);
	  _M_pad (__n + 1);
	  put ('-');
	  return write (__buf, __n);
	}
      return _M_unsigned ((unsigned long long) __v);
    }

    ostream& _M_unsigned (unsigned long long __v) noexcept
    {
      char __buf[24];
      unsigned __n = _M_digits (__v, __buf);
      unsigned __pre = (_M_showbase && _M_base == 16) ? 2u : 0u;
      _M_pad (__n + __pre);
      if (__pre)
	write (_M_upper ? "0X" : "0x", 2);
      return write (__buf, __n);
    }

    // Render into BUF, most significant digit first; return the length.
    // Division is done by shift-and-subtract so that this header needs no
    // 64-bit division helper.
    unsigned _M_digits (unsigned long long __v, char *__buf) const noexcept
    {
      const char *__d = _M_upper ? "0123456789ABCDEF" : "0123456789abcdef";
      char __tmp[24];
      unsigned __n = 0;

      if (__v == 0)
	__tmp[__n++] = '0';
      while (__v)
	{
	  unsigned long long __q = 0;
	  unsigned __rem = 0;
	  for (int __i = 0; __i < 64; ++__i)
	    {
	      unsigned __bit = (unsigned) (__v >> 63);
	      __v <<= 1;
	      __q <<= 1;
	      __rem = (__rem << 1) | __bit;
	      if (__rem >= _M_base)
		{
		  __rem -= _M_base;
		  __q |= 1;
		}
	    }
	  __tmp[__n++] = __d[__rem];
	  __v = __q;
	}
      for (unsigned __i = 0; __i < __n; ++__i)
	__buf[__i] = __tmp[__n - 1 - __i];
      return __n;
    }

    // setw applies to the next insertion only, as in a real stream.
    void _M_pad (unsigned __len) noexcept
    {
      unsigned __w = _M_width;
      _M_width = 0;
      while (__w-- > __len)
	put (_M_fill);
    }

    int _M_fd;
    unsigned _M_base;
    unsigned _M_width;
    char _M_fill;
    bool _M_upper;
    bool _M_showbase;
  };

  // --- manipulators ----------------------------------------------------
  inline ostream& dec (ostream& __o) noexcept { __o.base (10); return __o; }
  inline ostream& hex (ostream& __o) noexcept { __o.base (16); return __o; }
  inline ostream& oct (ostream& __o) noexcept { __o.base (8); return __o; }
  inline ostream& uppercase (ostream& __o) noexcept { __o.uppercase (true); return __o; }
  inline ostream& nouppercase (ostream& __o) noexcept { __o.uppercase (false); return __o; }
  inline ostream& showbase (ostream& __o) noexcept { __o.showbase (true); return __o; }
  inline ostream& noshowbase (ostream& __o) noexcept { __o.showbase (false); return __o; }
  inline ostream& endl (ostream& __o) noexcept { return __o.put ('\n'); }
  inline ostream& ends (ostream& __o) noexcept { return __o.put ('\0'); }
  inline ostream& flush (ostream& __o) noexcept { return __o.flush (); }

  // Printing a function pointer.
  //
  // A real std::ostream has no overload for one, so `cout << f' converts to
  // bool and prints "1" -- a well known trap.  Here a function pointer is
  // eight bytes and does not convert to const void * at all, so it would
  // otherwise still decay to bool.  Print the whole 64-bit value instead: the
  // low half is the code address and the high half is the carried stack and
  // ABI tag, so a plain function shows as 0x...0 in the upper word and one
  // that carries a stack shows the stack there.
  //
  // The template matches exactly, so it is preferred over the conversion to
  // bool.  Pointers to member functions are not covered and still decay.

  template<typename _Ret, typename... _Args>
    inline ostream&
    operator<< (ostream& __o, _Ret (*__f) (_Args...)) noexcept
    {
      unsigned __base = __o.base ();
      bool __show = __o.showbase ();
      char __fill = __o.fill ();

      // Zero-padded to all 16 digits so the two halves line up: the low eight
      // are the code address, the high eight the carried stack and ABI tag.
      __o.showbase (false);
      __o.base (16);
      __o.write ("0x", 2);
      __o.fill ('0');
      __o.width (16);
      __o << (unsigned long long) (__UINTFPTR_TYPE__) __f;

      __o.fill (__fill);
      __o.base (__base);
      __o.showbase (__show);
      return __o;
    }

#if defined(__cpp_noexcept_function_type) && __cpp_noexcept_function_type >= 201510L
  // Since C++17 noexcept is part of the type, so it needs its own overload.
  template<typename _Ret, typename... _Args>
    inline ostream&
    operator<< (ostream& __o, _Ret (*__f) (_Args...) noexcept) noexcept
    {
      return __o << (_Ret (*) (_Args...)) __f;
    }
#endif

  // Constructed on first use, so no static initialisation order problem.
  inline ostream& __m32df_cout () noexcept { static ostream __s (1); return __s; }
  inline ostream& __m32df_cerr () noexcept { static ostream __s (2); return __s; }

  static ostream& cout __attribute__((__unused__)) = __m32df_cout ();
  static ostream& cerr __attribute__((__unused__)) = __m32df_cerr ();
  static ostream& clog __attribute__((__unused__)) = __m32df_cerr ();
}

#endif // _M32DF_OSTREAM_H
