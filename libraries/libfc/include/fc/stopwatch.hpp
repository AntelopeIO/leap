#pragma once

#include <fc/log/logger.hpp>
#include <atomic>
#include <chrono>
#include <string_view>
#include <iostream>


namespace fc {

template <typename Clock = std::chrono::high_resolution_clock>
class stopwatch {
   typename Clock::time_point start_point;
   size_t total_us = 0;
   size_t calls = 0;
public:
   stopwatch()
         : start_point(Clock::now()) {}

   void start() {
      start_point = Clock::now();
   }

   void stop() {
      total_us += std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start_point).count();
      ++calls;
   }

   template <typename Rep = typename Clock::duration::rep, typename Units = typename Clock::duration>
   Rep elapsed_time() const {
      std::atomic_thread_fence(std::memory_order_relaxed);
      auto counted_time = std::chrono::duration_cast<Units>(Clock::now() - start_point).count();
      std::atomic_thread_fence(std::memory_order_relaxed);
      return static_cast<Rep>(counted_time);
   }

   size_t elapsed_time_us() const {
      return elapsed_time<size_t, std::chrono::microseconds>();
   }

   void report(std::string_view msg, size_t interval, bool reset_on_interval) {
      if( calls % interval == 0 ) {
         report_msg( msg );
         if (reset_on_interval)
            reset();
      }
   }

   void reset() {
      calls = 0;
      total_us = 0;
   }

private:
   void report_msg( std::string_view msg ) {
      ilog("${msg} calls: ${c}, total: ${t}us, avg: ${a}us",
           ("msg", msg)("c", calls)("t", total_us)("a", total_us/calls));
   }
};

using stop_watch = stopwatch<>;
using system_stop_watch = stopwatch<std::chrono::system_clock>;
using monotonic_stop_watch = stopwatch<std::chrono::steady_clock>;

} // namespace fc