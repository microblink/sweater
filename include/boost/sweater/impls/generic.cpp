////////////////////////////////////////////////////////////////////////////////
///
/// \file generic.cpp
/// -----------------
///
/// (c) Copyright Domagoj Saric 2016 - 2022.
///
///  Use, modification and distribution are subject to the
///  Boost Software License, Version 1.0. (See accompanying file
///  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
///
///  See http://www.boost.org for most recent version.
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#include "generic.hpp"
//------------------------------------------------------------------------------
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
namespace generic
{
//------------------------------------------------------------------------------

#ifdef __clang__
// Clang does not support [[ (un)likely ]] cpp attributes
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-attributes"
#endif // clang

namespace events
{
#if defined( __GNUC__ )
#   define WEAK __attribute__(( weak ))
#else
#   define WEAK
#endif

    WEAK void caller_stalled           ( std::uint8_t /*current_work_stealing_division*/                                                   ) noexcept {}
    WEAK void caller_join_begin        ( bool /*spinning*/                                                                                 ) noexcept {}
    WEAK void caller_join_end          (                                                                                                   ) noexcept {}
    WEAK void caller_work_begin        (                                          std::uint32_t /*iterations*/                             ) noexcept {}
    WEAK void caller_work_end          (                                                                                                   ) noexcept {}
    WEAK void caller_stolen_work_begin (                                                                                                   ) noexcept {}
    WEAK void caller_stolen_work_end   ( std::uint32_t          /*stolen_items*/                                                           ) noexcept {}
    WEAK void worker_enqueue_begin     ( hardware_concurrency_t /*worker_index*/, std::uint32_t /*begin_iter*/, std::uint32_t /*end_iter*/ ) noexcept {}
    WEAK void worker_enqueue_end       ( hardware_concurrency_t /*worker_index*/                                                           ) noexcept {}
    WEAK void worker_work_begin        ( hardware_concurrency_t /*worker_index*/                                                           ) noexcept {}
    WEAK void worker_work_end          ( hardware_concurrency_t /*worker_index*/                                                           ) noexcept {}
    WEAK void worker_sleep_begin       ( hardware_concurrency_t /*worker_index*/                                                           ) noexcept {}
    WEAK void worker_sleep_end         ( hardware_concurrency_t /*worker_index*/                                                           ) noexcept {}
    WEAK void worker_bulk_enqueue_begin( hardware_concurrency_t /*number_of_workers*/                                                      ) noexcept {}
    WEAK void worker_bulk_enqueue_end  (                                                                                                   ) noexcept {}
    WEAK void worker_bulk_signal_begin ( hardware_concurrency_t /*number_of_workers*/                                                      ) noexcept {}
    WEAK void worker_bulk_signal_end   (                                                                                                   ) noexcept {}

    WEAK void spread_begin             ( std::uint32_t /*iterations*/                                                                      ) noexcept {}
    WEAK void spread_preexisting_work  (                                          hardware_concurrency_t /*items_in_shop*/                 ) noexcept {}
    WEAK void spread_recursive_call    ( hardware_concurrency_t /*worker_index*/, hardware_concurrency_t /*items_in_shop*/                 ) noexcept {}
    WEAK void spread_end               ( hardware_concurrency_t /*dispatched_parts*/, bool /*caller_used*/                                 ) noexcept {}

#undef WEAK
} // namespace events

#if BOOST_SWEATER_HMP
bool shop::hmp = true;
shop::hmp_config shop::hmp_clusters;
#endif

#if BOOST_SWEATER_SPIN_BEFORE_SUSPENSION
std::uint32_t shop::worker_spin_count{ 100 * 1000 };
#if BOOST_SWEATER_USE_CALLER_THREAD
std::uint32_t shop::caller_spin_count{ 100 * 1000 };
#endif // BOOST_SWEATER_USE_CALLER_THREAD
#endif // BOOST_SWEATER_SPIN_BEFORE_SUSPENSION

#if BOOST_SWEATER_USE_PARALLELIZATION_COST
std::uint8_t shop::min_parallel_iter_boost = min_parallel_iter_boost_weight;
#endif // BOOST_SWEATER_USE_PARALLELIZATION_COST

auto shop::worker_loop( [[ maybe_unused ]] hardware_concurrency_t const worker_index ) noexcept
{
    /// \note BOOST_SWEATER_EXACT_WORKER_SELECTION requires the worker
    /// index to be captured by the worker lambda - this makes it larger
    /// than a void pointer and thus requries the 'synchronized_invocation'
    /// path in the create thread call. This in turn causes deadlocks when
    /// shops are created as global variables under
    /// - Windows in DLLs (as the DLL entry procedure then blocks when it
    ///   must not do so). As a workaround the worker index and the shop
    ///   instance pointer are packed together (expecting to never be
    ///   further apart than 2GB, avoiding the 64bit address canonical
    ///   form pointer tagging complexities)
    /// - Emscripten because threads are not created until control is
    ///   yielded back to JS code (and sweat_shop constructor dead locks
    ///   waiting for the workers to start - as control is never returned
    ///   JS).
    /// https://lwn.net/Articles/718888
    /// https://source.android.com/devices/tech/debug/tagged-pointers
    ///                                   (13.08.2020.) (Domagoj Saric)
#if BOOST_SWEATER_EXACT_WORKER_SELECTION && ( defined( _WIN64 ) || defined( __LP64__ ) )
    static_assert( sizeof( void * ) == 8 );
    static std::byte /*const - MSVC '& on constant'!?*/ dummy_reference_object{};
    auto const shop_offset{ reinterpret_cast<std::byte const *>( this ) - &dummy_reference_object };
    struct shop_and_worker_t
    {
        std:: int64_t shop_offset  : 48;
        std::uint64_t worker_index : 16;
    } const shop_and_worker{ .shop_offset = shop_offset, .worker_index = worker_index };
    static_assert( sizeof( void * ) == sizeof( shop_and_worker ) );
    BOOST_ASSERT( shop_and_worker.shop_offset  == shop_offset  );
    BOOST_ASSERT( shop_and_worker.worker_index == worker_index );
#elif BOOST_SWEATER_EXACT_WORKER_SELECTION
    static_assert( sizeof( void * ) == 4 );
#   ifdef _WIN32
    using global_offset_t = std::uint32_t;
    static std::byte const & dummy_reference_object{ reinterpret_cast<std::byte const &>( __ImageBase ) };
#   else
    using global_offset_t = std::int32_t;
    static std::byte const dummy_reference_object{};
#   endif
    auto const shop_offset{ static_cast<global_offset_t>( reinterpret_cast<std::byte const *>( this ) - &dummy_reference_object ) };
    struct shop_and_worker_t
    {
        global_offset_t shop_offset  : 27;
        std::uint32_t   worker_index :  5;
    } const shop_and_worker{ .shop_offset = shop_offset, .worker_index = worker_index };
    static_assert( sizeof( void * ) == sizeof( shop_and_worker ) );
    BOOST_ASSERT( shop_and_worker.shop_offset  == shop_offset  );
    BOOST_ASSERT( shop_and_worker.worker_index == worker_index );
#else
    auto const p_shop{ this };
#endif // EWS
    auto worker_loop_impl
    {
        [=]() noexcept
        {
#       if BOOST_SWEATER_EXACT_WORKER_SELECTION
            auto & parent{ const_cast< shop & >( *reinterpret_cast<shop const *>( &dummy_reference_object + shop_and_worker.shop_offset ) ) };
            auto const worker_index{ static_cast<hardware_concurrency_t>( shop_and_worker.worker_index ) };
#       else
            auto & parent{ *p_shop };
            auto const worker_index{ static_cast<hardware_concurrency_t>( -1 ) };
#       endif // BOOST_SWEATER_EXACT_WORKER_SELECTION

#       if BOOST_SWEATER_EXACT_WORKER_SELECTION
#           ifdef __linux__
            parent.pool_[ worker_index ].thread_id_ = ::gettid();
#           endif // linux
            auto       & __restrict producer_token{                                   parent.pool_[ worker_index ].token_                          };
#           ifdef __ANDROID__
            auto       & __restrict work_event    { !thrd_lite::slow_thread_signals ? parent.pool_[ worker_index ].event_ : parent.work_semaphore_ };
#           else
            auto       & __restrict work_event    {                                   parent.pool_[ worker_index ].event_                          };
#           endif // Android
#       else // BOOST_SWEATER_EXACT_WORKER_SELECTION
            auto       & __restrict work_event    { parent.work_semaphore_ };
#       endif // BOOST_SWEATER_EXACT_WORKER_SELECTION
            auto       & __restrict queue         { parent.queue_  };
            auto const & __restrict exit          { parent.brexit_ };

            auto consumer_token{ queue.consumer_token() };

            work_t work;

            for ( ; ; )
            {
#           if BOOST_SWEATER_EXACT_WORKER_SELECTION
                while
                (
                    !thrd_lite::slow_thread_signals &&
                    BOOST_LIKELY( queue.dequeue_from_producer( work, *producer_token ) )
                ) [[ likely ]]
                {
                    events::worker_work_begin( worker_index );
                    work();
                    parent.work_completed();
                    events::worker_work_end  ( worker_index );
                }
#           endif // EWS
                // Work stealing for EWS
                while ( queue.dequeue( work, consumer_token ) ) [[ likely ]]
                {
                    events::worker_work_begin( worker_index );
                    work();
                    parent.work_completed();
                    events::worker_work_end  ( worker_index );
                }

                if ( BOOST_UNLIKELY( exit.load( std::memory_order_relaxed ) ) )
                    return;
                events::worker_sleep_begin( worker_index );
#           if BOOST_SWEATER_SPIN_BEFORE_SUSPENSION
                work_event.wait( worker_spin_count );
#           else
                work_event.wait();
#           endif // BOOST_SWEATER_SPIN_BEFORE_SUSPENSION
                events::worker_sleep_end  ( worker_index );
            }
        }
    }; // worker_loop_impl
#if defined( _WIN32 ) || defined( __EMSCRIPTEN__ )
    static_assert
    (
        thrd_lite::detail::fits_into_a_pointer< decltype( worker_loop_impl ) >,
        "This worker_loop_impl will cause a dead lock in the constructor of global shops inside DLLs under Windows or under Emscripten"
    );
#endif // Windows || Emscripten
    return worker_loop_impl;
}

auto shop::number_of_worker_threads() const noexcept
{
    auto const worker_threads{ static_cast<hardware_concurrency_t>( pool_.size() ) };
#if BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
    BOOST_ASSUME( worker_threads <= BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY );
#endif
    return worker_threads;
}

shop::shop()
    :
    consumer_token_{ queue_.consumer_token() }
{
#ifdef __GNUC__ // compilers with init_priority attribute (see hardware_concurency.hpp)
    hardware_concurrency_t local_hardware_concurrency( thrd_lite::hardware_concurrency_max );
#else
    /// \note Avoid the static-initialization-order-fiasco (for compilers
    /// not supporting the init_priority attribute) by not using the
    /// global hardware_concurrency_max variable (i.e. allow users to
    /// safely create plain global-variable sweat_shop singletons).
    ///                                   (01.05.2017.) (Domagoj Saric)
    hardware_concurrency_t local_hardware_concurrency( thrd_lite::get_hardware_concurrency_max() );
#endif // __GNUC__
#if BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
    /// \note Fail-safe for possible future devices that may overflow
    /// BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY or for cases like running an
    /// ARMv7 slice on modern ARMv8 hardware.
    ///                                   (14.12.2021.) (Domagoj Saric)
    local_hardware_concurrency = std::min<hardware_concurrency_t>( local_hardware_concurrency, BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY );
#endif
    BOOST_ASSUME( local_hardware_concurrency > 0 );
    create_pool( local_hardware_concurrency - BOOST_SWEATER_USE_CALLER_THREAD );
}

shop::~shop() noexcept { stop_and_destroy_pool(); }

hardware_concurrency_t shop::number_of_workers() const noexcept
{
    auto const actual_number_of_workers{ number_of_worker_threads() + ( BOOST_SWEATER_USE_CALLER_THREAD && !thrd_lite::slow_thread_signals ) };
#if BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
    BOOST_ASSUME( actual_number_of_workers <= BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY );
#endif
    return static_cast<hardware_concurrency_t>( actual_number_of_workers );
}

// https://web.archive.org/web/20161026022836/http://www.route32.net/2016/09/ticks-scheduling-and-confighz-overview.html
// https://unix.stackexchange.com/questions/466722/how-to-change-the-length-of-time-slices-used-by-the-linux-cpu-scheduler

BOOST_ATTRIBUTES( BOOST_MINSIZE )
bool shop::set_priority( thrd_lite::priority const new_priority ) noexcept
{
#ifdef __EMSCRIPTEN__
    return new_priority == thrd_lite::priority::normal;
#else // !Emscripten
    [[ maybe_unused ]] auto const nice_value( static_cast<int>( new_priority ) );
    bool success( true );
    for ( auto & thread : pool_ )
    {
        success &= thread.set_priority( new_priority );
    }

#if defined( __linux ) // also on Android
    if ( !success )
    {
#   if BOOST_SWEATER_HMP // Disable HMP to get all workers queried
        auto hmp_setting{ hmp };
        hmp = false;
#   endif // BOOST_SWEATER_HMP
#   if BOOST_SWEATER_USE_CALLER_THREAD
        auto const caller_id{ ::gettid() };
#   endif // BOOST_SWEATER_USE_CALLER_THREAD
        success = true;
        spread_the_sweat
        (
            thrd_lite::hardware_concurrency_max,
            [ =, &success ]( iterations_t, iterations_t /*worker index?*/ ) noexcept
            {
#           if BOOST_SWEATER_USE_CALLER_THREAD
                /// \note Do not change the caller thread's priority.
                ///                       (05.05.2017.) (Domagoj Saric)
                if ( ::gettid() != caller_id )
#           endif // BOOST_SWEATER_USE_CALLER_THREAD
                {
                    auto const result( ::setpriority( PRIO_PROCESS, 0, nice_value ) );
                    BOOST_ASSERT( ( result == 0 ) || ( errno == EACCES ) );
                    success &= ( result == 0 );
                }
            }
        );
#   if BOOST_SWEATER_HMP
        hmp = hmp_setting;
#   endif // BOOST_SWEATER_HMP
    }
#endif // __linux
    return success;
#endif // !Emscripten
}

BOOST_ATTRIBUTES( BOOST_COLD )
bool shop::bind_worker
(
    hardware_concurrency_t const worker_index,
    cpu_affinity_mask      const mask
) noexcept
{
    auto & thread{ pool_[ worker_index ] };
    auto succeeded{ thread.bind_to_cpu( mask ) };
    if ( !succeeded )
    {
#   if defined( __linux__ )
#       if BOOST_SWEATER_EXACT_WORKER_SELECTION
        succeeded |= thrd_lite::thread::bind_to_cpu( thread.thread_id_, mask );
#       else
#       if BOOST_SWEATER_HMP // Disable HMP to get all workers queried
        auto hmp_setting{ hmp };
        hmp = false;
#       endif // BOOST_SWEATER_HMP
        int result{ 2 };
        spread_the_sweat
        (
            number_of_worker_threads(),
            [ &, target_handle = thread.get_id() ]( iterations_t, iterations_t ) noexcept
            {
                auto const current{ pthread_self() };
                if ( current == target_handle )
                {
                    BOOST_ASSERT( result == 2 );
                    result = thread.bind_to_cpu( ::gettid(), mask );
                    BOOST_ASSERT( result != 2 );
                }
            }
        );
#       if BOOST_SWEATER_HMP
        hmp = hmp_setting;
#       endif // BOOST_SWEATER_HMP
        BOOST_ASSERT( result != 2 );
        succeeded |= ( result == 0 );
#       endif // EWS or spread version
#   endif // Linux
    }
    return succeeded;
}

bool shop::bind_worker_to_cpu( hardware_concurrency_t const worker_index, unsigned const cpu_id ) noexcept
{
#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
    (void)worker_index; (void)cpu_id;
    return false;
#else
    cpu_affinity_mask mask;
    mask.add_cpu( cpu_id );
    return bind_worker( worker_index, mask );
#endif
}

BOOST_ATTRIBUTES( BOOST_COLD )
void shop::set_max_allowed_threads( hardware_concurrency_t const max_threads )
{
    BOOST_ASSERT_MSG( queue_.empty(), "Cannot change parallelism level while items are in queue."    );
    BOOST_ASSERT_MSG( !hmp          , "Cannot change number of workers directly when HMP is enabled" );
    stop_and_destroy_pool();
    create_pool( max_threads - BOOST_SWEATER_USE_CALLER_THREAD );
}

hardware_concurrency_t shop::number_of_items() const noexcept
{
#if 0
    return queue_.depth();
#else
    return work_items_.load( std::memory_order_acquire );
#endif
}

#if BOOST_SWEATER_HMP
BOOST_ATTRIBUTES( BOOST_COLD )
void shop::configure_hmp( hmp_clusters_info const config, std::uint8_t const number_of_clusters )
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

    hmp = !thrd_lite::slow_thread_signals;

    create_pool( number_of_cores - BOOST_SWEATER_USE_CALLER_THREAD );
}
#endif // BOOST_SWEATER_HMP

BOOST_ATTRIBUTES( BOOST_COLD )
void shop::create_pool( hardware_concurrency_t const size )
{
    BOOST_ASSERT_MSG( size <= thrd_lite::get_hardware_concurrency_max(), "Requested parallelism level not offered in hardware." );
    auto const current_size( pool_.size() );
    if ( size == current_size )
        return;
    stop_and_destroy_pool();
    brexit_.store( false, std::memory_order_relaxed );
#if BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
    pool_.resize( size );
#else
    auto p_workers( std::make_unique<worker_thread[]>( size ) );
    pool_ = { p_workers.get(), size };
#endif // !BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY

    for ( hardware_concurrency_t worker_index{ 0 }; worker_index < size; ++worker_index )
    {
#   if BOOST_SWEATER_EXACT_WORKER_SELECTION
        if ( !thrd_lite::slow_thread_signals )
            pool_[ worker_index ].token_.emplace( queue_.producer_token() );
#   endif // BOOST_SWEATER_EXACT_WORKER_SELECTION
        pool_[ worker_index ] = worker_loop( worker_index );
    }
#if !BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
    p_workers.release();
#endif // !BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
    std::atomic_thread_fence( std::memory_order_seq_cst );
}


BOOST_ATTRIBUTES( BOOST_COLD )
void shop::stop_and_destroy_pool() noexcept
{
    brexit_.store( true, std::memory_order_relaxed );
    wake_all_workers();
    for ( auto & worker : pool_ )
        worker.join();
#if BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
    pool_.clear();
#else
    delete[] pool_.data();
    pool_ = {};
#endif // BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
}

BOOST_NOINLINE
void shop::perform_caller_work
(
    iterations_t                   const iterations,
    spread_work_template_t const &       work_part_template,
    thrd_lite::barrier           &       completion_barrier
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

// Inter-spread work stealing (as another dynamic way of handling heterogenous
// CPU configurations, unbalanced loads by other/background processes,
// inconsistent/slow/unpredictable behaviour of OS thread signaling and
// managment primitevs) is implemented by splitting/dividing per-core work
// chunks/items or, IOW, producing more smaller/shorter work items.
// This incurrs an overhead (produces more queue traffic) which is negligible
// compared to practical runtime speedups and it (the queue based approach) is
// required for a dispatcher which supports concurrent and recursive dispatches.
std::uint8_t const spread_work_stealing_division_min{  4 };
std::uint8_t const spread_work_stealing_division_max{ 16 }; // has to be limited (among other reasons) not to overflow hardware_concurrency_t
std::uint8_t shop::spread_work_stealing_division    { spread_work_stealing_division_min };

#if BOOST_SWEATER_EXACT_WORKER_SELECTION
auto shop::dispatch_workers
(
    hardware_concurrency_t               worker_index,
    iterations_t                         iteration,
    hardware_concurrency_t         const max_parts,
    iterations_t                   const iterations_per_part,
    iterations_t                   const parts_with_extra_iteration,
    iterations_t                   const iterations, // total/max for the whole spread (not necessarily all for this call)
    thrd_lite::barrier           &       completion_barrier,
    spread_work_template_t const &       work_part_template
) noexcept
{
    BOOST_ASSUME( spread_work_stealing_division >= spread_work_stealing_division_min );
    BOOST_ASSUME( spread_work_stealing_division <= spread_work_stealing_division_max );
    auto const slice_div
    {
        static_cast<std::uint8_t>
        (
            std::min<iterations_t>( spread_work_stealing_division, std::max<iterations_t>( iterations_per_part, 1 ) ) // handle zero iterations_per_part
        )
    };
#ifdef BOOST_MSVC
    auto const slices{ static_cast<work_t *>( alloca( slice_div * sizeof( work_t ) ) ) };
#else
    alignas( work_t ) char slices_storage[ slice_div * sizeof( work_t ) ];
    auto const slices{ reinterpret_cast<work_t *>( slices_storage ) };
#endif // BOOST_MSVC

    for ( auto work_part{ 0U }; work_part < max_parts && iteration != iterations; ++work_part )
    {
        auto const start_iteration{ iteration };
        auto const extra_iteration{ work_part < parts_with_extra_iteration };
        auto const   end_iteration{ static_cast<iterations_t>( start_iteration + iterations_per_part + extra_iteration ) };
        events::worker_enqueue_begin( worker_index, start_iteration, end_iteration );
        auto const slice_iter            { ( iterations_per_part + extra_iteration ) / slice_div };
        auto       first_slice_extra_iter{ ( iterations_per_part + extra_iteration ) % slice_div };
        hardware_concurrency_t number_of_slices{ 0 };
        for ( auto iter{ start_iteration }; iter < end_iteration; )
        {
            BOOST_ASSUME( number_of_slices < slice_div );
            auto & work_chunk { *new ( &slices[ number_of_slices++ ] ) work_t{ work_part_template } };
            auto & chunk_setup{ work_chunk.target_as<spread_work_base>() };
            chunk_setup.start_iteration = iter;
            chunk_setup.  end_iteration = iter + first_slice_extra_iter + slice_iter;
            BOOST_ASSERT( chunk_setup.p_completion_barrier == &completion_barrier );
            BOOST_ASSERT( chunk_setup.start_iteration < chunk_setup.end_iteration );
            first_slice_extra_iter = 0;
            iter                   = chunk_setup.end_iteration;
            // Have to do incremental/'dynamic' 'number of work parts' counting
            // as the rounding and load balancing logic can produce a smaller
            // number than the simple number_of_work_parts calculation in the
            // non-HMP case.
            completion_barrier.add_expected_arrival();
            work_added();
        }
        BOOST_ASSERT( number_of_slices );
        BOOST_VERIFY( pool_[ worker_index ].enqueue( std::make_move_iterator( slices ), number_of_slices, queue_ ) ); //...mrmlj...todo err handling
        iteration = end_iteration;
        events::worker_enqueue_end( worker_index );
        ++worker_index;
        for ( auto slice{ 0 }; slice < number_of_slices; ++slice )
        {
            slices[ slice ].~work_t();
        }
    }

    return std::make_pair( worker_index, iteration );
}
#endif // BOOST_SWEATER_EXACT_WORKER_SELECTION

BOOST_NOINLINE
bool BOOST_CC_REG shop::spread_work
(
    spread_work_template_t       work_part_template,
    iterations_t           const iterations,
    iterations_t                 parallelizable_iterations_count
) noexcept
{
    if ( BOOST_UNLIKELY( iterations == 0 ) ) [[ unlikely ]]
        return true;

    events::spread_begin( iterations );

    thrd_lite::barrier completion_barrier;
    work_part_template.target_as<spread_work_base>().p_completion_barrier = &completion_barrier;

    auto const items_in_shop{ number_of_items() };
    if ( BOOST_UNLIKELY( items_in_shop ) ) [[ unlikely ]]
    {
        events::spread_preexisting_work( items_in_shop );

        // Support single-core Docker images (for other platforms assume no
        // single core systems actually exist nowadays) - in case of
        // concurrent spreads, err on the side of simplicity and perform
        // the work immediately on the caller (overcommiting the single
        // core).
#   if BOOST_SWEATER_USE_CALLER_THREAD && defined( __linux__ ) && !defined( __ANDROID__ )
        if ( number_of_worker_threads() == 0 ) [[ unlikely ]]
        {
            perform_caller_work( iterations, work_part_template, completion_barrier );
            return true;
        }
#   endif // single CPU Docker

        // Support recursive spread_the_sweat calls: for now just perform
        // everything in the caller.
        auto const this_thread{ thrd_lite::thread::get_active_thread_id() };
        for ( auto const & worker : pool_ )
        {
            if ( BOOST_UNLIKELY( worker.get_id() == this_thread ) ) [[ unlikely ]]
            {
                events::spread_recursive_call( static_cast<hardware_concurrency_t>( &worker - &pool_.front() ), items_in_shop );
                perform_caller_work( iterations, work_part_template, completion_barrier );
                return true;
            }
        }
    }

#if !BOOST_SWEATER_USE_PARALLELIZATION_COST
    parallelizable_iterations_count = 1;
#endif // BOOST_SWEATER_USE_PARALLELIZATION_COST

    auto const actual_number_of_workers{ number_of_workers() };
    auto const free_workers            { std::max<int>( 0, actual_number_of_workers - items_in_shop ) };
    auto const max_work_parts          { free_workers ? free_workers : number_of_worker_threads() }; // prefer using any available worker - otherwise queue and wait
    auto const queue_and_wait          { !free_workers };
    auto const use_caller_thread
    {
        BOOST_SWEATER_USE_CALLER_THREAD &&
        !queue_and_wait                 &&
        // On slow_thread_signals devices use the caller thread for signaling the workers and then stealing work if time remains.
        ( !thrd_lite::slow_thread_signals || ( thrd_lite::slow_thread_signals && ( iterations <= parallelizable_iterations_count ) ) )
    };

#if BOOST_SWEATER_USE_CALLER_THREAD
    completion_barrier.use_spin_wait( !queue_and_wait );
#endif // BOOST_SWEATER_USE_CALLER_THREAD

    hardware_concurrency_t dispatched_parts;
    bool enqueue_succeeded;
#if BOOST_SWEATER_HMP
    static_assert( BOOST_SWEATER_EXACT_WORKER_SELECTION );
    // In case of recursive and concurrent (but incomplete) spreads we skip
    // HMP logic (because the logic itself would need tweaking and
    // additional tracking of which cluster cores/workers are actually
    // free).
    if ( hmp && !items_in_shop )
    {
        BOOST_ASSERT_MSG( hmp_clusters.number_of_clusters, "HMP not configured" );
        BOOST_ASSUME( hmp_clusters.number_of_clusters <= hmp_clusters.max_clusters );
        BOOST_ASSUME( !thrd_lite::slow_thread_signals );

        // Leftovers of the original (pre-work-stealing/pre-July 2021) HMP
        // logic which 'cooperate good enough' with the work-stealing logic
        // performed in/by dispatch_workers(). TODO: revisit this for a new
        // approach tailored around work-stealing.

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
            auto const cluster_iterations        { hmp_distributions [ cluster ]      };
            auto       cluster_cores             { hmp_clusters.cores[ cluster ]      };
            auto       per_core_iterations       { cluster_iterations / cluster_cores };
            auto       parts_with_extra_iteration{ cluster_iterations % cluster_cores };

            if ( use_caller_thread && ( cluster == 0 ) )
            {
                BOOST_ASSUME( iteration == 0 );
                auto const extra_iteration{ parts_with_extra_iteration != 0 };
                caller_thread_end_iteration  = per_core_iterations + extra_iteration;
                iteration = caller_thread_end_iteration;
                --cluster_cores;
                parts_with_extra_iteration -= extra_iteration;
            }

            std::tie( worker, iteration ) = dispatch_workers( worker, iteration, cluster_cores, per_core_iterations, parts_with_extra_iteration, iterations, completion_barrier, work_part_template );
        }
        enqueue_succeeded = true; //...mrmlj...

        if ( BOOST_LIKELY( use_caller_thread ) ) [[ likely ]]
        {
            perform_caller_work( caller_thread_end_iteration, work_part_template, completion_barrier );
        }

        dispatched_parts = worker;
    }
    else // !HMP || items_in_shop
#endif // BOOST_SWEATER_HMP
    {
        auto parallelizable_parts           { std::max<iterations_t>( 1, iterations / parallelizable_iterations_count ) };
        auto number_of_work_parts           { static_cast<hardware_concurrency_t>( std::min<iterations_t>( parallelizable_parts, max_work_parts ) ) };
        auto number_of_dispatched_work_parts{ static_cast<hardware_concurrency_t>( std::max<int>( 0, number_of_work_parts - use_caller_thread ) ) };

        auto iterations_per_part       { iterations / number_of_work_parts };
        auto parts_with_extra_iteration{ iterations % number_of_work_parts };

#   if BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY
        BOOST_ASSUME( number_of_dispatched_work_parts <= ( BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY - BOOST_SWEATER_USE_CALLER_THREAD ) );
#   endif // BOOST_SWEATER_MAX_HARDWARE_CONCURRENCY

        iterations_t iteration{ 0 };
        iterations_t caller_thread_end_iteration{ 0 };
        if ( use_caller_thread )
        {
            auto const extra_iteration{ parts_with_extra_iteration != 0 };
            caller_thread_end_iteration  = iterations_per_part + extra_iteration;
            iteration                    = caller_thread_end_iteration;
            parts_with_extra_iteration  -= extra_iteration;
        }

#   if BOOST_SWEATER_EXACT_WORKER_SELECTION
        // When there are items_in_shop fallback to the shared queue (as
        // there is currently no tracking which queues/workers are taken).
        if ( !thrd_lite::slow_thread_signals && !items_in_shop )
        {
            iteration = dispatch_workers( 0, iteration, number_of_dispatched_work_parts, iterations_per_part, parts_with_extra_iteration, iterations, completion_barrier, work_part_template ).second;
            BOOST_ASSUME( iteration <= iterations );
            enqueue_succeeded = true; //...mrmlj...
        }
        else
#   endif
        if ( BOOST_LIKELY( number_of_dispatched_work_parts ) )
        { // Also serves as a slow_thread_signals fallback and items_in_shop 'handler'.
            if ( !items_in_shop )
            {
                // Slice up the parts for work stealing (unless there are other
                // active spreads - there's work to steal so don't create
                // unnecessary queue traffic).
                BOOST_ASSUME( spread_work_stealing_division <= spread_work_stealing_division_max );
                BOOST_ASSUME( iteration + iterations_per_part * number_of_dispatched_work_parts + parts_with_extra_iteration == iterations );
                auto const slice_div
                {
                    static_cast<std::uint8_t>
                    (
                        std::min<iterations_t>( spread_work_stealing_division, std::max<iterations_t>( iterations_per_part, 1 ) ) // handle zero iterations_per_part
                    )
                };
                parts_with_extra_iteration      += ( iterations_per_part % slice_div ) * number_of_dispatched_work_parts;
                iterations_per_part             /= slice_div;
                number_of_dispatched_work_parts *= slice_div;
                BOOST_ASSUME( iteration + iterations_per_part * number_of_dispatched_work_parts + parts_with_extra_iteration == iterations );
            }

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
            auto * const BOOST_MAY_ALIAS dispatched_work_parts{ reinterpret_cast<work_t *>( dispatched_work_parts_storage ) }; // GCC 11 ICEs w/o the asterisk
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
                BOOST_ASSUME( chunk_setup.start_iteration < chunk_setup.end_iteration );
                iteration = end_iteration;
            }
            BOOST_ASSUME( iteration == iterations );

            work_added( number_of_dispatched_work_parts );
            // Has to be initialized before enqueuing (to support spinning waits in workers -
            // where trivial work would get dequeued and executed (and 'arrived at') before
            // this thread could enter the if ( enqueue_succeeded ) block and initialize the
            // the barrier there).
            completion_barrier.initialize( number_of_dispatched_work_parts );
            enqueue_succeeded = queue_.enqueue_bulk
            (
                std::make_move_iterator( dispatched_work_parts ),
                number_of_dispatched_work_parts
            );
            events::worker_bulk_enqueue_end();
            if ( BOOST_LIKELY( enqueue_succeeded ) )
            {
                events::worker_bulk_signal_begin( number_of_dispatched_work_parts );
#           if !BOOST_SWEATER_EXACT_WORKER_SELECTION || defined( __ANDROID__ ) // Android also has the fallback work_semaphore_ for slow_thread_signals devices
                if ( thrd_lite::slow_thread_signals )
                    work_semaphore_.signal( number_of_dispatched_work_parts );
                else
#           endif
                    wake_all_workers();
                events::worker_bulk_signal_end();
            }
            else
            {
                completion_barrier.initialize( 0 );
                caller_thread_end_iteration = iterations;
            }

            for ( hardware_concurrency_t work_part{ 0 }; work_part < number_of_dispatched_work_parts; ++work_part )
            {
                dispatched_work_parts[ work_part ].~work_t();
            }
        }
        else // no dispatched parts
        {
#       if BOOST_SWEATER_USE_CALLER_THREAD
            BOOST_ASSUME( caller_thread_end_iteration == iterations );
            completion_barrier.initialize( 0 );
            enqueue_succeeded = true;
#       else
            BOOST_UNREACHABLE();
#       endif
        }

        if ( caller_thread_end_iteration ) // use_caller_thread or enqueue failed
        {
            perform_caller_work( caller_thread_end_iteration, work_part_template, completion_barrier );
        }

        dispatched_parts = number_of_dispatched_work_parts;
    } // !HMP

    // Caller work stealing
    if ( !queue_and_wait && !queue_.empty() )
    {
        events::caller_stolen_work_begin();
        work_t work;
        std::uint32_t stolen_items{ 0 };
        while ( true )
        {
            {
                // Support concurrent spreads (tokens aren't thread-safe).
                std::scoped_lock<thrd_lite::spin_lock> const token_lock{ consumer_token_mutex_ };
                if ( !queue_.dequeue( work, consumer_token_ ) )
                    break;
            }
            work();
            work_completed();
            ++stolen_items;
        }
        events::caller_stolen_work_end( stolen_items );
    }

#if BOOST_SWEATER_USE_CALLER_THREAD
    if ( !queue_and_wait )
    {
        if ( !completion_barrier.everyone_arrived() )
        {
            // Increase work-splitting if the worker has to wait/stall (having no work to steal).
            BOOST_ASSUME( dispatched_parts );
            events::caller_join_begin( use_caller_thread );
            auto const stalled
            {
                completion_barrier.spin_wait
                (
#               if BOOST_SWEATER_SPIN_BEFORE_SUSPENSION
                    caller_spin_count
#               endif // BOOST_SWEATER_SPIN_BEFORE_SUSPENSION
                )
            };
            events::caller_join_end();
            if ( stalled )
            {
                events::caller_stalled( spread_work_stealing_division );
                spread_work_stealing_division = std::min<std::uint8_t>( spread_work_stealing_division + 1, spread_work_stealing_division_max );
            }
        }
    }
    else
#endif // BOOST_SWEATER_USE_CALLER_THREAD
    {
        events::caller_join_begin( use_caller_thread );
        completion_barrier.wait();
        events::caller_join_end();
    }

    events::spread_end( dispatched_parts, use_caller_thread );
    return enqueue_succeeded;
}


void shop::wake_all_workers() noexcept
{
#if BOOST_SWEATER_EXACT_WORKER_SELECTION
    if ( !thrd_lite::slow_thread_signals )
    {
        for ( auto & worker : pool_ )
            worker.notify();
    }
    else
#endif
    {
#   if !BOOST_SWEATER_EXACT_WORKER_SELECTION || defined( __ANDROID__ )
        work_semaphore_.signal( number_of_worker_threads() );
#   else
        BOOST_UNREACHABLE();
#   endif
    }
}

//...mrmlj...allowing underflow/overflow because of late fetch_add in fire_and_forget and concurrent invocation 'races'
void shop::work_added    ( hardware_concurrency_t const items ) noexcept { /*thrd_lite::detail:: overflow_checked_add( work_items_, items );*/ work_items_.fetch_add( items, std::memory_order_acquire ); }
void shop::work_completed(                                    ) noexcept { /*thrd_lite::detail::underflow_checked_dec( work_items_        );*/ work_items_.fetch_sub( 1    , std::memory_order_release ); }

#if BOOST_SWEATER_EXACT_WORKER_SELECTION
void shop::worker_thread::notify() noexcept { event_.signal(); }

bool shop::worker_thread::enqueue( work_t && __restrict work, my_queue & __restrict queue ) noexcept
{
    BOOST_ASSUME( !thrd_lite::slow_thread_signals );
    bool success;
    {
        std::scoped_lock<thrd_lite::spin_lock> const token_lock{ token_mutex_ };
        success = queue.enqueue( std::move( work ), *token_ );
    }
    notify();
    return success;
}

bool shop::worker_thread::enqueue( std::move_iterator< work_t * > const p_work, hardware_concurrency_t const number_of_items, my_queue & __restrict queue ) noexcept
{
    BOOST_ASSUME( !thrd_lite::slow_thread_signals );
    BOOST_ASSERT( number_of_items                 );
    bool success;
    {
        std::scoped_lock<thrd_lite::spin_lock> const token_lock{ token_mutex_ };
        success = queue.enqueue_bulk( *token_, p_work, number_of_items );
    }
    notify();
    return success;
}
#endif // BOOST_SWEATER_EXACT_WORKER_SELECTION

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
