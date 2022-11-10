#include "subcommand.hpp"
#include <boost/exception/diagnostic_information.hpp>
#include <boost/exception_ptr.hpp>
#include <fc/exception/exception.hpp>

void leap_util_exception_handler::print_exception() noexcept {
   try {
      throw;
   } catch(const fc::exception& e) {
      elog("${e}", ("e", e.to_detail_string()));
   } catch(const boost::exception& e) {
      elog("${e}", ("e", boost::diagnostic_information(e)));
   } catch(const CLI::RuntimeError& e) {
      // avoid reporting it twice, RuntimeError is only for cli11
   } catch(const std::exception& e) {
      elog("${e}", ("e", e.what()));
   } catch(...) {
      elog("unknown exception");
   }
}
