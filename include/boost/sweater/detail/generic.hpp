////////////////////////////////////////////////////////////////////////////////
///
/// \file generic.hpp
/// -----------------
///
/// (c) Copyright Domagoj Saric 2016 - 2020.
///
///  Use, modification and distribution are subject to the
///  Boost Software License, Version 1.0. (See accompanying file
///  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
///
///  See http://www.boost.org for most recent version.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef generic_hpp__99FE2034_248F_4C7D_8CD2_EB2BB8247377
#define generic_hpp__99FE2034_248F_4C7D_8CD2_EB2BB8247377
#pragma once
//------------------------------------------------------------------------------
#include "../hardware_concurrency.hpp"
#include "../queues/mpmc_moodycamel.hpp"

#include <boost/core/no_exceptions_support.hpp>
#include <boost/config_ex.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/functionoid/functionoid.hpp>
#include <boost/range/iterator_range_core.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <iterator>
#ifdef BOOST_MSVC
#include <malloc.h>
#else
#include <alloca.h>
#endif // BOOST_MSVC
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#if 0 // sacrifice standard conformance to avoid the overhead of system_error
#include <system_error>
#endif // disabled

#ifdef BOOST_HAS_PTHREADS
#include <pthread.h>
#include <semaphore.h>
#else
#include <windows.h> // for SetThreadPriority
#endif // BOOST_HAS_PTHREADS

#if defined( __linux )
#include <sys/time.h>
#include <sys/resource.h>
#   ifdef __GLIBC__
    // Glibc pre 2.3 does not provide the wrapper for the gettid system call
    // https://stackoverflow.com/a/36025103
    // https://linux.die.net/man/2/gettid
#   include <unistd.h>
#   include <sys/syscall.h>
#ifdef BOOST_GCC
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wattributes"
#endif // GCC
    __attribute__(( const, weak )) inline
    pid_t gettid() { return syscall( SYS_gettid ); }
#ifdef BOOST_GCC
#    pragma GCC diagnostic pop
#endif // GCC
#   endif // glibc
#endif // __linux
//------------------------------------------------------------------------------
////////////////////////////////////////////////////////////////////////////////
// Compile time configuration
////////////////////////////////////////////////////////////////////////////////

#ifndef BOOST_SWEATER_USE_CALLER_THREAD
#   define BOOST_SWEATER_USE_CALLER_THREAD true
#endif // BOOST_SWEATER_USE_CALLER_THREAD

#ifndef BOOST_SWEATER_HMP // heterogeneous multi-core processing
// https://android.googlesource.com/kernel/msm/+/android-msm-bullhead-3.10-marshmallow-dr/Documentation/scheduler/sched-hmp.txt
// https://www.kernel.org/doc/html/latest/scheduler/sched-energy.html
// https://www.sisoftware.co.uk/2015/06/22/arm-big-little-the-trouble-with-heterogeneous-multi-processing-when-4-are-better-than-8-or-when-8-is-not-always-the-lucky-number
// https://lwn.net/Articles/352286
#if defined( __aarch64__ )
#   define BOOST_SWEATER_HMP true
#else
#   define BOOST_SWEATER_HMP false
#endif
#endif // BOOST_SWEATER_HMP

#ifndef BOOST_SWEATER_USE_PARALLELIZATION_COST
#   define BOOST_SWEATER_USE_PARALLELIZATION_COST BOOST_SWEATER_HMP
#endif // BOOST_SWEATER_USE_PARALLELIZATION_COST

// https://petewarden.com/2015/10/11/one-weird-trick-for-faster-android-multithreading
// https://stackoverflow.com/questions/26637654/low-latency-communication-between-threads-in-the-same-process
// https://source.android.com/devices/tech/debug/jank_jitter
// https://www.scylladb.com/2016/06/10/read-latency-and-scylla-jmx-process
// https://lwn.net/Articles/663879
#ifndef BOOST_SWEATER_SPIN_BEFORE_SUSPENSION
#if defined( __ANDROID__ )
#   define BOOST_SWEATER_SPIN_BEFORE_SUSPENSION true
#else
#   define BOOST_SWEATER_SPIN_BEFORE_SUSPENSION false
#endif // Android
#endif // BOOST_SWEATER_SPIN_BEFORE_SUSPENSION

// Basic auto-tuning mechanism for caller-thread usage: account for overheads/
// delays of worker-wakeup - give more more work to the caller to do to avoid
// it wasting time waiting for workers to finish/join (requires
// BOOST_SWEATER_USE_CALLER_THREAD).
#ifndef BOOST_SWEATER_CALLER_BOOST
#   define BOOST_SWEATER_CALLER_BOOST BOOST_SWEATER_HMP
#endif // BOOST_SWEATER_CALLER_BOOST

#ifndef BOOST_SWEATER_EVENTS
#if defined( __GNUC__ )
#   define BOOST_SWEATER_EVENTS true
#else
#   define BOOST_SWEATER_EVENTS false
#endif
#endif // BOOST_SWEATER_EVENTS

#if BOOST_SWEATER_HMP
#   if defined( BOOST_SWEATER_EXACT_WORKER_SELECTION ) && !BOOST_SWEATER_EXACT_WORKER_SELECTION
#       error BOOST_SWEATER_HMP requires BOOST_SWEATER_EXACT_WORKER_SELECTION
#   elif !defined( BOOST_SWEATER_EXACT_WORKER_SELECTION )
#       define BOOST_SWEATER_EXACT_WORKER_SELECTION true
#   endif
#endif // BOOST_SWEATER_HMP

#ifndef BOOST_SWEATER_EXACT_WORKER_SELECTION
#   define BOOST_SWEATER_EXACT_WORKER_SELECTION true
#endif // BOOST_SWEATER_EXACT_WORKER_SELECTION

#if BOOST_SWEATER_EXACT_WORKER_SELECTION && defined( _WIN32 ) && !defined( _WIN64 )
struct _IMAGE_DOS_HEADER;
extern "C" _IMAGE_DOS_HEADER __ImageBase;
#endif
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace sweater
{
//------------------------------------------------------------------------------
namespace queues { template <typename Work> class mpmc_moodycamel; }
//------------------------------------------------------------------------------
namespace generic
{
//------------------------------------------------------------------------------

#ifdef __clang__
// Clang does not support [[ (un)likely ]] cpp attributes
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-attributes"
#endif // clang

namespace detail
{
#if defined( BOOST_HAS_PTHREADS ) && !defined( __ANDROID__ )
#ifdef __EMSCRIPTEN__
    inline auto const default_policy_priority_min        { 0 };
    inline auto const default_policy_priority_max        { 0 };
#else
    inline auto const default_policy_priority_min        { ::sched_get_priority_min( SCHED_OTHER ) };
    inline auto const default_policy_priority_max        { ::sched_get_priority_max( SCHED_OTHER ) };
#endif
    inline auto const default_policy_priority_range      { static_cast<std::uint8_t>( default_policy_priority_max - default_policy_priority_min ) };
    inline auto const default_policy_priority_unchangable{ default_policy_priority_range == 0 };

    inline
    std::uint8_t round_divide( std::uint16_t const numerator, std::uint8_t const denominator ) noexcept
    {
        auto const integral_division      {   numerator / denominator                          };
        auto const at_least_half_remainder{ ( numerator % denominator ) >= ( denominator / 2 ) };
        return integral_division + at_least_half_remainder;
    }
#endif // __linux && !__ANDROID__ || __APPLE__

    BOOST_ATTRIBUTES( BOOST_COLD )
    inline void nop() noexcept // lightweight spin nop
    {
        // TODO http://open-std.org/JTC1/SC22/WG21/docs/papers/2016/p0514r0.pdf
#   if defined( __arc__ ) || defined( __mips__ ) || defined( __arm__ ) || defined( __powerpc__ )
        asm volatile( "" ::: "memory" );
#   elif defined( __i386__ ) || defined( __x86_64__ )
        asm volatile( "rep; nop" ::: "memory" );
#   elif defined( __aarch64__ )
        asm volatile( "yield" ::: "memory" );
#   elif defined( __ia64__ )
        asm volatile ( "hint @pause" ::: "memory" );
#   elif defined( _MSC_VER )
#       if defined( _M_ARM )
            YieldProcessor();
#       else
            _mm_pause();
#       endif
#   endif
    }

    inline void nops( std::uint8_t const count ) noexcept { for ( auto i{ 0 }; i < count; ++i ) nop(); }
} // namespace detail


namespace events
{
#if BOOST_SWEATER_EVENTS
#   define BOOST_AUX_EVENT_BODY ;
#   define BOOST_AUX_EVENT_INLINE
#else
#   define BOOST_AUX_EVENT_BODY {}
#   define BOOST_AUX_EVENT_INLINE inline constexpr
#endif // BOOST_SWEATER_EVENTS

    BOOST_AUX_EVENT_INLINE void caller_stalled           ( std::uint8_t /*current_boost*/                                        ) noexcept BOOST_AUX_EVENT_BODY
    BOOST_AUX_EVENT_INLINE void caller_join_begin        ( bool /*spinning*/                                                     ) noexcept BOOST_AUX_EVENT_BODY
    BOOST_AUX_EVENT_INLINE void caller_join_end          (                                                                       ) noexcept BOOST_AUX_EVENT_BODY
    BOOST_AUX_EVENT_INLINE void caller_work_begin        (                                          std::uint32_t /*iterations*/ ) noexcept BOOST_AUX_EVENT_BODY
    BOOST_AUX_EVENT_INLINE void caller_work_end          (                                                                       ) noexcept BOOST_AUX_EVENT_BODY
    BOOST_AUX_EVENT_INLINE void worker_enqueue_begin     ( hardware_concurrency_t /*worker_index*/                               ) noexcept BOOST_AUX_EVENT_BODY
    BOOST_AUX_EVENT_INLINE void worker_enqueue_end       ( hardware_concurrency_t /*worker_index*/, std::uint32_t /*iterations*/ ) noexcept BOOST_AUX_EVENT_BODY
    BOOST_AUX_EVENT_INLINE void worker_work_begin        ( hardware_concurrency_t /*worker_index*/                               ) noexcept BOOST_AUX_EVENT_BODY
    BOOST_AUX_EVENT_INLINE void worker_work_end          ( hardware_concurrency_t /*worker_index*/                               ) noexcept BOOST_AUX_EVENT_BODY
    BOOST_AUX_EVENT_INLINE void worker_sleep_begin       ( hardware_concurrency_t /*worker_index*/                               ) noexcept BOOST_AUX_EVENT_BODY
    BOOST_AUX_EVENT_INLINE void worker_sleep_end         ( hardware_concurrency_t /*worker_index*/                               ) noexcept BOOST_AUX_EVENT_BODY
    BOOST_AUX_EVENT_INLINE void worker_bulk_enqueue_begin( hardware_concurrency_t /*number_of_workers*/                          ) noexcept BOOST_AUX_EVENT_BODY
    BOOST_AUX_EVENT_INLINE void worker_bulk_enqueue_end  (                                                                       ) noexcept BOOST_AUX_EVENT_BODY
    BOOST_AUX_EVENT_INLINE void worker_bulk_signal_begin ( hardware_concurrency_t /*number_of_workers*/                          ) noexcept BOOST_AUX_EVENT_BODY
    BOOST_AUX_EVENT_INLINE void worker_bulk_signal_end   (                                                                       ) noexcept BOOST_AUX_EVENT_BODY

    BOOST_AUX_EVENT_INLINE void spread_begin             ( std::uint32_t /*iterations*/                                          ) noexcept BOOST_AUX_EVENT_BODY
    BOOST_AUX_EVENT_INLINE void spread_end               ( hardware_concurrency_t /*dispatched_parts*/, bool /*caller_used*/     ) noexcept BOOST_AUX_EVENT_BODY

#undef BOOST_AUX_EVENT_BODY
#undef BOOST_AUX_EVENT_INLINE
} // namespace events

class shop
{
public:
    using iterations_t = std::uint32_t;

#if BOOST_SWEATER_HMP
    static inline bool hmp = true;

    struct hmp_clusters_info
    {
        static auto constexpr max_clusters{ 3 }; // big - medium - little / turbo - big - little (Android state of affairs)

        hardware_concurrency_t cores[ max_clusters ];
        float                  power[ max_clusters ]; // 'capacity' as in capacity aware scheduling
    }; // struct hmp_clusters_info

private:
    struct hmp_config
    {
        static auto constexpr max_clusters{ hmp_clusters_info::max_clusters };
        static auto constexpr max_power   { 128 };

        hardware_concurrency_t cores[ max_clusters ];
        std::uint8_t           power[ max_clusters ]; // 'capacity' as in capacity aware scheduling

        std::uint8_t number_of_clusters;
    }; // struct hmp_config

    static inline hmp_config hmp_clusters;
public:
#else
    static bool constexpr hmp = false;
#endif

    enum struct priority : int
    {
    #ifdef BOOST_HAS_PTHREADS
        idle          =  19,
        background    =  10,
        low           =   5,
        normal        =   0,
        high          =  -5,
        foreground    = -10,
        time_critical = -20
    #else
        idle          = THREAD_PRIORITY_LOWEST, // TODO THREAD_MODE_BACKGROUND_BEGIN
        background    = THREAD_PRIORITY_LOWEST,
        low           = THREAD_PRIORITY_BELOW_NORMAL,
        normal        = THREAD_PRIORITY_NORMAL,
        high          = THREAD_PRIORITY_ABOVE_NORMAL,
        foreground    = THREAD_PRIORITY_HIGHEST,
        time_critical = THREAD_PRIORITY_TIME_CRITICAL
    #endif // thread backend
    }; // enum struct priority

#if BOOST_SWEATER_SPIN_BEFORE_SUSPENSION
    inline static std::uint32_t worker_spin_count = 1 * 1000;
#if BOOST_SWEATER_USE_CALLER_THREAD
    inline static std::uint32_t caller_spin_count = 1 * 1000;
#endif // BOOST_SWEATER_USE_CALLER_THREAD
#endif // BOOST_SWEATER_SPIN_BEFORE_SUSPENSION

#if BOOST_SWEATER_CALLER_BOOST
    inline static std::uint8_t caller_boost     = 0;
    inline static std::uint8_t caller_boost_max = 10;
#else
    static constexpr auto caller_boost     = 0;
    static constexpr auto caller_boost_max = 0;
#endif // BOOST_SWEATER_CALLER_BOOST
    static auto const caller_boost_weight = 128;

    static auto const min_parallel_iter_boost_weight = 64;
#if BOOST_SWEATER_USE_PARALLELIZATION_COST
    inline static std::uint8_t min_parallel_iter_boost = min_parallel_iter_boost_weight;
#endif // BOOST_SWEATER_USE_PARALLELIZATION_COST

private:
    struct worker_traits : functionoid::default_traits
    {
        static constexpr auto copyable    = functionoid::support_level::na     ;
        static constexpr auto moveable    = functionoid::support_level::nofail ;
        static constexpr auto destructor  = functionoid::support_level::trivial;
        static constexpr auto is_noexcept = true;
        static constexpr auto rtti        = false;
    }; // struct worker_traits

    using worker_counter = std::atomic<hardware_concurrency_t>;

    //...mrmlj...lighterweight alternatives to std::thread...to be moved to a separate lib...
#ifdef BOOST_HAS_PTHREADS
    class pthread_condition_variable;
    class pthread_mutex
    {
    public:
        pthread_mutex() noexcept BOOST_NOTHROW_LITE
#       if !defined( NDEBUG ) && defined( PTHREAD_ERRORCHECK_MUTEX_INITIALIZER )
            : mutex_( PTHREAD_ERRORCHECK_MUTEX_INITIALIZER ) {}
#       else
            : mutex_( PTHREAD_MUTEX_INITIALIZER ) {}
#       endif // NDEBUG
        ~pthread_mutex() noexcept BOOST_NOTHROW_LITE { BOOST_VERIFY( pthread_mutex_destroy( &mutex_ ) == 0 ); }

        pthread_mutex( pthread_mutex && other ) noexcept : mutex_( other.mutex_ ) { other.mutex_ = PTHREAD_MUTEX_INITIALIZER; }
        pthread_mutex( pthread_mutex const & ) = delete;

        void   lock() noexcept BOOST_NOTHROW_LITE { BOOST_VERIFY( pthread_mutex_lock  ( &mutex_ ) == 0 ); }
        void unlock() noexcept BOOST_NOTHROW_LITE { BOOST_VERIFY( pthread_mutex_unlock( &mutex_ ) == 0 ); }

        bool try_lock() noexcept BOOST_NOTHROW_LITE { return pthread_mutex_trylock( &mutex_ ) == 0; }

    private: friend class pthread_condition_variable;
        pthread_mutex_t mutex_;
    }; // class pthread_mutex

    class pthread_condition_variable
    {
    public:
        pthread_condition_variable() noexcept BOOST_NOTHROW_LITE : cv_( PTHREAD_COND_INITIALIZER ) {}
       ~pthread_condition_variable() noexcept BOOST_NOTHROW_LITE { BOOST_VERIFY( pthread_cond_destroy( &cv_ ) == 0 ); }

        pthread_condition_variable( pthread_condition_variable && other ) noexcept : cv_( other.cv_ ) { other.cv_ = PTHREAD_COND_INITIALIZER; }
        pthread_condition_variable( pthread_condition_variable const & ) = delete;

        void notify_all() noexcept BOOST_NOTHROW_LITE { BOOST_VERIFY( pthread_cond_broadcast( &cv_ ) == 0 ); }
        void notify_one() noexcept BOOST_NOTHROW_LITE { BOOST_VERIFY( pthread_cond_signal   ( &cv_ ) == 0 ); }

        void wait( std::unique_lock<pthread_mutex> & lock   ) noexcept BOOST_NOTHROW_LITE { wait( *lock.mutex() ); }
        void wait( pthread_mutex & mutex /*must be locked*/ ) noexcept BOOST_NOTHROW_LITE { BOOST_VERIFY( pthread_cond_wait( &cv_, &mutex.mutex_ ) == 0 ); }

    private:
        pthread_cond_t cv_;
    }; // class pthread_condition_variable

    class thread_impl
    {
    public:
        using native_handle_type = pthread_t;
        using id                 = pthread_t;

        class affinity_mask
        {
        public:
            affinity_mask() noexcept { CPU_ZERO( &value_ ); }

            void add_cpu( unsigned const cpu_id ) noexcept { CPU_SET( cpu_id, &value_ ); }

        private: friend class shop;
            cpu_set_t value_;
        }; // class affinity_mask

        void join  () noexcept BOOST_NOTHROW_LITE { BOOST_VERIFY( pthread_join  ( handle_, nullptr ) == 0 ); handle_ = {}; }
        void detach() noexcept BOOST_NOTHROW_LITE { BOOST_VERIFY( pthread_detach( handle_          ) == 0 ); handle_ = {}; }
        auto get_id() const noexcept { return handle_; }

        static auto get_active_thread_id() noexcept BOOST_NOTHROW_LITE { return pthread_self(); }

    protected:
       // https://stackoverflow.com/questions/43819314/default-member-initializer-needed-within-definition-of-enclosing-class-outside
       constexpr  thread_impl() noexcept {};
                 ~thread_impl() noexcept = default;

        using thread_procedure = void * (*) ( void * );

        auto create( thread_procedure const start_routine, void * const arg ) noexcept BOOST_NOTHROW_LITE
        {
            auto const error( pthread_create( &handle_, nullptr, start_routine, arg ) );
            if ( BOOST_UNLIKELY( error ) )
            {
                BOOST_ASSUME( error == EAGAIN ); // any other error indicates a programmer error
                handle_ = {};
            }
            return error;
        }

    protected:
        native_handle_type handle_{};
    }; // class thread_impl

    using condition_variable = pthread_condition_variable;
    using mutex              = pthread_mutex;

    class pthread_semaphore
    {
    public:
        pthread_semaphore() noexcept { BOOST_VERIFY( ::sem_init   ( &handle_, 0, 0 ) == 0 ); }
       ~pthread_semaphore() noexcept { BOOST_VERIFY( ::sem_destroy( &handle_       ) == 0 ); }

        pthread_semaphore( pthread_semaphore const & ) = delete;

        bool try_wait() noexcept { return ::sem_trywait( &handle_ ) == 0; }
        void     wait() noexcept
        {
#       if BOOST_SWEATER_SPIN_BEFORE_SUSPENSION
            for ( auto spin_try{ 0U }; spin_try < worker_spin_count; ++spin_try )
            {
                if ( BOOST_LIKELY( try_wait() ) )
                    return;
                detail::nops( 8 );
            }
#       endif // BOOST_SWEATER_SPIN_BEFORE_SUSPENSION
            BOOST_VERIFY( ::sem_wait( &handle_ ) == 0 );
        }

        void signal(                              ) noexcept { BOOST_VERIFY( ::sem_post( &handle_ ) == 0 ); }

        void signal( hardware_concurrency_t count ) noexcept
#       ifdef __clang__
          __attribute__(( no_sanitize( "implicit-integer-truncation" ) ))
          __attribute__(( no_sanitize( "implicit-integer-sign-change" ) ))
          __attribute__(( no_sanitize( "undefined" ) ))
#       endif
        { while ( count-- ) signal(); }

    private:
        sem_t handle_;
    }; // class pthread_semaphore

#else // Win32
    // Strategies for Implementing POSIX Condition Variables on Win32 http://www.cs.wustl.edu/~schmidt/win32-cv-1.html
    // http://developers.slashdot.org/story/07/02/26/1211220/pthreads-vs-win32-threads
    // http://nasutechtips.blogspot.com/2010/11/slim-read-write-srw-locks.html

    class condition_variable;
    class mutex
    {
    public:
        mutex(                ) noexcept : lock_{ SRWLOCK_INIT } {}
        mutex( mutex && other ) noexcept : lock_{ other.lock_ } { other.lock_ = { SRWLOCK_INIT }; }
        mutex( mutex const &  ) = delete ;
       ~mutex(                ) = default;

        void   lock() noexcept { ::AcquireSRWLockExclusive( &lock_ ); }
        void unlock() noexcept { ::ReleaseSRWLockExclusive( &lock_ ); }

        bool try_lock() noexcept { return ::TryAcquireSRWLockExclusive( &lock_ ) != false; }

    private: friend class condition_variable;
        ::SRWLOCK lock_;
    }; // class mutex

    class condition_variable
    {
    public:
        using lock_t = std::unique_lock<mutex>;

        condition_variable(                             ) noexcept : cv_{ CONDITION_VARIABLE_INIT } {}
        condition_variable( condition_variable && other ) noexcept : cv_{ other.cv_ } { other.cv_ = { CONDITION_VARIABLE_INIT }; }
        condition_variable( condition_variable const &  ) = delete ;
       ~condition_variable(                             ) = default;

        void notify_all() noexcept { ::WakeAllConditionVariable( &cv_ ); }
        void notify_one() noexcept { ::WakeConditionVariable   ( &cv_ ); }

        void wait( lock_t & lock ) noexcept { BOOST_VERIFY( wait( lock, INFINITE ) ); }
        void wait( mutex  & m    ) noexcept { BOOST_VERIFY( wait( m   , INFINITE ) ); }

        bool wait( lock_t & lock                , std::uint32_t const milliseconds ) noexcept { return wait( *lock.mutex(), milliseconds ); }
        bool wait( mutex  & m /*must be locked*/, std::uint32_t const milliseconds ) noexcept
        {
            auto const result( ::SleepConditionVariableSRW( &cv_, &m.lock_, milliseconds, 0/*CONDITION_VARIABLE_LOCKMODE_SHARED*/ ) );
            BOOST_ASSERT( result || ::GetLastError() == ERROR_TIMEOUT );
            return result != false;
        }

    private:
        ::CONDITION_VARIABLE cv_;
    }; // class condition_variable

    class thread_impl
    {
    public:
        using native_handle_type = ::HANDLE;
        using id                 = ::DWORD ;

        class affinity_mask
        {
        public:
            void add_cpu( unsigned const cpu_id ) noexcept { value_ |= DWORD_PTR( 1 ) << cpu_id; }

        private: friend class shop;
            DWORD_PTR value_ = 0;
        }; // class affinity_mask

        BOOST_NOTHROW_LITE void join() noexcept
        {
            BOOST_VERIFY( ::WaitForSingleObjectEx( handle_, INFINITE, false ) == WAIT_OBJECT_0 );
        #ifndef NDEBUG
            DWORD exitCode;
            BOOST_VERIFY( ::GetExitCodeThread( handle_, &exitCode ) );
            BOOST_ASSERT( exitCode == 0 );
        #endif // NDEBUG
            detach();
        }

        BOOST_NOTHROW_LITE void detach() noexcept
        {
            BOOST_VERIFY( ::CloseHandle( handle_ ) );
            handle_ = {};
        }

        auto get_id    () const noexcept { return ::GetThreadId( handle_ ); }
        auto get_handle() const noexcept { return handle_; }

        static auto get_active_thread_id() noexcept { return ::GetCurrentThreadId(); }

    protected:
        thread_impl() = default;
       ~thread_impl() = default;

        using thread_procedure = PTHREAD_START_ROUTINE;

        BOOST_NOTHROW_LITE auto create( thread_procedure const start_routine, void * const arg ) noexcept
        {
            handle_ = ::CreateThread( nullptr, 0, start_routine, arg, 0, nullptr );
            if ( BOOST_UNLIKELY( handle_ == nullptr ) )
            {
                BOOST_ASSERT( ::GetLastError() == ERROR_NOT_ENOUGH_MEMORY ); // any other error indicates a programmer error
                return ERROR_NOT_ENOUGH_MEMORY;
            }
            return ERROR_SUCCESS;
        }

    protected:
        native_handle_type handle_{};
    }; // class thread_impl
#endif // BOOST_HAS_PTHREADS

    class thread : public thread_impl
    {
    private:
        class synchronized_invocation
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

            if constexpr ( fits_into_a_pointer< F > )
            {
                void * context;
                new ( &context ) Functor( std::forward<F>( functor ) );
                create
                (
                    []( void * context ) noexcept -> ret_t
                    {
#                   ifdef BOOST_GCC
#                       pragma GCC diagnostic push
#                       pragma GCC diagnostic ignored "-Wstrict-aliasing"
#                   endif // GCC
                        auto & tiny_functor( reinterpret_cast<Functor &>( context ) );
#                   ifdef BOOST_GCC
#                       pragma GCC diagnostic pop
#                   endif // GCC
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
#           ifdef BOOST_NO_EXCEPTIONS
                std::terminate();
#           elif 0 // disabled - avoid the overhead of (at least) <system_error>
                throw std::system_error( std::error_code( error, std::system_category() ), "Thread creation failed" );
#           else
                throw std::runtime_error( "Not enough resources to create a new thread" );
#           endif
            }
        }
    }; // class thread

    class spin_lock
    {
    public:
        spin_lock(                   ) noexcept = default;
        spin_lock( spin_lock const & ) = delete;

        void lock    () noexcept { while ( BOOST_UNLIKELY( !try_lock() ) ) [[ unlikely ]] { generic::detail::nop(); } }
        bool try_lock() noexcept { return !flag_.test_and_set( std::memory_order_acquire ); }
        void unlock  () noexcept { flag_.clear( std::memory_order_release ); }

    private:
        std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
    }; // class spin_lock

    // TODO
    // http://www.1024cores.net/home/lock-free-algorithms/eventcounts
    // https://github.com/facebook/folly/blob/master/folly/experimental/EventCount.h
#ifdef BOOST_HAS_PTHREADS
    using semaphore = pthread_semaphore;
#else
    class semaphore
    {
    public:
        semaphore() noexcept : value_{ 0 }, waiters_{ 0 } {}
#   ifndef NDEBUG
        ~semaphore() noexcept { BOOST_ASSERT( value_ == 0 ); BOOST_ASSERT( waiters_ == 0 ); }
#   endif

        void signal( hardware_concurrency_t const count = 1 ) noexcept
        {
#       if BOOST_SWEATER_EXACT_WORKER_SELECTION
            BOOST_ASSUME( count == 1 );
#       endif // BOOST_SWEATER_EXACT_WORKER_SELECTION
            {
                std::scoped_lock<mutex> lock{ mutex_ };
                value_.fetch_add( count, std::memory_order_relaxed );
            }
#       if BOOST_SWEATER_EXACT_WORKER_SELECTION
            BOOST_ASSUME( waiters_ <= 1 );
            condition_.notify_one();
#       else
            if ( count < waiters_ )
                for ( auto notified{ 0U }; notified < count; ++notified )
                    condition_.notify_one();
            else
                condition_.notify_all();
#       endif // BOOST_SWEATER_EXACT_WORKER_SELECTION
        }

        void wait() noexcept
        {
#       if BOOST_SWEATER_SPIN_BEFORE_SUSPENSION
            { // waiting for atomic_ref
                auto value{ value_.load( std::memory_order_relaxed ) };
                for ( auto spin_try{ 0U }; spin_try < worker_spin_count; ++spin_try )
                {
                    if ( ( value > 0 ) && value_.compare_exchange_weak( value, value - 1, std::memory_order_acquire, std::memory_order_relaxed ) ) [[ likely ]]
                        return;
                    detail::nops( 8 );
                }
                BOOST_ASSUME( value <= 0 );
            }
#       endif // BOOST_SWEATER_SPIN_BEFORE_SUSPENSION

            std::unique_lock<mutex> lock{ mutex_ };
            ++waiters_;
            while ( value_.load( std::memory_order_relaxed ) <= 0 ) // support spurious wakeups
                condition_.wait( lock );
            value_.fetch_sub( 1, std::memory_order_relaxed );
            --waiters_;
        }

    private:
        std::atomic<std::int32_t> value_    ; // atomic to support spin-waits
        hardware_concurrency_t    waiters_  ; // to enable detecion when notify_all() can be used
        mutex                     mutex_    ;
        condition_variable        condition_;
    }; // class semaphore
#endif // pthreads?

    class barrier
    {
    public:
        barrier() noexcept : barrier( 0 ) {}
        barrier( hardware_concurrency_t const initial_value ) noexcept : counter_( initial_value ) {}

        void initialize( hardware_concurrency_t const initial_value ) noexcept
        {
            BOOST_ASSERT_MSG( counter_ == 0, "Already initialized" );
            counter_ = initial_value;
        }

        void add_expected_arrival() noexcept { counter_.fetch_add( 1, std::memory_order_relaxed ); }

#   if BOOST_SWEATER_USE_CALLER_THREAD
        void use_spin_wait( bool const value ) noexcept { spin_wait_ = value; }
#   endif // BOOST_SWEATER_USE_CALLER_THREAD

        void arrive() noexcept
        {
            BOOST_ASSERT( counter_ > 0 );
#       if BOOST_SWEATER_USE_CALLER_THREAD
            if ( BOOST_LIKELY( spin_wait_ ) )
            {
                counter_.fetch_sub( 1, std::memory_order_seq_cst );
                return;
            }
#       endif // BOOST_SWEATER_USE_CALLER_THREAD
            bool everyone_arrived;
            {
                std::scoped_lock<mutex> lock( mutex_ );
                BOOST_ASSERT( counter_ > 0 );
                everyone_arrived = counter_.fetch_sub( 1, std::memory_order_relaxed ) == 1;
            }
            if ( BOOST_UNLIKELY( everyone_arrived ) )
                event_.notify_one();
        }

        void wait() noexcept
        {
#       if BOOST_SWEATER_USE_CALLER_THREAD
            BOOST_ASSERT( !spin_wait_ );
#       endif // BOOST_SWEATER_USE_CALLER_THREAD
            std::scoped_lock<mutex> lock( mutex_ );
            while ( BOOST_UNLIKELY( counter_.load( std::memory_order_relaxed ) != 0 ) )
                event_.wait( mutex_ );
        }

#   if BOOST_SWEATER_USE_CALLER_THREAD
#       ifdef __clang__
          __attribute__(( no_sanitize( "implicit-integer-truncation" ) ))
          __attribute__(( no_sanitize( "implicit-integer-sign-change" ) ))
          __attribute__(( no_sanitize( "undefined" ) ))
#       endif
        BOOST_NOINLINE void spin_wait() noexcept
        {
            BOOST_ASSERT( spin_wait_ );

#       if BOOST_SWEATER_SPIN_BEFORE_SUSPENSION
            auto spin_tries{ caller_spin_count };
            while ( spin_tries-- )
            {
#       endif // BOOST_SWEATER_SPIN_BEFORE_SUSPENSION
                if ( BOOST_LIKELY( counter_.load( std::memory_order_acquire ) == 0 ) )
                {
#               if BOOST_SWEATER_CALLER_BOOST
                    caller_boost = std::max<std::int8_t>( 0, caller_boost - 1 );
#               endif // BOOST_SWEATER_CALLER_BOOST
                    return;
                }
#       if BOOST_SWEATER_SPIN_BEFORE_SUSPENSION
                detail::nops( 8 );
            }
#       endif // BOOST_SWEATER_SPIN_BEFORE_SUSPENSION

            events::caller_stalled( caller_boost );
#       if BOOST_SWEATER_CALLER_BOOST
            caller_boost = std::min<std::uint8_t>( caller_boost_max, caller_boost + 1 );
#       endif // BOOST_SWEATER_CALLER_BOOST
            while ( BOOST_UNLIKELY( counter_.load( std::memory_order_acquire ) != 0 ) )
            {
                std::this_thread::yield();
            }
        }
#   endif // BOOST_SWEATER_USE_CALLER_THREAD
    private:
        // http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2406.html#gen_cond_var
        worker_counter     counter_;
#   if BOOST_SWEATER_USE_CALLER_THREAD
        bool               spin_wait_{ false };
#   endif // BOOST_SWEATER_USE_CALLER_THREAD
        mutex              mutex_  ;
        condition_variable event_  ;
    }; // class barrier

    struct spread_work_base
    {
        void const   * p_work              ;
        iterations_t   start_iteration     ;
        iterations_t   end_iteration       ;
        barrier      * p_completion_barrier;
#   if BOOST_SWEATER_EXACT_WORKER_SELECTION && !defined( _WIN32 ) && !defined( NDEBUG ) && 0 //...mrmlj...only for debugging and overflows work_t's SBO storage
        worker_thread * p_thread;
#   endif // BOOST_SWEATER_EXACT_WORKER_SELECTION
    }; // struct spread_work_base

    using work_t = functionoid::callable<void(), worker_traits>;

    using my_queue = queues::mpmc_moodycamel<work_t>;

    template < typename Functor >
    static constexpr bool const fits_into_a_pointer =
        ( sizeof ( Functor ) <= sizeof ( void * ) )     &&
        std::is_trivially_copy_constructible_v<Functor> &&
        std::is_trivially_destructible_v      <Functor>;

    auto worker_loop( [[ maybe_unused ]] hardware_concurrency_t const worker_index ) noexcept
    {
        /// \note BOOST_SWEATER_EXACT_WORKER_SELECTION requires the worker
        /// index to be captured by the worker lambda - this makes it larger
        /// than a void pointer and thus requries the 'synchronized_invocation'
        /// path in the create thread call. This in turn causes deadlocks on
        /// Windows when shops are created as global variables in DLLs (as the
        /// DLL entry procedure then blocks when it must not do so). As a
        /// workaround the worker index and the shop instance pointer are
        /// packed together (expecting to never be further apart than 2GB,
        /// avoiding the 64bit address canonical form pointer tagging
        /// complexities).
        /// https://lwn.net/Articles/718888
        /// https://source.android.com/devices/tech/debug/tagged-pointers
        ///                                   (13.08.2020.) (Domagoj Saric)
#   if BOOST_SWEATER_EXACT_WORKER_SELECTION && ( defined( _WIN64 ) || defined( __LP64__ ) )
        static std::byte dummy_reference_object{};
        auto const shop_offset{ reinterpret_cast<std::byte const *>( this ) - &dummy_reference_object };
        struct shop_and_worker_t
        {
            std:: int64_t shop_offset  : 48;
            std::uint64_t worker_index : 16;
        } const shop_and_worker{ .shop_offset = shop_offset, .worker_index = worker_index };
        BOOST_ASSERT( shop_and_worker.shop_offset  == shop_offset  );
        BOOST_ASSERT( shop_and_worker.worker_index == worker_index );
#   elif BOOST_SWEATER_EXACT_WORKER_SELECTION && defined( _WIN32 )
        static auto const p_base{ reinterpret_cast<std::byte const *>( &__ImageBase ) };
        auto const shop_offset{ static_cast<std::uint32_t>( reinterpret_cast<std::byte const *>( this ) - p_base ) };
        struct shop_and_worker_t
        {
            std::uint32_t shop_offset  : 27;
            std::uint32_t worker_index :  5;
        } const shop_and_worker{ .shop_offset = shop_offset, .worker_index = worker_index };
        BOOST_ASSERT( shop_and_worker.shop_offset  == shop_offset  );
        BOOST_ASSERT( shop_and_worker.worker_index == worker_index );
#   else
        auto const p_shop{ this };
#   endif
        auto worker_loop_impl
        {
            [=]() noexcept
            {
#           if   BOOST_SWEATER_EXACT_WORKER_SELECTION && ( defined( _WIN64 ) || defined( __LP64__ ) )
                auto & parent{ const_cast< shop & >( *reinterpret_cast<shop const *>( &dummy_reference_object + shop_and_worker.shop_offset ) ) };
                auto const worker_index{ static_cast<hardware_concurrency_t>( shop_and_worker.worker_index ) };
#           elif BOOST_SWEATER_EXACT_WORKER_SELECTION && defined( _WIN32 )
                auto & parent{ const_cast< shop & >( *reinterpret_cast<shop const *>( p_base + shop_and_worker.shop_offset ) ) };
                auto const worker_index{ shop_and_worker.worker_index };
#           elif BOOST_SWEATER_EXACT_WORKER_SELECTION
                auto & parent{ *p_shop };
#           else
                auto & parent{ *p_shop };
                auto const worker_index{ static_cast<hardware_concurrency_t>( -1 ) };
#           endif // BOOST_SWEATER_EXACT_WORKER_SELECTION

#           if BOOST_SWEATER_EXACT_WORKER_SELECTION
#           ifdef __linux__
                parent.pool_[ worker_index ].thread_id_ = ::gettid();
#           endif // linux
                auto       & __restrict producer_token{ *parent.pool_[ worker_index ].token_ };
                auto       & __restrict work_event    {  parent.pool_[ worker_index ].event_ };
#           else // BOOST_SWEATER_EXACT_WORKER_SELECTION
                auto       & __restrict work_event    { parent.work_semaphore_ };
#           endif // BOOST_SWEATER_EXACT_WORKER_SELECTION
                auto       & __restrict queue         { parent.queue_  };
                auto const & __restrict exit          { parent.brexit_ };

                auto consumer_token{ queue.consumer_token() };

                work_t work;

                for ( ; ; )
                {
#               if BOOST_SWEATER_EXACT_WORKER_SELECTION
                    while ( BOOST_LIKELY( queue.dequeue_from_producer( work, producer_token ) ) ) [[ likely ]]
                    {
                        events::worker_work_begin( worker_index );
                        work();
                        events::worker_work_end  ( worker_index );
                        parent.work_completed();
                        detail::nops( 8 );
                    }
#               endif
                    // Work stealing for BOOST_SWEATER_EXACT_WORKER_SELECTION
                    if ( queue.dequeue( work, consumer_token ) ) [[ likely ]]
                    {
                        events::worker_work_begin( worker_index );
                        work();
                        events::worker_work_end  ( worker_index );
                        parent.work_completed();
                    }
                    else
                    {
                        if ( BOOST_UNLIKELY( exit.load( std::memory_order_relaxed ) ) )
                            return;
                        events::worker_sleep_begin( worker_index );
                        work_event.wait();
                        events::worker_sleep_end  ( worker_index );
                    }
                }
            }
        }; // worker_loop_impl
#   ifdef _WIN32
        static_assert
        (
            fits_into_a_pointer< decltype( worker_loop_impl ) >,
            "This worker_loop_impl will cause a dead lock in the constructor of global shops inside DLLs under Windows"
        );
#   endif // Windows
        return worker_loop_impl;
    }

    struct spread_worker_template_traits : worker_traits
    {
        static constexpr auto copyable = functionoid::support_level::trivial;
        static constexpr auto moveable = functionoid::support_level::nofail ;
    }; // struct worker_traits

    using spread_work_template_t = functionoid::callable<void(), spread_worker_template_traits>;

    auto number_of_worker_threads() const noexcept
    {
        auto const worker_threads{ static_cast<hardware_concurrency_t>( pool_.size() ) };
#   if BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
        BOOST_ASSUME( worker_threads <= BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY );
#   endif
        return worker_threads;
    }

public:
    shop()
    {
#   ifdef __GNUC__ // compilers with init_priority attribute (see hardware_concurency.hpp)
        auto const local_hardware_concurrency( hardware_concurrency_max );
#   else
        /// \note Avoid the static-initialization-order-fiasco (for compilers
        /// not supporting the init_priority attribute) by not using the
        /// global hardware_concurrency_max variable (i.e. allow users to
        /// safely create plain global-variable sweat_shop singletons).
        ///                                   (01.05.2017.) (Domagoj Saric)
        auto const local_hardware_concurrency( sweater::detail::get_hardware_concurrency_max() );
#   endif // __GNUC__
        create_pool( local_hardware_concurrency - BOOST_SWEATER_USE_CALLER_THREAD );
    }

    ~shop() noexcept { stop_and_destroy_pool(); }

    hardware_concurrency_t number_of_workers() const noexcept
    {
        auto const actual_number_of_workers{ number_of_worker_threads() + BOOST_SWEATER_USE_CALLER_THREAD };
#   if BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
        BOOST_ASSUME( actual_number_of_workers <= BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY );
#   endif
        return static_cast<hardware_concurrency_t>( actual_number_of_workers );
    }

    /// For GCD dispatch_apply/OMP-like parallel loops.
    /// \details Guarantees that <VAR>work</VAR> will not be called more than
    /// <VAR>iterations</VAR> times (even if number_of_workers() > iterations).
    template <typename F>
    bool spread_the_sweat( iterations_t const iterations, F && __restrict work, iterations_t const parallelizable_iterations_count = 1 ) noexcept
    {
        static_assert( noexcept( work( iterations, iterations ) ), "F must be noexcept" );

#   if 0 // worth it...not worth it...
        if ( BOOST_UNLIKELY( iterations == 0 ) )
            return true;
#   endif
#   if 0 // worth it...not worth it...
        if ( BOOST_UNLIKELY( iterations == 1 ) )
        {
            work( 0, 1 );
            return true;
        }
#   endif

        struct spread_wrapper : spread_work_base
        {
            void operator()() noexcept
            {
#           if BOOST_SWEATER_EXACT_WORKER_SELECTION && !defined( _WIN32 ) && !defined( NDEBUG ) && 0 //...mrmlj...todo
                auto const tid{ ::gettid() };
                BOOST_ASSERT( tid == p_thread->thread_id );
#           endif // BOOST_SWEATER_EXACT_WORKER_SELECTION
                BOOST_ASSUME( start_iteration < end_iteration );
                auto & __restrict work( *static_cast<std::decay_t<F> *>( const_cast< void * >( p_work ) ) );
                work( start_iteration, end_iteration );
                p_completion_barrier->arrive();
            }
        }; // struct spread_wrapper
        static_assert( std::is_standard_layout_v<spread_wrapper> ); // required for correctness of work_t::target_as() usage
#   ifdef BOOST_GCC
#       pragma GCC diagnostic push
#       pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#   endif // GCC
#   ifndef BOOST_MSVC // 16.7 ICE
        BOOST_ASSERT( spread_work_template_t{ spread_wrapper{{ .p_work = &work }} }.target_as<spread_work_base>().p_work == &work );
#   endif // BOOST_MSVC
        return spread_work( spread_wrapper{{ .p_work = &work }}, iterations, parallelizable_iterations_count );
#   ifdef BOOST_GCC
#       pragma GCC diagnostic pop
#   endif // GCC
    }

    template <typename F>
    bool fire_and_forget( F && work ) noexcept( noexcept( std::is_nothrow_constructible_v<std::remove_reference_t<F>, F &&> ) )
    {
        using Functor = std::remove_reference_t<F>;
        return create_fire_and_destroy<Functor>( std::forward<F>( work ) );
    }

    template <typename F>
    auto dispatch( F && work )
    {
        // http://scottmeyers.blogspot.hr/2013/03/stdfutures-from-stdasync-arent-special.html
        using Functor = std::remove_reference_t<F>;
        struct future_wrapper
        {
            // Note: Clang v8 and v9 think that result_t is unused
        #ifdef __clang__
        #   pragma clang diagnostic push
        #   pragma clang diagnostic ignored "-Wunused-local-typedef"
        #endif
            using result_t  = decltype( std::declval<Functor &>()() );
        #ifdef __clang__
        #   pragma clang diagnostic pop
        #endif
            using promise_t = std::promise<result_t>;
            using future_t  = std::future <result_t>;

            future_wrapper( F && work_source, future_t & future )
                :
                work( std::forward<F>( work_source ) )
            {
                future = promise.get_future();
            }

            void operator()() noexcept
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

            Functor   work   ;
            promise_t promise;
        }; // struct future_wrapper

        typename future_wrapper::future_t future;
        auto const dispatch_succeeded( this->create_fire_and_destroy<future_wrapper>( std::forward<F>( work ), future ) );
        if ( BOOST_UNLIKELY( !dispatch_succeeded ) )
        {
            typename future_wrapper::promise_t failed_promise;
            failed_promise.set_exception( std::make_exception_ptr( std::bad_alloc() ) );
            future = failed_promise.get_future();
        }
        return future;
    }

    BOOST_ATTRIBUTES( BOOST_MINSIZE )
    bool set_priority( priority const new_priority ) noexcept
    {
#   ifdef __EMSCRIPTEN__
        if constexpr ( true )
            return ( new_priority == priority::normal );
#   endif
#   ifdef __ANDROID__
        /// \note Android's pthread_setschedparam() does not actually work so we
        /// have to abuse the general Linux' setpriority() non-POSIX compliance
        /// (i.e. that it sets the calling thread's priority).
        /// http://stackoverflow.com/questions/17398075/change-native-thread-priority-on-android-in-c-c
        /// https://android.googlesource.com/platform/dalvik/+/gingerbread/vm/alloc/Heap.c
        /// https://developer.android.com/topic/performance/threads.html
        /// _or_
        /// try the undocumented things the Java Process.setThreadPriority()
        /// function seems to be doing:
        /// https://github.com/android/platform_frameworks_base/blob/master/core/java/android/os/Process.java#L634
        /// https://github.com/android/platform_frameworks_base/blob/master/core/jni/android_util_Process.cpp#L475
        /// https://android.googlesource.com/platform/frameworks/native/+/jb-dev/libs/utils/Threads.cpp#329
        ///                                   (03.05.2017.) (Domagoj Saric)
#   endif
        auto const nice_value( static_cast<int>( new_priority ) );
        bool success( true );
        for ( auto & thread : pool_ )
        {
#   ifdef BOOST_HAS_PTHREADS
#       if defined( __ANDROID__ )
            success &= ( ::setpriority( PRIO_PROCESS, thread.native_handle(), nice_value ) == 0 );
#       else
            std::uint8_t const api_range            ( static_cast<std::int8_t>( priority::idle ) - static_cast<std::int8_t>( priority::time_critical ) );
            auto         const platform_range       ( detail::default_policy_priority_range );
            auto         const uninverted_nice_value( static_cast<std::uint8_t>( - ( nice_value - static_cast<std::int8_t>( priority::idle ) ) ) );
            int          const priority_value       ( detail::default_policy_priority_min + detail::round_divide( uninverted_nice_value * platform_range, api_range ) ); // surely it will be hoisted
#           if defined( __APPLE__ )
            BOOST_ASSERT( !detail::default_policy_priority_unchangable );
            ::sched_param scheduling_parameters;
            int           policy;
            auto const handle( thread.native_handle() );
            BOOST_VERIFY( pthread_getschedparam( handle, &policy, &scheduling_parameters ) == 0 );
            scheduling_parameters.sched_priority = priority_value;
            success &= ( pthread_setschedparam( handle, policy, &scheduling_parameters ) == 0 );
#           else
            success &= !detail::default_policy_priority_unchangable && ( pthread_setschedprio( thread.native_handle(), priority_value ) == 0 );
#           endif
#       endif // __ANDROID__
#   else // thread backend
            /// \note SetThreadPriority() silently falls back to the highest
            /// priority level available to the caller based on its privileges
            /// (instead of failing).
            ///                               (23.06.2017.) (Domagoj Saric)
            BOOST_VERIFY( ::SetThreadPriority( thread.native_handle(), nice_value ) != false );
            success &= true;
#   endif // thread backend
        }

#   if defined( __linux ) // also on Android
        if ( !success )
        {
#       if BOOST_SWEATER_HMP
            auto hmp_setting{ hmp };
            hmp = false;
#       endif // BOOST_SWEATER_HMP
#       if BOOST_SWEATER_USE_CALLER_THREAD
            auto const caller_id{ ::gettid() };
#       endif // BOOST_SWEATER_USE_CALLER_THREAD
            success = true;
            spread_the_sweat
            (
                hardware_concurrency_max,
                [ =, &success ]( iterations_t, iterations_t /*worker index?*/ ) noexcept
                {
#               if BOOST_SWEATER_USE_CALLER_THREAD
                    /// \note Do not change the caller thread's priority.
                    ///                       (05.05.2017.) (Domagoj Saric)
                    if ( ::gettid() != caller_id )
#               endif // BOOST_SWEATER_USE_CALLER_THREAD
                    {
                        auto const result( ::setpriority( PRIO_PROCESS, 0, nice_value ) );
                        BOOST_ASSERT( ( result == 0 ) || ( errno == EACCES ) );
                        success &= ( result == 0 );
                    }
                }
            );
#       if BOOST_SWEATER_HMP
            hmp = hmp_setting;
#       endif // BOOST_SWEATER_HMP
        }
#   endif // __linux
        return success;
    }

    using cpu_affinity_mask = thread::affinity_mask;

    BOOST_ATTRIBUTES( BOOST_COLD )
    auto bind_worker( hardware_concurrency_t const worker_index, cpu_affinity_mask const mask ) noexcept
    {
        auto & thread{ pool_[ worker_index ] };
#   ifdef _WIN32
        return ::SetThreadAffinityMask( thread.get_handle(), mask.value_ ) != 0;
#   else // platform
#       if 0 // Android does not have pthread_setaffinity_np or pthread_attr_setaffinity_np
         // and there seems to be no way of detecting its presence.
        return pthread_setaffinity_np( thread.get_id(), sizeof( mask.value_ ), &mask.value_ ) == 0;
#       elif BOOST_SWEATER_EXACT_WORKER_SELECTION
        return sched_setaffinity( thread.thread_id_, sizeof( mask.value_ ), &mask.value_ ) == 0;
#       else
#       if BOOST_SWEATER_HMP
        auto hmp_setting{ hmp };
        hmp = false;
#       endif // BOOST_SWEATER_HMP
        int result{ 2 };
        spread_the_sweat
        (
            number_of_workers(),
            [ &, target_handle = thread.get_id() ]( iterations_t, iterations_t ) noexcept
            {
                auto const current{ pthread_self() };
                if ( current == target_handle )
                {
                    BOOST_ASSERT( result == 2 );
                    result = sched_setaffinity( ::gettid(), sizeof( mask.value_ ), &mask.value_ );
                    BOOST_ASSERT( result != 2 );
                }
            }
        );
#       if BOOST_SWEATER_HMP
        hmp = hmp_setting;
#       endif // BOOST_SWEATER_HMP
        BOOST_ASSERT( result != 2 );
        return result == 0;
#       endif // platform
#   endif // platform
    }

    BOOST_ATTRIBUTES( BOOST_COLD )
    auto bind_worker_to_cpu( hardware_concurrency_t const worker_index, unsigned const cpu_id ) noexcept
    {
        cpu_affinity_mask mask;
        mask.add_cpu( cpu_id );
        return bind_worker( worker_index, mask );
    }

    BOOST_ATTRIBUTES( BOOST_COLD )
    void set_max_allowed_threads( hardware_concurrency_t const max_threads )
    {
        BOOST_ASSERT_MSG( queue_.empty(), "Cannot change parallelism level while items are in queue." );
        BOOST_ASSERT_MSG( !hmp          , "Cannot change number of workers directly when HMP is enabled" );
        stop_and_destroy_pool();
        brexit_.store( false, std::memory_order_relaxed );
        create_pool( max_threads - BOOST_SWEATER_USE_CALLER_THREAD );
        BOOST_ASSERT( number_of_workers() == max_threads );
    }

    auto number_of_items() const noexcept
    {
#   if 0
        return queue_.depth();
#   else
        return work_items_.load( std::memory_order_acquire );
#   endif
    }

#   if BOOST_SWEATER_HMP
    BOOST_ATTRIBUTES( BOOST_COLD )
    void configure_hmp( hmp_clusters_info const config, std::uint8_t const number_of_clusters )
    {
        BOOST_ASSERT( number_of_clusters <= config.max_clusters );
        hmp_clusters.number_of_clusters = number_of_clusters;

        hardware_concurrency_t number_of_cores{ 0 };

        float cluster_capacities[ config.max_clusters ];
        float total_power{ 0 };
        for ( auto cluster{ 0 }; cluster < number_of_clusters; ++cluster )
        {
            hmp_clusters.cores[ cluster ] = config.cores[ cluster ];
            cluster_capacities[ cluster ] = config.power[ cluster ];
            number_of_cores += hmp_clusters.cores[ cluster ];
            total_power     += cluster_capacities[ cluster ];
        }

        std::uint8_t final_total_power{ 0 };
        for ( auto cluster{ 0 }; cluster < number_of_clusters; ++cluster )
        {
            hmp_clusters.power[ cluster ] = static_cast<std::uint8_t>( cluster_capacities[ cluster ] / total_power * hmp_config::max_power );
            final_total_power += hmp_clusters.power[ cluster ];
        }
        BOOST_ASSERT // max integer rounding error
        (
            final_total_power >= ( hmp_config::max_power - ( number_of_clusters - 1 ) ) &&
            final_total_power <= ( hmp_config::max_power + ( number_of_clusters - 1 ) )
        );
        auto const  overflow_err{ static_cast<std::uint8_t>( std::max( 0, final_total_power     - hmp_config::max_power ) ) };
        auto const underflow_err{ static_cast<std::uint8_t>( std::max( 0, hmp_config::max_power - final_total_power     ) ) };
        hmp_clusters.power[ number_of_clusters - 1 ] -=  overflow_err;
        hmp_clusters.power[ 0                      ] += underflow_err;

        final_total_power -=  overflow_err;
        final_total_power += underflow_err;
        BOOST_ASSERT( final_total_power == hmp_config::max_power );

        create_pool( number_of_cores - BOOST_SWEATER_USE_CALLER_THREAD );
    }
#   endif // BOOST_SWEATER_HMP

private:
    BOOST_ATTRIBUTES( BOOST_COLD )
    void create_pool( hardware_concurrency_t const size )
    {
        BOOST_ASSERT_MSG( size <= sweater::detail::get_hardware_concurrency_max(), "Requested parallelism level not offered in hardware." );
        auto const current_size( pool_.size() );
        if ( size == current_size )
            return;
#   if BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
        pool_.resize( size );
#   else
        auto p_workers( std::make_unique<worker_thread[]>( size ) );
        pool_ = make_iterator_range_n( p_workers.get(), size );
#   endif // !BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY

        for ( hardware_concurrency_t worker_index{ 0 }; worker_index < size; ++worker_index )
        {
#       if BOOST_SWEATER_EXACT_WORKER_SELECTION
            pool_[ worker_index ].token_.emplace( queue_.producer_token() );
#       endif // BOOST_SWEATER_EXACT_WORKER_SELECTION
            pool_[ worker_index ] = worker_loop( worker_index );
        }
#   if !BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
        p_workers.release();
#   endif // !BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
#   if BOOST_SWEATER_CALLER_BOOST
        caller_boost = 0;
#   endif // BOOST_SWEATER_CALLER_BOOST
    }


    BOOST_ATTRIBUTES( BOOST_COLD )
    void stop_and_destroy_pool() noexcept
    {
        brexit_.store( true, std::memory_order_relaxed );
        wake_all_workers();
        for ( auto & worker : pool_ )
            worker.join();
#   if BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
        pool_.clear();
#   else
        delete[] pool_.begin();
        pool_ = {};
#   endif // BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
    }

    BOOST_NOINLINE
    void perform_caller_work
    (
        iterations_t                   const iterations,
        spread_work_template_t const &       work_part_template,
        barrier                      &       completion_barrier
    ) noexcept
    {
        events::caller_work_begin( iterations );
        work_t caller_chunk{ work_part_template };
        auto & chunk_setup{ caller_chunk.target_as<spread_work_base>() };
        chunk_setup.start_iteration = 0;
        chunk_setup.  end_iteration = iterations;
        BOOST_ASSERT( chunk_setup.p_completion_barrier == &completion_barrier );
        completion_barrier.add_expected_arrival();
        work_added    ();
        caller_chunk  ();
        work_completed();
        events::caller_work_end();
    }

#if BOOST_SWEATER_EXACT_WORKER_SELECTION
    auto dispatch_workers
    (
        hardware_concurrency_t               worker_index,
        iterations_t                         iteration,
        hardware_concurrency_t         const max_parts,
        iterations_t                   const per_part_iterations,
        iterations_t                   const parts_with_extra_iteration,
        iterations_t                   const iterations, // total/max for the whole spread (not necessary all for this call)
        barrier                      &       completion_barrier,
        spread_work_template_t const &       work_part_template
    ) noexcept
    {
        for ( hardware_concurrency_t work_part{ 0 }; work_part < max_parts && iteration != iterations; ++work_part )
        {
            events::worker_enqueue_begin( worker_index );
            auto const start_iteration{ iteration };
            auto const extra_iteration{ work_part < parts_with_extra_iteration };
            auto const end_iteration  { static_cast<iterations_t>( start_iteration + per_part_iterations + extra_iteration ) };
            BOOST_ASSERT( work_part_template.target_as<spread_work_base>().p_completion_barrier == &completion_barrier );
            work_t work_chunk{ work_part_template };
            auto & chunk_setup{ work_chunk.target_as<spread_work_base>() };
            chunk_setup.start_iteration = start_iteration;
            chunk_setup.  end_iteration =   end_iteration;
            BOOST_ASSERT( chunk_setup.p_completion_barrier == &completion_barrier );
            BOOST_ASSERT( chunk_setup.start_iteration < chunk_setup.end_iteration );
            // Have to do incremental/'dynamic' 'number of work parts' counting
            // as the rounding and load balancing logic can produce a smaller
            // number than the simple number_of_work_parts calculation in the
            // non-HMP case.
            completion_barrier.add_expected_arrival();
            work_added();
            BOOST_VERIFY( pool_[ worker_index ].enqueue( std::move( work_chunk ), queue_ ) ); //...mrmlj...todo err handling
            iteration = end_iteration;
            events::worker_enqueue_end( worker_index, end_iteration - start_iteration );
            ++worker_index;
        }

        return std::make_pair( worker_index, iteration );
    }
#endif // BOOST_SWEATER_EXACT_WORKER_SELECTION

    BOOST_NOINLINE
    bool BOOST_CC_REG spread_work
    (
        spread_work_template_t       work_part_template,
        iterations_t           const iterations,
        iterations_t                 parallelizable_iterations_count
    ) noexcept
    {
        if ( BOOST_UNLIKELY( iterations == 0 ) ) [[ unlikely ]]
            return true;

        barrier completion_barrier;
        work_part_template.target_as<spread_work_base>().p_completion_barrier = &completion_barrier;

        auto const items_in_shop{ number_of_items() };
        if ( BOOST_UNLIKELY( items_in_shop ) ) [[ unlikely ]]
        { // support recursive spread_the_sweat calls: for now just perform everything in the caller
            auto const this_thread{ thread::get_active_thread_id() };
            for ( auto const & worker : pool_ )
            {
                if ( BOOST_UNLIKELY( worker.get_id() == this_thread ) ) [[ unlikely ]]
                {
                    perform_caller_work( iterations, work_part_template, completion_barrier );
                    return true;
                }
            }
        }

        events::spread_begin( iterations );

#   if BOOST_SWEATER_USE_PARALLELIZATION_COST
        parallelizable_iterations_count = parallelizable_iterations_count * min_parallel_iter_boost / min_parallel_iter_boost_weight;
        parallelizable_iterations_count = std::max<iterations_t>( 1, parallelizable_iterations_count );
#   else
        parallelizable_iterations_count = 1;
#   endif // BOOST_SWEATER_USE_PARALLELIZATION_COST

        auto const actual_number_of_workers{ number_of_workers() };
        auto const free_workers            { std::max<int>( 0, actual_number_of_workers - items_in_shop ) };
        auto const max_work_parts          { free_workers ? free_workers : number_of_worker_threads() }; // prefer using any available worker - otherwise queue and wait
        auto const use_caller_thread       { BOOST_SWEATER_USE_CALLER_THREAD && free_workers };

#   if BOOST_SWEATER_USE_CALLER_THREAD
        completion_barrier.use_spin_wait( use_caller_thread );
#   endif // BOOST_SWEATER_USE_CALLER_THREAD

        hardware_concurrency_t dispatched_parts;
        bool enqueue_succeeded;
#   if BOOST_SWEATER_HMP
        static_assert( BOOST_SWEATER_EXACT_WORKER_SELECTION );
        if ( BOOST_LIKELY( hmp ) ) [[ likely ]]
        {
            BOOST_ASSERT_MSG( hmp_clusters.number_of_clusters, "HMP not configured" );
            BOOST_ASSUME( hmp_clusters.number_of_clusters <= hmp_clusters.max_clusters );

            std::uint8_t number_used_of_clusters{ 0 };
            iterations_t hmp_distributions[ hmp_clusters.max_clusters ];
            {
                iterations_t iteration{ 0 };
                for ( auto cluster{ 0 }; cluster < hmp_clusters.number_of_clusters; ++cluster )
                {
                    auto const cluster_power              { hmp_clusters.power[ cluster ] };
                    auto const cluster_cores              { hmp_clusters.cores[ cluster ] };
                    auto const weight_factor              { hmp_clusters.max_power        };
                    auto const weighted_cluster_iterations{ iterations * cluster_power / weight_factor };
                    auto const remaining_iterations       { static_cast<iterations_t>( iterations - iteration ) };
                    auto const cluster_iterations
                    {
                        std::min
                        (
                            remaining_iterations,
                            std::max<iterations_t>
                            (
                                weighted_cluster_iterations,
                                // this also makes sure we use all cores of this cluster
                                // before moving to the next one (i.e. even if
                                // parallelizable_iterations_count == 1)
                                parallelizable_iterations_count * cluster_cores
                            )
                        )
                    };
                    if ( BOOST_UNLIKELY( !cluster_iterations ) ) // handle small spreads (w/ iterations < number of cores)
                        break;
                    hmp_distributions[ cluster ]  = cluster_iterations;
                    iteration                    += cluster_iterations;
                    ++number_used_of_clusters;
                }
                BOOST_ASSUME( iteration <= iterations );
                BOOST_ASSUME( ( number_used_of_clusters > 0 ) && ( number_used_of_clusters <= hmp_clusters.max_clusters ) );
                auto const remaining_iterations{ iterations - iteration };
                BOOST_ASSERT( remaining_iterations < hmp_distributions[ number_used_of_clusters - 1 ] );
                hmp_distributions[ 0 ] += remaining_iterations; // add to strongest cluster
            }

            iterations_t           caller_thread_end_iteration{ 0 };
            iterations_t           iteration                  { 0 };
            hardware_concurrency_t worker                     { 0 };
            for ( auto cluster{ 0 }; cluster < number_used_of_clusters; ++cluster )
            {
                auto const remaining_workers{ max_work_parts - worker }; //...mrmlj...quick-fix for recusive and concurrent (but incomplete) spreads...think of a cleaner/'implicit' solution
#           if BOOST_SWEATER_CALLER_BOOST
                auto const cluster_iterations        { std::min<iterations_t>( hmp_distributions[ cluster ], iterations - iteration ) }; // guard in case caller_boost is in effect
#           else
                auto const cluster_iterations        { hmp_distributions [ cluster ] };
#           endif
                auto       cluster_cores             { std::min<hardware_concurrency_t>( remaining_workers, hmp_clusters.cores[ cluster ] ) };
                auto       per_core_iterations       { cluster_iterations / cluster_cores };
                auto       parts_with_extra_iteration{ cluster_iterations % cluster_cores };

                if ( use_caller_thread && ( cluster == 0 ) )
                {
                    BOOST_ASSUME( iteration == 0 );
                    auto const extra_iteration{ parts_with_extra_iteration != 0 };
                    caller_thread_end_iteration  = per_core_iterations + extra_iteration;
#               if BOOST_SWEATER_CALLER_BOOST
                    caller_thread_end_iteration += caller_thread_end_iteration * caller_boost / caller_boost_weight;
                    caller_thread_end_iteration  = std::min( caller_thread_end_iteration, cluster_iterations );
#               endif // BOOST_SWEATER_CALLER_BOOST
                    iteration = caller_thread_end_iteration;
                    --cluster_cores;

#               if BOOST_SWEATER_CALLER_BOOST
                    // caller boost readjustment
                    if ( cluster_cores ) // e.g. phones with one 'turbo-core'
                    {
                        auto const dispatched_iterations{ cluster_iterations - caller_thread_end_iteration };
                        per_core_iterations        = dispatched_iterations / cluster_cores;
                        parts_with_extra_iteration = dispatched_iterations % cluster_cores;
                    }
                    else
                    {
                        BOOST_ASSUME( caller_thread_end_iteration == cluster_iterations );
                    }
#               else
                    parts_with_extra_iteration -= extra_iteration;
#               endif // BOOST_SWEATER_CALLER_BOOST
                }

                std::tie( worker, iteration ) = dispatch_workers( worker, iteration, cluster_cores, per_core_iterations, parts_with_extra_iteration, iterations, completion_barrier, work_part_template );
            }
            enqueue_succeeded = true;

            if ( BOOST_LIKELY( use_caller_thread ) ) [[ likely ]]
            {
                perform_caller_work( caller_thread_end_iteration, work_part_template, completion_barrier );
            }

            dispatched_parts = worker;
        }
        else
#   endif // BOOST_SWEATER_HMP
        {
            auto parallelizable_parts           { std::max<iterations_t>( 1, iterations / parallelizable_iterations_count ) };
            auto number_of_work_parts           { static_cast<hardware_concurrency_t>( std::min<iterations_t>( parallelizable_parts, max_work_parts ) ) };
            auto number_of_dispatched_work_parts{ static_cast<hardware_concurrency_t>( std::max<int>( 0, number_of_work_parts - use_caller_thread ) ) };

            auto iterations_per_part       { iterations / number_of_work_parts };
            auto parts_with_extra_iteration{ iterations % number_of_work_parts };

#       if BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
            BOOST_ASSUME( number_of_dispatched_work_parts <= ( BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY - BOOST_SWEATER_USE_CALLER_THREAD ) );
#       endif // BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY

            iterations_t iteration{ 0 };
            iterations_t caller_thread_end_iteration{ 0 };
            if ( use_caller_thread )
            {
                auto const extra_iteration{ parts_with_extra_iteration != 0 };
                caller_thread_end_iteration  = iterations_per_part + extra_iteration;
#           if BOOST_SWEATER_CALLER_BOOST
                caller_thread_end_iteration += caller_thread_end_iteration * caller_boost / caller_boost_weight;
                caller_thread_end_iteration  = std::min( caller_thread_end_iteration, iterations );
#           endif // BOOST_SWEATER_CALLER_BOOST
                iteration                    = caller_thread_end_iteration;

#           if BOOST_SWEATER_CALLER_BOOST
                BOOST_ASSUME( free_workers ); // otherwise use_caller_thread would be false
                if ( number_of_dispatched_work_parts )
                {
                    auto const dispatched_iterations{ iterations - iteration };

                    parallelizable_parts            = std::max<hardware_concurrency_t>( 1, dispatched_iterations / parallelizable_iterations_count );
                    number_of_work_parts            = static_cast<hardware_concurrency_t>( std::min<iterations_t>( parallelizable_parts, free_workers - use_caller_thread ) );
                    number_of_dispatched_work_parts = number_of_work_parts;

                    iterations_per_part        = dispatched_iterations / number_of_dispatched_work_parts;
                    parts_with_extra_iteration = dispatched_iterations % number_of_dispatched_work_parts;
                }
                else
                {
                    BOOST_ASSUME( iteration == iterations );
                }
#           else
                parts_with_extra_iteration -= extra_iteration;
#           endif // BOOST_SWEATER_CALLER_BOOST
            }

#       if BOOST_SWEATER_EXACT_WORKER_SELECTION
            iteration = dispatch_workers( 0, iteration, number_of_dispatched_work_parts, iterations_per_part, parts_with_extra_iteration, iterations, completion_barrier, work_part_template ).second;
            BOOST_ASSUME( iteration <= iterations );
            enqueue_succeeded = true;
#       else // BOOST_SWEATER_EXACT_WORKER_SELECTION
            events::worker_bulk_enqueue_begin( number_of_dispatched_work_parts );
            /// \note MSVC does not support VLAs but has an alloca that returns (16
            /// byte) aligned memory. Clang's alloca is unaligned and it does not
            /// support VLAs of non-POD types. GCC has a (16 byte) aligned alloca
            /// and supports VLAs of non-POD types
            /// (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=19131).
            /// Regardless of any of this a work_t VLA is not used to avoid needless
            /// default construction of its members.
            /// The code below is safe with noexcept enqueue and noexcept
            /// destructible and constructible work_ts.
            ///                                   (21.01.2017.) (Domagoj Saric)
#       ifdef BOOST_MSVC
            auto const dispatched_work_parts{ static_cast<work_t *>( alloca( number_of_dispatched_work_parts * sizeof( work_t ) ) ) };
#       else
            alignas( work_t ) char dispatched_work_parts_storage[ number_of_dispatched_work_parts * sizeof( work_t ) ];
            auto * const BOOST_MAY_ALIAS dispatched_work_parts{ reinterpret_cast<work_t *>( dispatched_work_parts_storage ) };
#       endif // BOOST_MSVC

            for ( hardware_concurrency_t work_part{ 0 }; work_part < number_of_dispatched_work_parts; ++work_part )
            {
                auto const start_iteration{ iteration };
                auto const extra_iteration{ work_part < parts_with_extra_iteration };
                auto const end_iteration  { static_cast<iterations_t>( start_iteration + iterations_per_part + extra_iteration ) };
                auto const placeholder{ &dispatched_work_parts[ work_part ] };
                auto & work_chunk { *new ( placeholder ) work_t{ work_part_template } };
                auto & chunk_setup{ work_chunk.target_as<spread_work_base>() };
                chunk_setup.start_iteration = start_iteration;
                chunk_setup.  end_iteration =   end_iteration;
                BOOST_ASSERT( chunk_setup.start_iteration < chunk_setup.end_iteration );
                iteration = end_iteration;
            }
            BOOST_ASSUME( iteration == iterations );

            work_items_.fetch_add( number_of_dispatched_work_parts, std::memory_order_acquire );
            enqueue_succeeded = queue_.enqueue_bulk
            (
                std::make_move_iterator( dispatched_work_parts ),
                number_of_dispatched_work_parts
            );
            events::worker_bulk_enqueue_end();
            if ( BOOST_LIKELY( enqueue_succeeded ) )
            {
                completion_barrier.initialize( number_of_dispatched_work_parts );
                events::worker_bulk_signal_begin( number_of_dispatched_work_parts );
                work_semaphore_.signal( number_of_dispatched_work_parts );
                events::worker_bulk_signal_end();
            }
            else
            {
                caller_thread_end_iteration = iterations;
            }

            for ( hardware_concurrency_t work_part{ 0 }; work_part < number_of_dispatched_work_parts; ++work_part )
            {
                dispatched_work_parts[ work_part ].~work_t();
            }
#       endif // BOOST_SWEATER_EXACT_WORKER_SELECTION
            if ( caller_thread_end_iteration ) // use_caller_thread or enqueue failed
            {
                perform_caller_work( caller_thread_end_iteration, work_part_template, completion_barrier );
            }

            dispatched_parts = number_of_dispatched_work_parts;
        } // !HMP

        events::caller_join_begin( use_caller_thread );
        if ( use_caller_thread )
        {
            completion_barrier.spin_wait();
        }
        else
        {
            completion_barrier.wait();
        }
        events::caller_join_end();

        events::spread_end( dispatched_parts, use_caller_thread );
        return enqueue_succeeded;
    }


    template <typename Functor, typename ... Args>
    bool create_fire_and_destroy( Args && ... args ) noexcept
    (
        std::is_nothrow_constructible_v<Functor, Args && ...> &&
        !work_t::requires_allocation<Functor>
    )
    {
        static_assert( noexcept( std::declval<Functor &>()() ), "Fire and forget work has to be noexcept" );

        bool enqueue_succeeded;
        if constexpr( work_t::requires_allocation<Functor> )
        {
            struct self_destructed_work
            {
                self_destructed_work( Args && ... args ) : p_functor( new Functor{ std::forward<Args>( args )... } ) {}
                self_destructed_work( self_destructed_work && other ) noexcept : p_functor( other.p_functor ) { other.p_functor = nullptr; BOOST_ASSERT( p_functor ); }
                self_destructed_work( self_destructed_work const & ) = delete;
                void operator()() noexcept( noexcept( std::declval<Functor &>()() ) )
                {
                    BOOST_ASSERT( p_functor );
                    struct destructor
                    {
                        Functor * const p_work;
                        ~destructor() noexcept { delete p_work; }
                    } const eh_safe_destructor{ p_functor };
                    (*p_functor)();
                #ifndef NDEBUG
                    p_functor = nullptr;
                #endif // NDEBUG
                } // void operator()
                Functor * __restrict p_functor = nullptr;
            }; // struct self_destructed_work
            static_assert( std::is_trivially_destructible_v<self_destructed_work> );
#       if BOOST_SWEATER_EXACT_WORKER_SELECTION
            enqueue_succeeded = this->pool_.front().enqueue( self_destructed_work{ std::forward<Args>( args )... }, this->queue_ );
#       else
            enqueue_succeeded = this->queue_.enqueue( self_destructed_work{ std::forward<Args>( args )... } );
            this->wake_one_worker();
#       endif
        }
        else
        {
            struct self_destructed_work
            {
                self_destructed_work( Args && ... args ) { new ( storage ) Functor{ std::forward<Args>( args )... }; }
                self_destructed_work( self_destructed_work && other ) noexcept( std::is_nothrow_move_constructible_v<Functor> )
                {
                    auto & source( reinterpret_cast<Functor &>( other.storage ) );
                    new ( storage ) Functor( std::move( source ) );
                    source.~Functor();
                }
                self_destructed_work( self_destructed_work const & ) = delete;
                void operator()() noexcept( noexcept( std::declval<Functor &>()() ) )
                {
                    auto & work( reinterpret_cast<Functor &>( storage ) );
                    struct destructor
                    {
                        Functor & work;
                        ~destructor() noexcept { work.~Functor(); }
                    } eh_safe_destructor{ work };
                    work();
                } // void operator()
                alignas( alignof( Functor ) ) char storage[ sizeof( Functor ) ];
            }; // struct self_destructed_work
#       if BOOST_SWEATER_EXACT_WORKER_SELECTION
            enqueue_succeeded = this->pool_.front().enqueue( self_destructed_work{ std::forward<Args>( args )... }, this->queue_ );
#       else
            enqueue_succeeded = this->queue_.enqueue( self_destructed_work{ std::forward<Args>( args )... } );
            this->wake_one_worker();
#       endif
        }
        this->work_items_.fetch_add( enqueue_succeeded, std::memory_order_acquire );
        return BOOST_LIKELY( enqueue_succeeded );
    }


    void wake_one_worker() noexcept
    {
#   if BOOST_SWEATER_EXACT_WORKER_SELECTION
        //...mrmlj...TODO find an idle worker...
        pool_.front().notify();
#   else
        work_semaphore_.signal();
#   endif
    }


    void wake_all_workers() noexcept
    {
#   if BOOST_SWEATER_EXACT_WORKER_SELECTION
        for ( auto & worker : pool_ )
            worker.notify();
#   else
        work_semaphore_.signal( number_of_worker_threads() );
#   endif
    }

    void work_added    () noexcept { work_items_.fetch_add( 1, std::memory_order_acquire ); }
    void work_completed() noexcept { work_items_.fetch_sub( 1, std::memory_order_release ); }

private:
    std::atomic<bool                  > brexit_     = false;
    std::atomic<hardware_concurrency_t> work_items_ = 0;
#if BOOST_SWEATER_EXACT_WORKER_SELECTION
    struct alignas( 64 ) worker_thread : thread
    {
        using thread::operator=;

        void notify() noexcept { event_.signal(); }

        bool enqueue( work_t && work, my_queue & queue ) noexcept
        {
            bool success;
            {
                std::scoped_lock<spin_lock> const token_lock{ token_mutex_ };
                success = queue.enqueue( std::move( work ), *token_ );
            }
            notify();
            return success;
        }

        semaphore                                 event_;
        spin_lock                                 token_mutex_; // producer tokens are not thread safe (support concurrent spread_the_sweat calls)
        std::optional<my_queue::producer_token_t> token_;
#   ifdef __linux__
        pid_t thread_id_ = 0;
#   endif // Linux
    }; // struct worker_thread

#else // BOOST_SWEATER_EXACT_WORKER_SELECTION
    using worker_thread = thread;

    semaphore work_semaphore_;
#endif // BOOST_SWEATER_EXACT_WORKER_SELECTION

    /// \todo Further queue refinements.
    /// http://ithare.com/implementing-queues-for-event-driven-programs
    /// https://en.wikipedia.org/wiki/Work_stealing
    /// http://www.drdobbs.com/parallel/writing-lock-free-code-a-corrected-queue/210604448
    /// https://github.com/cameron314/readerwriterqueue
    /// http://moodycamel.com/blog/2013/a-fast-lock-free-queue-for-c++
    /// http://stackoverflow.com/questions/1164023/is-there-a-production-ready-lock-free-queue-or-hash-implementation-in-c#14936831
    /// https://github.com/facebook/folly/blob/master/folly/docs/ProducerConsumerQueue.md
    /// https://github.com/facebook/folly/blob/master/folly/MPMCQueue.h
    /// http://landenlabs.com/code/ring/ring.html
    /// https://github.com/Qarterd/Honeycomb/blob/master/src/common/Honey/Thread/Pool.cpp
    ///                                       (12.10.2016.) (Domagoj Saric)
    my_queue queue_;

#if BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
    using pool_threads_t = container::static_vector<worker_thread, BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY - BOOST_SWEATER_USE_CALLER_THREAD>;
#else
    using pool_threads_t = iterator_range<worker_thread *>;
#endif
    pool_threads_t pool_;
}; // class shop

#ifdef __clang__
#pragma clang diagnostic pop
#endif // clang

//------------------------------------------------------------------------------
} // namespace generic
//------------------------------------------------------------------------------
} // namespace sweater
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
#endif // generic_hpp
