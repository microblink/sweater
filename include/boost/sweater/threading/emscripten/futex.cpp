////////////////////////////////////////////////////////////////////////////////
///
/// \file futex.cpp
/// ---------------
///
/// (c) Copyright Domagoj Saric 2021.
///
///  Use, modification and distribution are subject to the
///  Boost Software License, Version 1.0. (See accompanying file
///  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
///
///  See http://www.boost.org for most recent version.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "../futex.hpp"

#include <Utils/disable_warnings.hpp>

#include <cmath>

#include <emscripten/threading.h>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace thrd_lite
{
//------------------------------------------------------------------------------

namespace
{
    static_assert( sizeof( futex ) == 4 );
    auto void_cast( futex const * p_futex ) noexcept { return reinterpret_cast<volatile void *>( const_cast< futex * >( p_futex ) ); }
}

void futex::wake_one(                                              ) const noexcept { wake( 1 ); }
void futex::wake    ( hardware_concurrency_t const waiters_to_wake ) const noexcept { emscripten_futex_wake( void_cast( this ), waiters_to_wake ); }
void futex::wake_all(                                              ) const noexcept { emscripten_futex_wake( void_cast( this ), INT_MAX         ); }

MB_DISABLE_WARNING_PUSH
MB_DISABLE_WARNING_CLANG( "-Wunknown-warning-option" )
MB_DISABLE_WARNING_CLANG( "-Wnan-infinity-disabled" )

void futex::wait_if_equal( value_type const desired_value ) const noexcept
{
    emscripten_futex_wait( void_cast( this ), desired_value, INFINITY );
};

MB_DISABLE_WARNING_POP

//------------------------------------------------------------------------------
} // namespace thrd_lite
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
