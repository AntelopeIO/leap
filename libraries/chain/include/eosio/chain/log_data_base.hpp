#pragma once
#include <fc/io/cfile.hpp>

namespace eosio {
namespace chain {

template <typename T>
T read_buffer(const char* buf) {
   T result;
   memcpy(&result, buf, sizeof(T));
   return result;
}

template <typename T>
T read_data_at(fc::datastream<fc::cfile>& file, std::size_t offset) {
   file.seek(offset);
   T value;
   fc::raw::unpack(file, value);
   return value;
}

template <typename Derived>
class log_data_base {
 protected:
   fc::datastream<fc::cfile> file;

   Derived* self() { return static_cast<Derived*>(this); }
   const Derived* self() const { return static_cast<const Derived*>(this); }

 public:

   log_data_base() = default;

   void    close() { file.close(); }
   bool    is_open() const { return file.is_open(); }

   uint64_t    size() const { return self()->size(); }

   uint32_t    last_block_num() { return self()->block_num_at(last_block_position()); }
   uint64_t    last_block_position() {
      uint32_t offset = sizeof(uint64_t);
      if (self()->is_currently_pruned())
         offset += sizeof(uint32_t);
      return read_data_at<uint64_t>(file, size() - offset);
   }

   uint32_t num_blocks() {
      if (self()->first_block_position() == size())
         return 0;
      else if (self()->is_currently_pruned())
         return read_data_at<uint32_t>(file, size() - sizeof(uint32_t));
      return last_block_num() - self()->first_block_num() + 1;
   }
};
} // namespace chain
} // namespace eosio