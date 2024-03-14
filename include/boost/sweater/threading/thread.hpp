////////////////////////////////////////////////////////////////////////////////
///
/// \file thread.hpp
/// ----------------
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
#pragma once
//------------------------------------------------------------------------------
#include "condvar.hpp"
#include "mutex.hpp"
#ifdef _WIN32
#include "windows/thread.hpp"
#else
#include "posix/thread.hpp"
#endif

#include "hardware_concurrency.hpp"

#include <boost/assert.hpp>

#ifndef BOOST_NO_EXCEPTIONS
#include <stdexcept>
#endif // BOOST_NO_EXCEPTIONS

#include <exception>  // std::terminate
#include <memory>
#include <mutex>
#include <type_traits>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace thrd_lite
{
//------------------------------------------------------------------------------

namespace detail
{
    template < typename Functor >
    constexpr bool const fits_into_a_pointer =
        ( sizeof ( Functor ) <= sizeof ( void * ) )     &&
        std::is_trivially_copy_constructible_v<Functor> &&
        std::is_trivially_destructible_v      <Functor>;
} // namespace detail

class thread : public thread_impl
{
private:
    class synchronized_invocation // TODO futex impl
    {
    public:
        synchronized_invocation( void const * const p_functor ) noexcept
            :
            p_functor_     ( const_cast<void *>( p_functor ) ),
            lock_          ( mutex_                          ),
            functor_copied_( false                           )
        {}

        ~synchronized_invocation() noexcept
        {
            while ( BOOST_UNLIKELY( !functor_copied_ ) )
                event_.wait( lock_ );
        }

        template <typename Functor>
        auto & functor() const noexcept { return *static_cast<Functor *>( p_functor_ ); }

        auto notify() noexcept
        {
            functor_copied_ = true;
            event_.notify_one();
        }

    private:
        void *                  const p_functor_;
        mutex                         mutex_;
        condition_variable            event_;
        std::unique_lock<mutex>       lock_;
        bool volatile                 functor_copied_;
    }; // struct synchronized_invocation

public:
    thread() noexcept = default;
   ~thread() noexcept { BOOST_ASSERT_MSG( !joinable(), "Abandoning a thread!" ); }

    thread( thread && other ) noexcept { swap( other ); }
    thread( thread const & ) = delete;

    thread & operator=( thread && other ) noexcept
    {
        this->handle_ = other.handle_;
        other.handle_ = {};
        return *this;
    }

    template <class F>
    thread & operator=( F && functor )
    {
        using ret_t   = std::invoke_result_t<thread_procedure, void *>;
        using Functor = std::decay_t<F>;

        if constexpr ( detail::fits_into_a_pointer< F > )
        {
            void * context;
            new ( &context ) Functor( std::forward<F>( functor ) );
            create
            (
                []( void * context ) noexcept -> ret_t
                {
#               ifdef BOOST_GCC
#                   pragma GCC diagnostic push
#                   pragma GCC diagnostic ignored "-Wstrict-aliasing"
#               endif // GCC
                    auto & tiny_functor( reinterpret_cast<Functor &>( context ) );
#               ifdef BOOST_GCC
#                   pragma GCC diagnostic pop
#               endif // GCC
                    tiny_functor();
                    return 0;
                },
                context
            );
        }
        else
        if constexpr ( noexcept( Functor( std::forward<F>( functor ) ) ) )
        {
            synchronized_invocation context( &functor );

            create
            (
                []( void * const context ) noexcept -> ret_t
                {
                    auto & synchronized_context( *static_cast<synchronized_invocation *>( context ) );
                    Functor functor( std::forward<F>( synchronized_context.functor<Functor>() ) );
                    synchronized_context.notify();
                    functor();
                    return 0;
                },
                &context
            );
        }
        else
        {
            auto p_functor( std::make_unique<Functor>( std::forward<F>( functor ) ) );
            create
            (
                []( void * const context ) noexcept -> ret_t
                {
                    std::unique_ptr<Functor> const p_functor( static_cast<Functor *>( context ) );
                    (*p_functor)();
                    return 0;
                },
                p_functor.get()
            );
            p_functor.release();
        }
        return *this;
    }

    auto native_handle() const noexcept { return handle_; }

    bool joinable() const noexcept { return native_handle() != native_handle_type{}; }

    void join() noexcept
    {
        BOOST_ASSERT_MSG( joinable(), "No thread to join" );
        BOOST_ASSERT_MSG( get_id() != get_active_thread_id(), "Waiting on this_thread: deadlock!" );

        thread_impl::join();
    }

    void swap( thread & other ) noexcept { std::swap( this->handle_, other.handle_ ); }

    static auto hardware_concurrency() noexcept { return hardware_concurrency_max; }

private:
    BOOST_ATTRIBUTES( BOOST_COLD )
    void create( thread_procedure const start_routine, void * const arg )
    {
        BOOST_ASSERT_MSG( !joinable(), "A thread already created" );
        auto const error( thread_impl::create( start_routine, arg ) );
        if ( BOOST_UNLIKELY( error ) )
        {
#       ifdef BOOST_NO_EXCEPTIONS
            std::terminate();
#       elif 0 // disabled - avoid the overhead of (at least) <system_error>
            throw std::system_error( std::error_code( error, std::system_category() ), "Thread creation failed" );
#       else
            throw std::runtime_error( "Not enough resources to create a new thread" );
#       endif
        }
    }
}; // class thread

//------------------------------------------------------------------------------
} // namespace thrd_lite
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
