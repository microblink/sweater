////////////////////////////////////////////////////////////////////////////////
///
/// \file hardware_concurrency.cpp
/// ------------------------------
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
#include "hardware_concurrency.hpp"
//------------------------------------------------------------------------------
#ifdef __ANDROID__
#   include <android/api-level.h>
#   include <unistd.h>
#endif // __ANDROID__
#ifdef __EMSCRIPTEN_PTHREADS__
#   include <emscripten/threading.h>
#endif // __EMSCRIPTEN_PTHREADS__
#ifdef __linux__
#   include <sys/sysinfo.h>
#endif // __linux__

#if BOOST_SWEATER_DOCKER_LIMITS
#   include <boost/assert.hpp>

#   include <fcntl.h>
#   include <sys/types.h>
#   include <unistd.h>
#endif // BOOST_SWEATER_DOCKER_LIMITS

#ifdef _MSC_VER
#   include <yvals.h>
#   pragma detect_mismatch( "BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY", _STRINGIZE( BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY ) )
#endif // _MSC_VER

#include <cstdint>
#include <thread>
//------------------------------------------------------------------------------
#ifdef __ANDROID__
// https://android.googlesource.com/platform/bionic/+/HEAD/docs/status.md
__attribute__(( weak )) int get_nprocs     () noexcept { return static_cast< int >( ::sysconf( _SC_NPROCESSORS_ONLN ) ); }
__attribute__(( weak )) int get_nprocs_conf() noexcept { return static_cast< int >( ::sysconf( _SC_NPROCESSORS_CONF ) ); }
#endif // __ANDROID__
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace thrd_lite
{
//------------------------------------------------------------------------------

#if BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY == 1

hardware_concurrency_t hardware_concurrency_current() noexcept { return 1; }
hardware_concurrency_t get_hardware_concurrency_max() noexcept { return 1; }

#elif BOOST_SWEATER_DOCKER_LIMITS

namespace
{
    [[ maybe_unused ]]
    int read_int( char const * const file_path ) noexcept
    {
        auto const fd{ ::open( file_path, O_RDONLY, 0 ) };
        if ( fd == -1 )
            return -1;
        char value[ 64 ];
        BOOST_VERIFY( ::read( fd, value, sizeof( value ) ) < signed( sizeof( value ) ) );
        BOOST_VERIFY( ::close( fd ) == 0 );
        return std::atoi( value );
    }

    auto get_docker_limit() noexcept
    {
        // https://bugs.openjdk.java.net/browse/JDK-8146115
        // http://hg.openjdk.java.net/jdk/hs/rev/7f22774a5f42
        // RAM limit /sys/fs/cgroup/memory.limit_in_bytes
        // swap limt /sys/fs/cgroup/memory.memsw.limit_in_bytes

        // https://github.com/moby/moby/issues/20770#issuecomment-1559152307

        // LMG note 2024-09-13: It seems that newer linux distros always have `/sys/fs/cgroup/cpu.max`
        //                      Because of this, just always use the cgroup v2 format.
        //                      https://docs.kernel.org/admin-guide/cgroup-v2.html

        int cfs_quota { 0 };
        int cfs_period{ 0 };

        auto const fd{ ::open( "/sys/fs/cgroup/cpu.max", O_RDONLY, 0 ) };

        if ( fd != -1 )
        {
            // New cgroup v2 format
            char value_pair[ 128 ]; value_pair[ 0 ] = 0;
            BOOST_VERIFY( ::read( fd, value_pair, sizeof( value_pair ) ) < signed( sizeof( value_pair ) ) );
            BOOST_VERIFY( ::close( fd ) == 0 );

            char * cfs_period_ptr;
            cfs_quota  = static_cast< int >( std::strtol( value_pair    , &cfs_period_ptr, 10 ) );
            cfs_period = static_cast< int >( std::strtol( cfs_period_ptr, nullptr        , 10 ) );
        }
        else
        {
            // Fall back to old cgroup v1 format
            cfs_quota  = read_int( "/sys/fs/cgroup/cpu/cpu.cfs_quota_us"  );
            cfs_period = read_int( "/sys/fs/cgroup/cpu/cpu.cfs_period_us" );
        }

        if ( ( cfs_quota > 0 ) && ( cfs_period > 0 ) )
        {
            // Docker allows non-whole core quota assignments - use some sort of
            // heurestical rounding.
            return std::max( static_cast<int>( ( cfs_quota + cfs_period / 2 ) / cfs_period ), 1 );
        }
        return -1;
    }

    struct docker_quota_t // TODO extract a generalized 'prioritized value wrapper' template
    {
        docker_quota_t() noexcept : value{ get_docker_limit() } {}
        int const value;
        __attribute__(( pure )) operator int() const noexcept { return value; }
    } const docker_quota __attribute__(( init_priority( 101 ) ));
} // anonymous namespace

hardware_concurrency_t get_hardware_concurrency_max() noexcept
{
    // Obey docker limits even when someone attempts to create a pool with
    // more threads than allowed by the Docker container but return the number
    // of all CPUs when there is no Docker CPU quota in place.
    return static_cast<hardware_concurrency_t>( ( docker_quota != -1 ) ? docker_quota : get_nprocs_conf() );
}

hardware_concurrency_t hardware_concurrency_current() noexcept { return static_cast<hardware_concurrency_t>( ( docker_quota != -1 ) ? docker_quota : get_nprocs() ); }

#else // generic/standard impl

hardware_concurrency_t get_hardware_concurrency_max() noexcept
{
    return static_cast<hardware_concurrency_t>
    (
#   if defined( __EMSCRIPTEN_PTHREADS__ )
        emscripten_has_threading_support() ? emscripten_num_logical_cores() : 1
#   elif defined( __linux__ )
        // libcpp std::thread::hardware_concurrency() returns the dynamic number of active cores.
        get_nprocs_conf()
#   else
        std::thread::hardware_concurrency()
#   endif
    );
}

hardware_concurrency_t hardware_concurrency_current() noexcept
{
    return static_cast<hardware_concurrency_t>
    (
#   if defined( __EMSCRIPTEN_PTHREADS__ )
        get_hardware_concurrency_max()
#   elif defined( __linux__ )
        get_nprocs()
#   else
        std::thread::hardware_concurrency()
#   endif
    );
}

#endif // impl

// http://clang-developers.42468.n3.nabble.com/Clang-equivalent-to-attribute-init-priority-td4034229.html
// https://gcc.gnu.org/ml/gcc-help/2011-05/msg00221.html
// "can only use 'init_priority' attribute on file-scope definitions of objects of class type"
hardware_concurrency_max_t const hardware_concurrency_max
#ifdef __GNUC__
 __attribute__(( init_priority( 101 ) ))
#endif
;


#ifdef __ANDROID__
slow_thread_signals_t::slow_thread_signals_t() noexcept
    : value{ android_get_device_api_level() < 24 } {} // pre Android 7 Noughat

slow_thread_signals_t const slow_thread_signals __attribute__(( init_priority( 101 ) ));
#endif // ANDROID

//------------------------------------------------------------------------------
} // namespace thrd_lite
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------
