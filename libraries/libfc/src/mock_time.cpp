#include <fc/mock_time.hpp>
#include <thread>

namespace fc {

bool mock_time_traits::mock_enabled_ = false;
const boost::posix_time::ptime mock_time_traits::epoch_{ boost::gregorian::date( 1970, 1, 1 ) };
std::atomic<int64_t> mock_time_traits::now_{};

mock_time_traits::time_type mock_time_traits::now() noexcept {
   return epoch_ + boost::posix_time::microseconds( now_.load() );
}

void mock_time_traits::set_now( time_type t ) {
   fc::time_point now( fc::microseconds( (t - epoch_).total_microseconds() ) );
   set_now( now );
}

void mock_time_traits::set_now( const fc::time_point& now ) {
   now_ = now.time_since_epoch().count();
   if( !mock_enabled_ ) mock_enabled_ = true;

   // After modifying the clock, we need to sleep the thread to give the io_service
   // the opportunity to poll and notice the change in clock time.
   // See to_posix_duration()
   std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
}

fc::time_point mock_time_traits::fc_now() {
   return fc::time_point( fc::microseconds( ( mock_time_traits::now() - epoch_ ).total_microseconds() ) );
}

} //namespace fc
