// <bits/chimeric_ptr.h> -*- C++ -*-
//
// Chimeric pointer type.
//
// This file is part of the GNU ISO C++ Library.
// Copyright (C) 2025 Free Software Foundation, Inc.
//
// This file is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// This file is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma GCC system_header

#if __cplusplus >= 202302L

#ifndef _GLIBCXX_CHIMERIC_PTR_H
#define _GLIBCXX_CHIMERIC_PTR_H 1

#include <cstddef>                  // nullptr_t, size_t
#include <tuple>                    // get, tuple, tuple_element, tuple_size
#include <type_traits>
#include <utility>                  // declval, index_sequence, make_index_sequence, to_underlying

namespace std _GLIBCXX_VISIBILITY(default) {
_GLIBCXX_BEGIN_NAMESPACE_VERSION

namespace chimeric_detail {

  // Next three lines: Get class from pointer-to-member (can be function or variable)
  template<typename T                > struct __member_ptr_class_S;
  template<typename T, typename Class> struct __member_ptr_class_S< T Class::* > { typedef Class type; };
  template<typename T                > requires is_member_pointer_v<T> using __member_ptr_class = typename __member_ptr_class_S<T>::type;

  // Check if Derived inherits from Base,
  // and that the CV-qualifiers are compatible
  // for a conversion.
  template<class Derived, class Base>
  constexpr bool derived_fromCV = is_base_of_v<Base, Derived> && is_convertible_v<Derived*, Base*>;

  template<class ToRef, class From>
  requires is_lvalue_reference_v<ToRef> && (!is_reference_v<From>)
           && derived_fromCV< From, remove_reference_t<ToRef> >
  [[nodiscard]] constexpr ToRef static_upcast(From &arg) noexcept
  {
    // The whole point of this function is that
    // it will never perform a downcast (even
    // though 'static_cast' can perform downcasts
    // which might result in undefined behaviour)
    return static_cast<ToRef>(arg);
  }

  template<class ToRef, class From>
  requires is_lvalue_reference_v<ToRef> && (!is_reference_v<From>)
  consteval bool is_noexcept_user_conversion(void) noexcept
  {
    typedef remove_cvref_t    <ToRef> To  ;
    typedef remove_reference_t<ToRef> ToCV;

    if constexpr (requires(From &f) { f.operator ToCV&(); })  // Prefer exact match first (matches C++ "best viable" more closely)
      return noexcept(declval<From&>().operator ToCV&());
    else if constexpr (requires(From &f) { f.operator To&(); })
      return noexcept(declval<From&>().operator To&());
    else if constexpr (requires(From &f) { f.operator To const&(); } && is_convertible_v<To const*, ToCV*>)
      return noexcept(declval<From&>().operator To const&());
    else if constexpr (requires(From &f) { f.operator To volatile&(); } && is_convertible_v<To volatile*, ToCV*>)
      return noexcept(declval<From&>().operator To volatile&());
    else if constexpr (requires(From &f) { f.operator To const volatile&(); } && is_convertible_v<To const volatile*, ToCV*>)
      return noexcept(declval<From&>().operator To const volatile&());
    else
      static_assert(false, "constraints on user_conversion should prevent this from being reached -- you have a bug");
  }

  template<class ToRef, class From>
  requires   is_lvalue_reference_v<ToRef> &&
           (!is_reference_v<From>) &&
           (
              requires { declval<From&>().operator remove_cvref_t<ToRef>               &(); } ||
             (requires { declval<From&>().operator remove_cvref_t<ToRef> const         &(); } && is_convertible_v< remove_cvref_t<ToRef> const         *, remove_reference_t<ToRef>* >) ||
             (requires { declval<From&>().operator remove_cvref_t<ToRef>       volatile&(); } && is_convertible_v< remove_cvref_t<ToRef>       volatile*, remove_reference_t<ToRef>* >) ||
             (requires { declval<From&>().operator remove_cvref_t<ToRef> const volatile&(); } && is_convertible_v< remove_cvref_t<ToRef> const volatile*, remove_reference_t<ToRef>* >)
           )
  constexpr ToRef user_conversion(From &arg) noexcept( is_noexcept_user_conversion<ToRef,From>() )
  {
    typedef remove_cvref_t    <ToRef> To  ;
    typedef remove_reference_t<ToRef> ToCV;

    if constexpr (requires { arg.operator ToCV&(); })  // Prefer exact match first (matches C++ "best viable" more closely)
      return arg.operator ToCV&();
    else if constexpr (requires { arg.operator To&(); })
      return arg.operator To&();
    else if constexpr (requires { arg.operator To const&(); } && is_convertible_v<To const*, ToCV*>)
      return arg.operator To const&();
    else if constexpr (requires { arg.operator To volatile&(); } && is_convertible_v<To volatile*, ToCV*>)
      return arg.operator To volatile&();
    else if constexpr (requires { arg.operator To const volatile&(); } && is_convertible_v<To const volatile*, ToCV*>)
      return arg.operator To const volatile&();
    else
      static_assert(false, "control should never reach here -- should be prevented by constraints -- you have a bug");
  }

  template <typename T>
  concept option = requires { typename T::__tag_chimera_option; }; // || is_same_v< T, std::reuse_stdlib_type >;

  // Canonicalise for duplicate checks but ignore const/volatile
  template<class T, class... Us>
  struct none_same_canon
    : bool_constant< (!is_same_v< remove_cv_t<T>, remove_cv_t<Us> > && ...) > {};

  template<class... Ts>
  struct unique_canon : true_type {};

  template<class T0, class... TRest>
  struct unique_canon<T0, TRest...>
    : bool_constant< none_same_canon<T0, TRest...>::value
                     && unique_canon<TRest...>::value > {};

  template<class... Ts>
  struct has_no_options
    : bool_constant< (!option<Ts> && ...) > {};

  // Enforce: All option types must precede all interface types
  template<class... Ts>
  struct options_then_interfaces : true_type {};

  template<class T0, class... TRest>
  struct options_then_interfaces<T0, TRest...>
    : bool_constant<
      option<T0>
      ? options_then_interfaces<TRest...>::value
      : has_no_options<TRest...>::value > {};

  // Convert parameter pack to tuple
  template<class T, class Tuple>
  struct tuple_prepend;
  template<class T, class... Ts>
  struct tuple_prepend< T, tuple<Ts...> >
  { using type = tuple<T, Ts...>; };

  // Filter interfaces out of parameter pack
  template<class... InterfacesAndOptions>
  struct filter_interfaces;
  template<> struct filter_interfaces<> { using type = tuple<>; };
  template<class I0, class... Rest>
  struct filter_interfaces<I0, Rest...>
  {
    using tail = typename filter_interfaces<Rest...>::type;
    using type = conditional_t<
      option<I0>,
      tail,
      typename tuple_prepend<I0, tail>::type >;
  };

  // Filter options out of parameter pack
  template<class... InterfacesAndOptions>
  struct filter_options;
  template<> struct filter_options<> { using type = tuple<>; };
  template<class I0, class... Rest>
  struct filter_options<I0, Rest...>
  {
    using tail = typename filter_options<Rest...>::type;
    using type = conditional_t<
      option<I0>,
      typename tuple_prepend<I0, tail>::type,
      tail>;
  };

  // Convert tuple into tuple of pointers
  template<class Tuple>
  struct tuple_pointerize;
  template<class... Ts>
  struct tuple_pointerize< tuple<Ts...> >
  { using type = tuple<Ts*...>; };

  template<class B, class Tuple>
  struct tuple_contains_type : false_type {};
  template<class B, class... Ts>
  struct tuple_contains_type< B, tuple<Ts...> >
    : bool_constant< (is_same_v<B, Ts> || ...) > {};

  template<class B, class Tuple>
  struct tuple_contains_type_derived_fromCV : false_type {};
  template<class B, class... Ts>
  struct tuple_contains_type_derived_fromCV< B, tuple<Ts...> >
    : bool_constant< (derived_fromCV<Ts, B> || ...) > {};

  // Option tuple must have no const/volatile
  template<class Tuple>
  struct tuple_all_uncv : false_type {};
  template<class... Ts>
  struct tuple_all_uncv< tuple<Ts...> >
    : bool_constant< (is_same_v<Ts, remove_cv_t<Ts> > && ...) > {};

  // For alphabetical order: option types define
  //   typedef integral_constant<unsigned, N> __tag_chimera_option;
  template<class T> struct option_key               : integral_constant< unsigned, T::__tag_chimera_option::value > {};
//template<       > struct option_key<reuse_stdlib> : integral_constant< unsigned, 70u                            > {};

  template<class... Ts>
  struct is_option_pack_alphabetical : true_type {};

  template<class T0, class T1, class... Ts>
  struct is_option_pack_alphabetical<T0, T1, Ts...>
    : bool_constant<
        (option_key<T0>::value < option_key<T1>::value)
        && is_option_pack_alphabetical<T1, Ts...>::value > {};

  template<class Tuple>
  struct is_option_tuple_alphabetical : false_type {};

  template<class... Ts>
  struct is_option_tuple_alphabetical< tuple<Ts...> >
    : is_option_pack_alphabetical<Ts...> {};

  template<class Want, class Tuple>
  struct tuple_find_uncv;

  template<class Want>
  struct tuple_find_uncv<Want, tuple<>>
  { using type = void; };

  template<class Want, class T0, class... Ts>
  struct tuple_find_uncv<Want, tuple<T0, Ts...>>
  {
    using type = conditional_t<
      is_same_v<remove_cv_t<T0>, remove_cv_t<Want>>,
      T0,
      typename tuple_find_uncv<Want, tuple<Ts...>>::type>;
  };

  template<class Want, class Tuple>
  using tuple_find_uncv_t = typename tuple_find_uncv<Want, Tuple>::type;

  template<class I, class RhsTuple>
  concept rhs_supplies_interface =
    (!is_void_v<tuple_find_uncv_t<I, RhsTuple>>)
    && is_convertible_v<tuple_find_uncv_t<I, RhsTuple>*, I*>;

  template<class LhsTuple, class RhsTuple>
  concept rhs_supplies_all =
    []<class... Is>(type_identity<tuple<Is...>>) consteval
    {
      return (rhs_supplies_interface<Is, RhsTuple> && ...);
    }(type_identity<LhsTuple>{});

  // Index of T within tuple<Ts...>.
  template<class T, class Tuple>
  struct tuple_index;

  template<class T, class... Ts>
  struct tuple_index<T, tuple<T, Ts...>> : integral_constant<size_t, 0u> {};

  template<class T, class U, class... Ts>
  struct tuple_index<T, tuple<U, Ts...>>
    : integral_constant<size_t, 1u + tuple_index<T, tuple<Ts...>>::value> {};

} // namespace chimeric_detail

struct allow_dynamic    { using __tag_chimera_option = integral_constant<unsigned, 10u>; };
struct bases_only       { using __tag_chimera_option = integral_constant<unsigned, 20u>; };
struct forbid_ambiguity { using __tag_chimera_option = integral_constant<unsigned, 30u>; };
struct many_pointers    { using __tag_chimera_option = integral_constant<unsigned, 40u>; };
struct nothrow_construct{ using __tag_chimera_option = integral_constant<unsigned, 50u>; };
struct nothrow_resolve  { using __tag_chimera_option = integral_constant<unsigned, 60u>; };

template<class... InterfacesAndOptions>
class chimeric_ptr final {

  typedef int __tag_chimera;

  template<class...> friend class chimeric_ptr;

  static_assert(sizeof...(InterfacesAndOptions) > 0u,
                "std::chimeric_ptr : template parameter list cannot be empty");

  static_assert((is_class_v<InterfacesAndOptions> && ...),
                "std::chimeric_ptr : all template parameters must be class types (no pointers, no references)");

  static_assert(chimeric_detail::unique_canon<InterfacesAndOptions...>::value,
                "std::chimeric_ptr : duplicate template parameters are not permitted (after removing const/volatile)");

  static_assert(chimeric_detail::options_then_interfaces<InterfacesAndOptions...>::value,
                "std::chimeric_ptr : all options must precede all interfaces in template parameter list");

  static_assert( !(is_specialization_of<InterfacesAndOptions,chimeric_ptr> || ...),
                "std::chimeric_ptr : cannot be used as a template argument to itself");

  typedef typename chimeric_detail::filter_interfaces<InterfacesAndOptions...>::type ChimericInterfacesTuple_t;
  typedef typename chimeric_detail::filter_options   <InterfacesAndOptions...>::type ChimericOptionsTuple_t   ;

  typedef typename chimeric_detail::tuple_pointerize<ChimericInterfacesTuple_t>::type ChimericInterfacePointersTuple_t;

  static_assert( tuple_size_v<ChimericInterfacesTuple_t> > 0u,
                "std::chimeric_ptr : must have at least one interface");

  static constexpr auto __chimeric_interface_sequence = make_index_sequence< tuple_size_v<ChimericInterfacesTuple_t> >{};

  static_assert(chimeric_detail::tuple_all_uncv<ChimericOptionsTuple_t>::value,
                "std::chimeric_ptr : option types must not be const/volatile");

  static_assert(chimeric_detail::is_option_tuple_alphabetical<ChimericOptionsTuple_t>::value,
                "std::chimeric_ptr : options must be in alphabetical order");

  static constexpr bool
     opt_allow_dynamic     = chimeric_detail::tuple_contains_type<allow_dynamic    , ChimericOptionsTuple_t>::value
    ,opt_bases_only        = chimeric_detail::tuple_contains_type<bases_only       , ChimericOptionsTuple_t>::value
    ,opt_forbid_ambiguity  = chimeric_detail::tuple_contains_type<forbid_ambiguity , ChimericOptionsTuple_t>::value
    ,opt_many_pointers     = chimeric_detail::tuple_contains_type<many_pointers    , ChimericOptionsTuple_t>::value
    ,opt_nothrow_construct = chimeric_detail::tuple_contains_type<nothrow_construct, ChimericOptionsTuple_t>::value
    ,opt_nothrow_resolve   = chimeric_detail::tuple_contains_type<nothrow_resolve  , ChimericOptionsTuple_t>::value
  ;

  static_assert(false == (opt_allow_dynamic && opt_bases_only),
                "std::chimeric_ptr : options 'allow_dynamic' and 'bases_only' cannot be combined");

  static_assert(false == (opt_many_pointers && opt_nothrow_resolve),
                "std::chimeric_ptr : options 'many_pointers' and 'nothrow_resolve' cannot be combined, as the former implies the latter");

  // Two-pointer representation.
  struct alignas( 2u * sizeof(void*) ) __two_ptrs {
    void const volatile *original_class_pointer;
    void const volatile *(*funcptr_get_interface)(void const volatile *, size_t) noexcept(opt_nothrow_resolve);
  };

  conditional_t<opt_many_pointers,
                ChimericInterfacePointersTuple_t,
                __two_ptrs> __chimeric_addresses{}; // start off all nullptr's

  enum class HowInterfaceGotten : int {  // negative number means it can throw
    not_possible  =  0,
    user_conversion_nothrow = +1,
    user_conversion_throw   = -1,
    static_upcast = +2,
    dynamic = -3,
  };

  template<class T, class Interface>
  static consteval HowInterfaceGotten HowToGet(void) noexcept
  {
    if constexpr ( !opt_bases_only && requires(T &t){ chimeric_detail::user_conversion<Interface&>(t); } )
    {
      return noexcept(chimeric_detail::user_conversion<Interface&>(declval<T&>()))
             ?
             HowInterfaceGotten::user_conversion_nothrow
             :
             HowInterfaceGotten::user_conversion_throw;
    }
    else if constexpr ( requires(T &t){ chimeric_detail::static_upcast<Interface&>(t); } )
    {
      return HowInterfaceGotten::static_upcast;
    }
    else if constexpr ( opt_allow_dynamic && is_polymorphic_v<T>
                        && requires (T &t){ dynamic_cast<Interface&>(t); })
    {
      return HowInterfaceGotten::dynamic;
    }
    else
    {
      static_assert(false, "std::chimeric_ptr : cannot get interface from supplied object");
    }
  }

  template<typename T, bool only_user_conversion = false>
  static consteval bool AllNoexcept(void) noexcept
  {
    constexpr auto CanAnyThrow =
      []<size_t... Is>( index_sequence<Is...> ) consteval noexcept -> bool
        {
          constexpr auto CanInterfaceThrow = []<typename Interface>(void) consteval noexcept -> bool
            {
              return only_user_conversion
                     ?
                     HowInterfaceGotten::user_conversion_throw == HowToGet<T,Interface>()
                     :
                     to_underlying( HowToGet<T,Interface>() ) < 0;
            };

          return ( CanInterfaceThrow.template operator()< tuple_element_t<Is, ChimericInterfacesTuple_t>  >() || ... );
        };

    return false == CanAnyThrow.template operator()<>( __chimeric_interface_sequence );
  }

  template<class Interface, bool test_run_for_dynamic_cast = false, class T>
  static constexpr void const volatile *__get_one_interface(T *const p) noexcept( AllNoexcept<T>() )
  {
    if constexpr ( !opt_bases_only && requires { chimeric_detail::user_conversion<Interface&>(*p); } )
    {
      // constructor contains 'static_assert' for 'noexcept' violations
      if constexpr ( test_run_for_dynamic_cast ) return nullptr;
      else return static_cast<void const volatile *>(
        __builtin_addressof( chimeric_detail::user_conversion<Interface&>(*p) )
      );
    }
    else if constexpr ( requires { chimeric_detail::static_upcast<Interface&>(*p); } )
    {
      if constexpr ( test_run_for_dynamic_cast ) return nullptr;
      else return static_cast<void const volatile *>(
        __builtin_addressof( chimeric_detail::static_upcast<Interface&>(*p) )
      );
    }
    else if constexpr ( opt_bases_only && requires { chimeric_detail::user_conversion<Interface&>(*p); } )
    {
      static_assert(false, "std::chimeric_ptr : cannot get interface because of option 'bases_only'");
    }
    else if constexpr ( opt_allow_dynamic )
    {
      if constexpr ( false == is_polymorphic_v<T> )
      {
        static_assert(false, "std::chimeric_ptr : cannot get interface because T is not polymorphic (dynamic allowed)");
      }
      else if constexpr ( false == requires { dynamic_cast<Interface&>(*p); } )
      {
        static_assert(false, "std::chimeric_ptr : cannot get interface because dynamic_cast is ill-formed");
      }
      else
      {
        return __builtin_addressof( dynamic_cast<Interface&>(*p) );  // might throw 'std::bad_cast'
      }
    }
    else
    {
      static_assert(false, "std::chimeric_ptr : cannot get interface from supplied object (dynamic disallowed)");
    }
  }

  template<class T>
  static constexpr void const volatile *__resolve(void const volatile *const obj, size_t const idx)
  noexcept( opt_nothrow_resolve || AllNoexcept<T,true /* means only user conversion */>() )
  {
    constexpr auto mylambda = []<size_t... Is>(void const volatile *const obj2, size_t const idx2, index_sequence<Is...>) constexpr
      {
        T *const p = const_cast<T*>( static_cast<T const volatile*>(obj2) );
        void const volatile *out = nullptr;
    
        ( ( (idx2 == Is) && (out = __get_one_interface< tuple_element_t<Is, ChimericInterfacesTuple_t> >(p)) ) || ... );

        __glibcxx_assert( nullptr != out );
        return out;
      };

    return mylambda( obj, idx, __chimeric_interface_sequence );
  }

public:
  constexpr chimeric_ptr(void) noexcept = default;
  constexpr chimeric_ptr(nullptr_t) noexcept {}

  constexpr chimeric_ptr(chimeric_ptr const& ) noexcept = default;
  constexpr chimeric_ptr(chimeric_ptr      &&) noexcept = default;
  constexpr chimeric_ptr &operator=(chimeric_ptr const&) noexcept = default;
  constexpr chimeric_ptr &operator=(chimeric_ptr     &&) noexcept = default;
  constexpr chimeric_ptr &operator=(nullptr_t) noexcept
  {
    __chimeric_addresses = {};
    return *this;
  }

  template<class T>
  requires is_class_v<T>  // "std::chimeric_ptr : constructor argument must be pointer to class"
  /* implicit */ constexpr chimeric_ptr(T *const p) noexcept( (!opt_many_pointers && !opt_nothrow_resolve) || AllNoexcept<T>() )
  {
    static_assert( !opt_nothrow_construct || !opt_nothrow_resolve || AllNoexcept<T>(),
                   "std::chimeric_ptr : options 'nothrow_construct' and ' nothrow_resolve' specified however some interface conversions can throw.");

    if ( nullptr == p ) return;  // __chimeric_addresses is already initialised all nullptr's

    if constexpr ( opt_many_pointers )
    {
      static_assert( !opt_nothrow_construct || AllNoexcept<T>(),
                     "std::chimeric_ptr : option 'nothrow_construct' specified with 'many_pointers' however some interface conversions can throw.");

      constexpr auto set_from = []<size_t... Is>(ChimericInterfacesTuple_t &tup, T *const p2, index_sequence<Is...>) constexpr
        {
          ( (get<Is>(tup) = __get_one_interface< tuple_element_t<Is, ChimericInterfacesTuple_t> >(p2)), ... );
        };

      set_from( __chimeric_addresses, p, __chimeric_interface_sequence );
    }
    else
    {
      static_assert( !opt_nothrow_resolve || AllNoexcept<T,true /* means only user conversion */>(),
                     "std::chimeric_ptr : option 'nothrow_resolve' specified however some interface conversions can throw (try replace with 'many_pointers').");

      __chimeric_addresses.original_class_pointer = static_cast<void const volatile *>(p);
      __chimeric_addresses.funcptr_get_interface  = &__resolve<T>;
      
      if constexpr ( opt_nothrow_resolve )  // Try all the dynamic casts at construction (rather than at resolution)
      {
        constexpr auto f = []<size_t... Is>(T *const p2, index_sequence<Is...>) constexpr noexcept(false)
          {
            // The following line might throw as it tries all the dynamic_cast's
            ( __get_one_interface< tuple_element_t<Is, ChimericInterfacesTuple_t>, true /* means dynamic test */ >(p2), ... );
          };
        
        f( p, __chimeric_interface_sequence );
      }
    }
  }

  explicit constexpr operator bool(void) const noexcept
  {
    if constexpr ( opt_many_pointers )
      return nullptr != get<0u>(__chimeric_addresses);
    else
      return nullptr != __chimeric_addresses.original_class_pointer;
  }

  constexpr bool operator!(void) const noexcept { return !static_cast<bool>(*this); }

  friend constexpr bool operator==(chimeric_ptr const &p, nullptr_t) noexcept { return  !p; }
  friend constexpr bool operator==(nullptr_t, chimeric_ptr const &p) noexcept { return  !p; }
  friend constexpr bool operator!=(chimeric_ptr const &p, nullptr_t) noexcept { return !!p; }
  friend constexpr bool operator!=(nullptr_t, chimeric_ptr const &p) noexcept { return !!p; }

private:
  template<class B, size_t... Is>
  // see constraint below on operator B*  --  no point in duplicating it here
  constexpr B *__operator_B_star( index_sequence<Is...> ) const // see 'noexcept' specifier on operator B*
  {
    B *p = nullptr;

    constexpr auto mylambda = []<size_t I>( decltype(__chimeric_addresses) const &addrs ) constexpr noexcept -> B*
      {
        typedef tuple_element_t<I, ChimericInterfacesTuple_t> T;

        if constexpr ( chimeric_detail::derived_fromCV< T, B > )  // false if CV qualifiers incompatible
        {
          if constexpr ( opt_many_pointers )
          {
            return get<I>(addrs);  // implict conversion from Derived* to Base*
          }
          else
          {
            void const volatile *const raw = addrs.funcptr_get_interface(addrs.original_class_pointer, I);
            return const_cast<T*>( static_cast< T const volatile* >(raw) );  // implict conversion from Derived* to Base*
          }
        }
        else return nullptr;
      };

    ( (p = mylambda.template operator()<Is>(__chimeric_addresses)) || ... );
    __glibcxx_assert( nullptr != p );
    return p;
  }

public:
  template<class B>
  requires chimeric_detail::tuple_contains_type_derived_fromCV<B, ChimericInterfacesTuple_t>::value
  constexpr operator B*(void) const noexcept(opt_many_pointers || opt_nothrow_resolve)
  {
    __glibcxx_assert( *this );
    return this->__operator_B_star<B>( __chimeric_interface_sequence );
  }

  template<typename MemPtr, typename... Params>
  requires is_member_pointer_v<MemPtr>  // cannot be a reference
  constexpr decltype(auto) std_invoke_member_pointer(MemPtr const mem, Params&&... args) const
    noexcept( (opt_many_pointers || opt_nothrow_resolve) && noexcept( (declval< chimeric_detail::__member_ptr_class<MemPtr>* >()->*mem)( forward<Params>(args)... ) ) )
  {
    __glibcxx_assert( *this );
    typedef chimeric_detail::__member_ptr_class<MemPtr> Class;
    Class *const p = static_cast<Class*>( *this );  // won't throw if 'many_pointers' or 'nothrow_resolve'
    __glibcxx_assert( nullptr != p );
    return (p->*mem)( forward<Params>(args)... );
  }

  template<typename MemPtr>
  requires is_member_pointer_v<MemPtr>  // cannot be a reference
  constexpr decltype(auto) operator->*(MemPtr const mem) const
  noexcept( is_member_function_pointer_v<MemPtr> || opt_many_pointers || opt_nothrow_resolve )
  {
    __glibcxx_assert( *this );
    if constexpr ( is_member_function_pointer_v<MemPtr> )
    {
      /* In here we need to make a copy of the chimeric_ptr
         because the current chimeric_ptr could be 'moved from'
         before the member function is invoked. */
      chimeric_ptr<InterfacesAndOptions...> const mycopy(*this);
      return [mycopy /* capture by value */, mem]<typename... Params2>(Params2&&... args2)
        noexcept( noexcept(mycopy.std_invoke_member_pointer( mem, forward<Params2>(args2)... )) )
        -> decltype(auto)
        {
          return mycopy.std_invoke_member_pointer( mem, forward<Params2>(args2)... );
        };
    }
    else /* member object */
    {
      typedef chimeric_detail::__member_ptr_class<MemPtr> Class;
      Class *const p = static_cast<Class*>( *this );  // won't throw if 'many_pointers' or 'nothrow_resolve'
      __glibcxx_assert( nullptr != p );
      return p->*mem;
    }
  }
};

_GLIBCXX_END_NAMESPACE_VERSION
} // namespace std

#endif // ifndef _GLIBCXX_CHIMERIC_PTR_H

#else  // else leg of "if __cplusplus >= 202302L"
#  error "std::chimeric_ptr requires C++23 or later."
#endif
