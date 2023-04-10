#pragma once
#include <boost/predef/os.h>
#include <filesystem>

// adapted from boost source boost/dll/detail/posix/program_location_impl.hpp
#if BOOST_OS_MACOS || BOOST_OS_IOS
#   include <mach-o/dyld.h>
#   include <vector>
   [[gnu::pure]] std::filesystem::path program_location() {
      char     path[1024];
      uint32_t size = sizeof(path);
      if (_NSGetExecutablePath(path, &size) == 0)
         return std::filesystem::path(path);

      std::vector<char> buf(size);
      if (_NSGetExecutablePath(buf.data(), &size) != 0) {
         throw std::system_error{ std::make_error_code(std::errc::bad_file_descriptor) };
      }
      return std::filesystem::path{ buf.data() };
   }
#elif BOOST_OS_WINDOWS
#   error "Unsupported platform"
#elif BOOST_OS_SOLARIS
#include <stdlib.h>
   [[gnu::pure]] std::filesystem::path program_location() { return std::filesystem::path(getexecname()); }
#elif BOOST_OS_BSD_FREE
#   include <stdlib.h>
#   include <sys/sysctl.h>
#   include <sys/types.h>

   [[gnu::pure]] std::filesystem::path program_location() {
      int mib[4];
      mib[0] = CTL_KERN;
      mib[1] = KERN_PROC;
      mib[2] = KERN_PROC_PATHNAME;
      mib[3] = -1;
      char   buf[10240];
      size_t cb = sizeof(buf);
      sysctl(mib, 4, buf, &cb, NULL, 0);

      return std::filesystem::path(buf);
   }
#elif BOOST_OS_BSD_NET
   [[gnu::pure]] std::filesystem::path program_location() { return std::filesystem::canonical("/proc/curproc/exe"); }
#elif BOOST_OS_BSD_DRAGONFLY
   [[gnu::pure]] std::filesystem::path program_location() { return std::filesystem::canonical("/proc/curproc/file"); }
#elif BOOST_OS_QNX

#   include <fstream>
#   include <string> // for std::getline
   [[gnu::pure]] std::filesystem::path program_location() {

      std::string   s;
      std::ifstream ifs("/proc/self/exefile");
      std::getline(ifs, s);

      if (ifs.fail() || s.empty()) {
         throw std::system_error{ std::make_error_code(std::errc::bad_file_descriptor) };
      }

      return std::filesystem::path(s);
   }
#else // BOOST_OS_LINUX || BOOST_OS_UNIX || BOOST_OS_HPUX || BOOST_OS_ANDROID
   [[gnu::pure]] std::filesystem::path program_location() { return std::filesystem::canonical("/proc/self/exe"); }
#endif
