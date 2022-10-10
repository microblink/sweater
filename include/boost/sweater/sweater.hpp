////////////////////////////////////////////////////////////////////////////////
///
/// \file sweater.hpp
/// -----------------
///
/// (c) Copyright Domagoj Saric 2016 - 2021.
///
///  Use, modification and distribution are subject to the
///  Boost Software License, Version 1.0. (See accompanying file
///  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
///
///  See http://www.boost.org for most recent version.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef sweater_hpp__83B147A4_8450_4A6D_8FC1_72EA64FACABF
#define sweater_hpp__83B147A4_8450_4A6D_8FC1_72EA64FACABF
#pragma once
//------------------------------------------------------------------------------
#include "threading/hardware_concurrency.hpp"

#  if BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY == 1
#   define BOOST_SWEATER_IMPL single_threaded
#	include "impls/single_threaded.hpp"
#elif defined( __APPLE__ ) && !defined( BOOST_SWEATER_IMPL )
// #   define BOOST_SWEATER_IMPL apple
// #	include "impls/apple.hpp"
#   undef  BOOST_SWEATER_IMPL
#   define BOOST_SWEATER_IMPL generic
#	include "impls/generic.hpp"
#elif defined( _WIN32_unimplemented ) && !defined( BOOST_SWEATER_IMPL )
#   define BOOST_SWEATER_IMPL windows
#	include "impls/windows.hpp"
#elif defined( _OPENMP ) && !defined( BOOST_SWEATER_IMPL )
#   define BOOST_SWEATER_IMPL openmp
#	include "impls/openmp.hpp"
#elif defined( BOOST_SWEATER_IMPL )
#include "boost/preprocessor/stringize.hpp"
#	include BOOST_PP_STRINGIZE( detail/BOOST_SWEATER_IMPL.hpp )
#else
#   undef  BOOST_SWEATER_IMPL
#   define BOOST_SWEATER_IMPL generic
#	include "impls/generic.hpp"
#endif

#ifdef _MSC_VER
#   include <version>
#   if defined( _MSVC_STL_VERSION )
#   include <yvals.h>
#   define BOOST_SWEATER_STRINGIZE _STRINGIZE
#else // presumably clang-cl with libcpp
#   include "boost/preprocessor/stringize.hpp"
#   define BOOST_SWEATER_STRINGIZE BOOST_PP_STRINGIZE
#endif
#   pragma detect_mismatch( "Boost.Sweater implementation", BOOST_SWEATER_STRINGIZE( BOOST_SWEATER_IMPL ) )
#   undef BOOST_SWEATER_STRINGIZE
#endif // _MSC_VER
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace sweater
{
//------------------------------------------------------------------------------

using namespace BOOST_SWEATER_IMPL;

//------------------------------------------------------------------------------
} // namespace sweater
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // sweater_hpp
