////////////////////////////////////////////////////////////////////////////////
///
/// \file mpmc_moodycamel.hpp
/// -------------------------
///
/// MoodyCamel based implementation of the queue backend for the generic sweater
/// implementation.
///
/// Copyright (c) Domagoj Saric 2017 - 2019.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
/// (See accompanying file LICENSE_1_0.txt or copy at
/// http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef mpmc_moodycamel_hpp__2418E6BF_4795_42B2_8FE8_4733F52FFC89
#define mpmc_moodycamel_hpp__2418E6BF_4795_42B2_8FE8_4733F52FFC89
#pragma once
//------------------------------------------------------------------------------
// https://svn.boost.org/trac/boost/ticket/12880
#if defined( _WIN64 ) || defined( __APPLE__ ) || defined( __aarch64__ ) || defined( LP64 )
#   define BOOST_SWEATER_AUX_ALIGNED_MALLOC
#endif

#ifndef BOOST_SWEATER_AUX_ALIGNED_MALLOC
#   include <boost/align/aligned_alloc.hpp>
#endif // BOOST_SWEATER_AUX_ALIGNED_MALLOC

#ifdef _MSC_VER
#    pragma warning( push )
#    pragma warning( disable : 4127 ) // Conditional expression is constant @ concurrentqueue.h 775
#endif // _MSC_VER
#include <concurrentqueue/concurrentqueue.h>
#ifdef MSC_VER
#   pragma warning( pop )
#endif // MSC_VER

#include <cstdint>
#include <type_traits>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace sweater
{
//------------------------------------------------------------------------------
namespace queues
{
//------------------------------------------------------------------------------

template <typename Work>
class mpmc_moodycamel
{
private:
    struct queue_traits : moodycamel::ConcurrentQueueDefaultTraits
    {
	    using size_t  = std::uint32_t;
	    using index_t = std::uint32_t;

	    static constexpr size_t EXPLICIT_INITIAL_INDEX_SIZE = 4;
	    static constexpr size_t IMPLICIT_INITIAL_INDEX_SIZE = 4;

#   ifndef BOOST_SWEATER_AUX_ALIGNED_MALLOC
        static void * malloc( std::size_t size ) noexcept
        {
#       if defined( __EMSCRIPTEN__ ) && !defined( NDEBUG )
            size = ( ( size + 15 ) / 16 ) * 16;
            volatile auto p = std::malloc( size );
            auto p2 = std::malloc( size );
            std::free( p );
            return p2;
#       else
            return boost::alignment::aligned_alloc( 16, size );
#       endif
        }
        static void free( void * const ptr ) noexcept
        {
#       if defined( __EMSCRIPTEN__ ) && !defined( NDEBUG )
            std::free( ptr );
#       else
            return boost::alignment::aligned_free( ptr );
#       endif
        }
#   endif // BOOST_SWEATER_AUX_ALIGNED_MALLOC
    }; // struct queue_traits

    using work_queue = moodycamel::ConcurrentQueue<Work, queue_traits>;

public:
    using consumer_token_t = typename work_queue::consumer_token_t;
    using producer_token_t = typename work_queue::producer_token_t;

    template <typename Functor, typename ... Token>
    bool enqueue( Functor && functor, Token const & ... token ) { return queue_.enqueue( token..., std::forward<Functor>( functor ) ); }

    template <typename ... Arguments>
    bool enqueue_bulk( Arguments && ... arguments ) { return queue_.enqueue_bulk( std::forward<Arguments>( arguments )... ); }

    template <typename ... Token>
    bool dequeue( Work & work, Token & ... token ) noexcept( std::is_nothrow_move_assignable<Work>::value ) { return queue_.try_dequeue( token..., work ); }

    consumer_token_t consumer_token() noexcept { return consumer_token_t( queue_ ); }
    producer_token_t producer_token() noexcept { return producer_token_t( queue_ ); }

    auto empty() const noexcept { return queue_.size_approx() == 0; }

private:
    work_queue queue_;
}; // class mpmc_moodycamel

//------------------------------------------------------------------------------
} // namespace queues
//------------------------------------------------------------------------------
} // namespace sweater
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#undef BOOST_SWEATER_AUX_ALIGNED_MALLOC

#endif // mpmc_moodycamel_hpp
