#pragma once

#include <cli11/CLI11.hpp>

#include <memory>
#include <fc/exception/exception.hpp>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/exception_ptr.hpp>

template <class subcommand_options> class sub_command {
protected:
  std::shared_ptr<subcommand_options> opt;
  sub_command() : opt(std::make_shared<subcommand_options>()) {}

  void print_exception() noexcept {
    try {
      throw;
    } catch (const fc::exception &e) {
      elog("${e}", ("e", e.to_detail_string()));
    } catch (const boost::exception &e) {
      elog("${e}", ("e", boost::diagnostic_information(e)));
    } catch (const CLI::RuntimeError &e) {
      // avoid reporting it twice, RuntimeError is only for cli11
    } catch (const std::exception &e) {
      elog("${e}", ("e", e.what()));
    } catch (...) {
      elog("unknown exception");
    }
  }

public:
  virtual ~sub_command() {}
  virtual void setup(CLI::App &app) = 0;
};