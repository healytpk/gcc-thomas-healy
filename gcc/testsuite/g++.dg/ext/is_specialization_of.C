// { dg-do compile }
// { dg-options "-std=gnu++23" }

#include <cstddef>
#include <array>
#include <list>
#include <ratio>
#include <tuple>
#include <vector>

static_assert( __is_specialization_of(std::tuple<int,float,char>, std::tuple));
static_assert( __is_specialization_of(std::tuple<              >, std::tuple));

static_assert( __is_specialization_of(std::array<int, 3>, std::array ));
static_assert( __is_specialization_of(std::list <int   >, std::list  ));
static_assert( __is_specialization_of(std::ratio <1,  2>, std::ratio ));
static_assert( __is_specialization_of(std::vector<int  >, std::vector));

static_assert(!__is_specialization_of(std::array<int, 3>, std::vector));
static_assert(!__is_specialization_of(std::list <int   >, std::array ));
static_assert(!__is_specialization_of(std::ratio<1,   2>, std::array ));
static_assert(!__is_specialization_of(std::array<int, 2>, std::ratio ));

static_assert(!__is_specialization_of(int  , std::array ));
static_assert(!__is_specialization_of(float, std::ratio ));
static_assert(!__is_specialization_of(int  , std::vector));
static_assert(!__is_specialization_of(float, std::list  ));

/* The following will force a compiler error rather than a 'false'

static_assert(!__is_specialization_of(std::array <int, 3>, std::array <int, 3>));
static_assert(!__is_specialization_of(std::list  <int   >, std::list  <int   >));
static_assert(!__is_specialization_of(std::ratio <1,   2>, std::ratio <1,  2 >));
static_assert(!__is_specialization_of(std::vector<int   >, std::vector<int   >));

static_assert(!__is_specialization_of(std::array <int, 3>, int         ));
static_assert(!__is_specialization_of(std::list  <int   >, float       ));
static_assert(!__is_specialization_of(std::ratio <1,   2>, double      ));
static_assert(!__is_specialization_of(std::vector<int   >, std::tuple<>));
*/

template<template<typename, std::size_t> class TT>
constexpr bool f()
{
  return __is_specialization_of(std::array<int, 3>, TT);
}

static_assert(f<std::array>());
