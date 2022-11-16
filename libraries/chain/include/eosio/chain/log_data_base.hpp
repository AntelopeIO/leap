#pragma once
#include <boost/iostreams/device/mapped_file.hpp>

namespace eosio {
namespace chain {

template <typename T>
T read_buffer(const char* buf) {
   T result;
   memcpy(&result, buf, sizeof(T));
   return result;
}

template <typename Derived>
class log_data_base {
 protected:
   boost::iostreams::mapped_file file;

   const Derived* self() const { return static_cast<const Derived*>(this); }

 public:
   using mapmode = boost::iostreams::mapped_file::mapmode;

   log_data_base() = default;

   void    close() { file.close(); }
   bool    is_open() const { return file.is_open(); }
   mapmode flags() const { return file.flags(); }

   const char* data() const { return file.const_data(); }
   uint64_t    size() const { return file.size(); }
   uint32_t    last_block_num() const { return self()->block_num_at(last_block_position()); }
   uint64_t    last_block_position() const {
      uint32_t offset = sizeof(uint64_t);
      if (self()->is_currently_pruned())
         offset += sizeof(uint32_t);
      return read_buffer<uint64_t>(data() + size() - offset);
   }

   uint32_t num_blocks() const {
      if (self()->first_block_position() == file.size())
         return 0;
      else if (self()->is_currently_pruned())
         return read_buffer<uint32_t>(data() + size() - sizeof(uint32_t));
      return last_block_num() - self()->first_block_num() + 1;
   }
};
} // namespace chain
} // namespace eosio