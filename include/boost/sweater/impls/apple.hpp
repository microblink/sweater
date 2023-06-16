////////////////////////////////////////////////////////////////////////////////
///
/// \file apple.hpp
/// ---------------
///
/// (c) Copyright Domagoj Saric 2016 - 2023.
///
///  Use, modification and distribution are subject to the
///  Boost Software License, Version 1.0. (See accompanying file
///  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
///
///  See http://www.boost.org for most recent version.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#pragma once
//------------------------------------------------------------------------------
#include "../threading/hardware_concurrency.hpp"
#include "../spread_chunked.hpp"

#include <boost/assert.hpp>
#include <boost/core/no_exceptions_support.hpp>
#include <boost/config_ex.hpp>

#include <cstdint>
#include <future>
#include <thread>
#include <type_traits>

#include <dispatch/dispatch.h>
#include <TargetConditionals.h>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace sweater
{
//------------------------------------------------------------------------------
namespace apple
{
//------------------------------------------------------------------------------

class shop
{
public:
    using iterations_t = std::uint32_t;

    // http://newosxbook.com/articles/GCD.html
    // http://www.idryman.org/blog/2012/08/05/grand-central-dispatch-vs-openmp
    [[ gnu::pure ]]
    static thrd_lite::hardware_concurrency_t number_of_workers() noexcept
    {
        BOOST_ASSERT_MSG( thrd_lite::hardware_concurrency_max == std::thread::hardware_concurrency(), "Hardware concurrency changed at runtime!?" );
#   if BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
        BOOST_ASSUME( thrd_lite::hardware_concurrency_max <= BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY );
#   endif
        return thrd_lite::hardware_concurrency_max;
    }

    template <typename F>
    static void spread_the_sweat( iterations_t const iterations, F && work, iterations_t /*parallelizable_iterations_count TODO*/ = 1 ) noexcept
    {
        static_assert( noexcept( work( iterations, iterations ) ), "F must be noexcept" );

        // "...make the number of iterations at least three times the total number of cores on the system."
        // https://developer.apple.com/documentation/apple-silicon/tuning-your-code-s-performance-for-apple-silicon
        // http://wanderingcoder.net/2021/02/25/libdispatch-douchebag
        // ...so we make it 4 (to be turned into a parameter if needed later)
        // but not larger (or simply <VAR>iterations</VAR>) to also mitigate
        // GCD (internal dispatch_apply loop) overheads.
        auto const number_of_chunks{ static_cast<iterations_t>( 4 * thrd_lite::hardware_concurrency_max ) };
        auto /*const*/ worker
        {
            [
                &work,
                setup = chunked_spread{ iterations, number_of_chunks }
            ]
            ( std::size_t const chunk ) noexcept
            {

                auto const chunk_index{ static_cast<thrd_lite::hardware_concurrency_t>( chunk ) };
                auto const range      { setup.chunk_range( chunk_index ) };
                work( range.first, range.second );
            }
        };
        /// \note dispatch_apply delegates to dispatch_apply_f so we avoid the
        /// extra layer.
        ///                                   (04.10.2016.) (Domagoj Saric)
        dispatch_apply_f
        (
            std::min( number_of_chunks, iterations ),
            high_priority_queue,
            const_cast<void *>( static_cast<void const *>( &worker ) ),
            []( void * const p_context, std::size_t const chunk ) noexcept
            {
                auto & __restrict the_worker{ *static_cast<decltype( worker ) *>( p_context ) };
                the_worker( chunk );
            }
        );
    }

    template <typename F>
    static void fire_and_forget( F && work ) noexcept
    {
        static_assert( noexcept( work() ), "Fire and forget work has to be noexcept" );

        using Functor = std::remove_reference_t<F>;
        if constexpr
        (
            ( sizeof ( work    ) <= sizeof ( void * ) ) &&
            ( alignof( Functor ) <= alignof( void * ) ) &&
            std::is_trivially_copyable    <Functor>::value &&
            std::is_trivially_destructible<Functor>::value
        )
        {
            void * context;
            new ( &context ) Functor( std::forward<F>( work ) );
            dispatch_async_f
            (
                default_queue,
                context,
                []( void * context ) noexcept
                {
                    auto & __restrict the_work{ reinterpret_cast<Functor &>( context ) };
                    the_work();
                }
            );
        }
        else
        {
#       if defined( __clang__ )
            /// \note "ObjC++ attempts to copy lambdas, preventing capture of
            /// move-only types". https://llvm.org/bugs/show_bug.cgi?id=20534
            ///                               (14.01.2016.) (Domagoj Saric)
            __block auto moveable_work( std::forward<F>( work ) );
            dispatch_async( default_queue, ^(){ moveable_work(); } );
#       else
            /// \note Still no block support in GCC.
            ///                               (10.06.2017.) (Domagoj Saric)
            auto const p_heap_work( new Functor( std::forward<F>( work ) ) );
            dispatch_async_f
            (
                default_queue,
                p_heap_work,
                []( void * const p_context ) noexcept
                {
                    auto & __restrict the_work{ *static_cast<Functor const *>( p_context ) };
                    the_work();
                    delete &the_work;
                }
            );
#       endif // compiler
        }
    }

    template <typename F>
    static auto dispatch( F && work )
    {
        using result_t = typename std::invoke_result_t<F>;
        std::promise<result_t> promise;
        std::future<result_t> future( promise.get_future() );
        fire_and_forget
        (
            [promise = std::move( promise ), work = std::forward<F>( work )]
            () mutable noexcept
            {
                BOOST_TRY
                {
                    if constexpr ( std::is_same_v<result_t, void> )
                    {
                        work();
                        promise.set_value();
                    }
                    else
                    {
                        promise.set_value( work() );
                    }
                }
                BOOST_CATCH( ... )
                {
                    promise.set_exception( std::current_exception() );
                }
                BOOST_CATCH_END
            }
        );
        return future;
    }

private:
    static dispatch_queue_t const default_queue      ;
    static dispatch_queue_t const high_priority_queue;
}; // class shop

__attribute__(( weak )) dispatch_queue_t const shop::default_queue      ( dispatch_get_global_queue( QOS_CLASS_DEFAULT       , 0 ) );
__attribute__(( weak )) dispatch_queue_t const shop::high_priority_queue( dispatch_get_global_queue( QOS_CLASS_USER_INITIATED, 0 ) );

//------------------------------------------------------------------------------
} // namespace apple
//------------------------------------------------------------------------------
} // namespace sweater
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
