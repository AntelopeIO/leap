#pragma once
#include <memory>
#include <utility>

#include <fc/fwd.hpp>
#include <fc/reflect/typename.hpp>
#include <fc/string.hpp>

#include <filesystem>

namespace fc {

/** @return the home directory on Linux and OS X and the Profile directory on Windows */
const std::filesystem::path& home_path();

/** @return the home_path() on Linux, home_path()/Library/Application Support/ on OS X,
 *  and APPDATA on windows
 */
const std::filesystem::path& app_path();

class variant;
void to_variant(const std::filesystem::path&, fc::variant&);
void from_variant(const fc::variant&, std::filesystem::path&);

template <>
struct get_typename<std::filesystem::path> {
   static const char* name() { return "path"; }
};

class temp_directory {
   std::filesystem::path tmp_path;

 public:
   temp_directory(const std::filesystem::path& tempFolder = std::filesystem::temp_directory_path()) {
      std::filesystem::path template_path{ tempFolder / "fc-XXXXXX" };
      std::string tmp_buf = template_path.string();
      // The following is valid because the return array of std::string::data() is null-terminated since C++11
      if (mkdtemp(tmp_buf.data()) == nullptr)
         throw std::system_error(errno, std::generic_category(), __PRETTY_FUNCTION__);
      tmp_path = tmp_buf;
   }
   temp_directory(const temp_directory&) = delete;
   temp_directory(temp_directory&& other) { tmp_path.swap(other.tmp_path); }

   ~temp_directory() {
      if (!tmp_path.empty()) {
         std::error_code ec;
         std::filesystem::remove_all(tmp_path, ec);
      }
   }

   temp_directory& operator=(const temp_directory&) = delete;
   temp_directory& operator=(temp_directory&& other) {
      tmp_path.swap(other.tmp_path);
      return *this;
   }
   const std::filesystem::path& path() const { return tmp_path; }
};


} // namespace fc
