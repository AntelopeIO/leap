#pragma once

#include <boost/filesystem/path.hpp>
#include <fc/io/cfile.hpp>

namespace eosio {
namespace chain {
/// copy up to n bytes from the current position of src to dest
void copy_file_content(fc::cfile& src, fc::cfile& dest, uint64_t n = UINT64_MAX);

template <typename Exception>
class log_index {
   fc::cfile file_;
   std::size_t num_blocks_ = 0;
 public:
   log_index() = default;
   log_index(const boost::filesystem::path& path) {
      open(path);
   }

   void open(const boost::filesystem::path& path) {
      if (file_.is_open())
         file_.close();
      file_.set_file_path(path);
      file_.open("rb");
      file_.seek_end(0);
      num_blocks_ = file_.tellp()/ sizeof(uint64_t);
      EOS_ASSERT(file_.tellp() % sizeof(uint64_t) == 0, Exception,
                 "The size of ${file} is not a multiple of sizeof(uint64_t)", ("file", path.generic_string()));
   }

   bool is_open() const { return file_.is_open(); }

   uint64_t back() { return nth_block_position(num_blocks()-1); }
   unsigned num_blocks() const { return num_blocks_; }
   uint64_t nth_block_position(uint32_t n) {
      file_.seek(n*sizeof(uint64_t));
      uint64_t r;
      file_.read((char*)&r, sizeof(r));
      return r;
   }

   void copy_to(fc::cfile& dest, uint64_t nbytes) {
      file_.seek(0);
      copy_file_content(file_, dest, nbytes);
   }

};

} // namespace chain
} // namespace eosio
