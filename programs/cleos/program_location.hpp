#pragma once
#include <filesystem>
#include <boost/predef/os.h>
#ifdef __APPLE__
#   include <mach-o/dyld.h>
#   include <vector>
#endif

// adapted from boost source boost/dll/detail/posix/program_location_impl.hpp
[[gnu::pure]] std::filesystem::path program_location() {
#if BOOST_OS_MACOS || BOOST_OS_IOS
   char     path[1024];
   uint32_t size = sizeof(path);
   if (_NSGetExecutablePath(path, &size) == 0)
      return std::filesystem::path(path);

   std::vector<char> buf(size);
   if (_NSGetExecutablePath(buf.data(), &size) != 0) {
      throw std::system_error{ std::make_error_code(std::errc::bad_file_descriptor) };
   }
   return std::filesystem::path{ buf.data() };
#elif BOOST_OS_WINDOWS || BOOST_OS_SOLARIS || BOOST_OS_BSD_FREE || BOOST_OS_BSD_NET || BOOST_OS_BSD_DRAGONFLY || BOOST_OS_QNX
#error "Unsupported platform"
#else // BOOST_OS_LINUX || BOOST_OS_UNIX || BOOST_OS_HPUX || BOOST_OS_ANDROID
   return std::filesystem::canonical("/proc/self/exe");
#endif
}
