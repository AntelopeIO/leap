#pragma once
#include <fc/io/cfile.hpp>
#include <fc/io/raw.hpp>

namespace fc {

   /**
    * This namespace provides functions related to reading and writing a persistence file
    * with a header delineating the type of file and version.
    */

namespace persistence_util {

   cfile open_cfile_for_read(const std::filesystem::path& dir, const std::string& filename) {
      if (!std::filesystem::is_directory(dir))
         std::filesystem::create_directories(dir);

      auto dat_file = dir / filename;

      cfile dat_content;
      dat_content.set_file_path(dat_file);
      if( std::filesystem::exists( dat_file ) ) {
         dat_content.open(cfile::create_or_update_rw_mode);
      }
      return dat_content;
   }

   uint32_t read_persistence_header(cfile& dat_content, const uint32_t magic_number, const uint32_t min_supported_version,
      const uint32_t max_supported_version) {
      dat_content.seek(0); // needed on mac
      auto ds = dat_content.create_datastream();

      // validate totem
      uint32_t totem = 0;
      fc::raw::unpack( ds, totem );
      if( totem != magic_number) {
         FC_THROW_EXCEPTION(fc::parse_error_exception,
                            "File has unexpected magic number: ${actual_totem}. Expected ${expected_totem}",
                            ("actual_totem", totem)
                            ("expected_totem", magic_number));
      }

      // validate version
      uint32_t version = 0;
      fc::raw::unpack( ds, version );
      if( version < min_supported_version || version > max_supported_version) {
         FC_THROW_EXCEPTION(fc::parse_error_exception,
                            "Unsupported version for file. "
                            "Version is ${version} while code supports version(s) [${min},${max}]",
                            ("version", version)
                            ("min", min_supported_version)
                            ("max", max_supported_version));
      }

      return version;
   }

   cfile open_cfile_for_write(const std::filesystem::path& dir, const std::string& filename) {
      if (!std::filesystem::is_directory(dir))
         std::filesystem::create_directories(dir);

      auto dat_file = dir / filename;
      cfile dat_content;
      dat_content.set_file_path(dat_file.generic_string().c_str());
      dat_content.open( cfile::truncate_rw_mode );
      return dat_content;
   }

   void write_persistence_header(cfile& dat_content, const uint32_t magic_number, const uint32_t current_version) {
      dat_content.write( reinterpret_cast<const char*>(&magic_number), sizeof(magic_number) );
      dat_content.write( reinterpret_cast<const char*>(&current_version), sizeof(current_version) );
   }
} // namespace persistence_util

} // namespace fc
