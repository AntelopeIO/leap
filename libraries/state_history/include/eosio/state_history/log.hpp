#pragma once

#include <boost/filesystem.hpp>
#include <fstream>
#include <stdint.h>

#include <eosio/chain/block_header.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/types.hpp>
#include <fc/io/cfile.hpp>
#include <fc/log/logger.hpp>

namespace eosio {

/*
 *   *.log:
 *   +---------+----------------+-----------+------------------+-----+---------+----------------+
 *   | Entry i | Pos of Entry i | Entry i+1 | Pos of Entry i+1 | ... | Entry z | Pos of Entry z |
 *   +---------+----------------+-----------+------------------+-----+---------+----------------+
 *
 *   *.index:
 *   +----------------+------------------+-----+----------------+
 *   | Pos of Entry i | Pos of Entry i+1 | ... | Pos of Entry z |
 *   +----------------+------------------+-----+----------------+
 *
 * each entry:
 *    state_history_log_header
 *    payload
 *
 * When block pruning is enabled, a slight modification to the format is as followed:
 * For first entry in log, a unique version is used to indicate the log is a "pruned log": this prevents
 *  older versions from trying to read something with holes in it
 * The end of the log has a 4 byte value that indicates guaranteed number of blocks the log has at its
 *  end (this can be used to reconstruct an index of the log from the end even when there is a hole in
 *  the middle of the log)
 *
 * In theory it should be possible to transition from a pruned log back to a standard log fairly easily. This
 *  is why only the version in the first block of the file is marked with a different value. But this functionality
 *  is not implemented yet.
 */

inline uint64_t       ship_magic(uint16_t version, uint16_t features = 0) {
   using namespace eosio::chain::literals;
   return "ship"_n.to_uint64_t() | version | features<<16;
}
inline bool           is_ship(uint64_t magic) {
   using namespace eosio::chain::literals;
   return (magic & 0xffff'ffff'0000'0000) == "ship"_n.to_uint64_t();
}
inline uint16_t       get_ship_version(uint64_t magic) { return magic; }
inline uint16_t       get_ship_features(uint64_t magic) { return magic>>16; }
inline bool           is_ship_supported_version(uint64_t magic) { return get_ship_version(magic) == 0; }
static const uint16_t ship_current_version = 0;
static const uint16_t ship_feature_pruned_log = 1;
inline bool           is_ship_log_pruned(uint64_t magic) { return get_ship_features(magic) & ship_feature_pruned_log; }

struct state_history_log_header {
   uint64_t             magic        = ship_magic(ship_current_version);
   chain::block_id_type block_id     = {};
   uint64_t             payload_size = 0;
};
static const int state_history_log_header_serial_size = sizeof(state_history_log_header::magic) +
                                                        sizeof(state_history_log_header::block_id) +
                                                        sizeof(state_history_log_header::payload_size);

class state_history_log {
 private:
   const char* const       name = "";
   std::string             log_filename;
   std::string             index_filename;
   std::optional<uint32_t> prune_blocks;
   bool                    prune_on_exit_if_small = true;
   fc::cfile               log;
   fc::cfile               index;
   uint32_t                _begin_block = 0;        //always tracks the first block available even after pruning
   uint32_t                _index_begin_block = 0;  //the first block of the file; even after pruning. it's what index 0 in the index file points to
   uint32_t                _end_block   = 0;
   chain::block_id_type    last_block_id;

 public:
   state_history_log(const char* const name, std::string log_filename, std::string index_filename, const std::optional<uint32_t> prune_blocks, bool prune_on_exit_if_small = true)
       : name(name)
       , log_filename(std::move(log_filename))
       , index_filename(std::move(index_filename))
       , prune_blocks(prune_blocks)
       , prune_on_exit_if_small(prune_on_exit_if_small) {
      open_log();
      open_index();

      //check for conversions to/from pruned log, as long as log contains something
      if(_begin_block != _end_block) {
         state_history_log_header first_header;
         log.seek(0);
         read_header(first_header);

         if((is_ship_log_pruned(first_header.magic) == false) && prune_blocks) {
            //need to convert non-pruned to pruned; first prune any ranges we can (might be none)
            prune();

            //update first header to indicate prune feature is enabled
            log.seek(0);
            first_header.magic = ship_magic(get_ship_version(first_header.magic), ship_feature_pruned_log);
            write_header(first_header);

            //write trailer on log with num blocks
            log.seek_end(0);
            const uint32_t num_blocks_in_log = _end_block - _begin_block;
            fc::raw::pack(log, num_blocks_in_log);
         }
         else if(is_ship_log_pruned(first_header.magic) && !prune_blocks) {
            vacuum();
         }
      }
   }

   ~state_history_log() {
      //nothing to do if log is empty or we aren't pruning
      if(_begin_block == _end_block)
         return;
      if(!prune_blocks)
         return;

      const size_t first_data_pos = get_pos(_begin_block);
      const size_t last_data_pos = fc::file_size(log.get_file_path());
      if(last_data_pos - first_data_pos < 1024*1024*1024 && prune_on_exit_if_small)
         vacuum();
   }

   uint32_t begin_block() const { return _begin_block; }
   uint32_t end_block() const { return _end_block; }

   void read_header(state_history_log_header& header, bool assert_version = true) {
      char bytes[state_history_log_header_serial_size];
      log.read(bytes, sizeof(bytes));
      fc::datastream<const char*> ds(bytes, sizeof(bytes));
      fc::raw::unpack(ds, header);
      EOS_ASSERT(!ds.remaining(), chain::plugin_exception, "state_history_log_header_serial_size mismatch");
      if (assert_version)
         EOS_ASSERT(is_ship(header.magic) && is_ship_supported_version(header.magic), chain::plugin_exception,
                    "corrupt ${name}.log (0)", ("name", name));
   }

   void write_header(const state_history_log_header& header) {
      char                  bytes[state_history_log_header_serial_size];
      fc::datastream<char*> ds(bytes, sizeof(bytes));
      fc::raw::pack(ds, header);
      EOS_ASSERT(!ds.remaining(), chain::plugin_exception, "state_history_log_header_serial_size mismatch");
      log.write(bytes, sizeof(bytes));
   }

   template <typename F>
   void write_entry(state_history_log_header header, const chain::block_id_type& prev_id, F write_payload) {
      auto block_num = chain::block_header::num_from_id(header.block_id);
      EOS_ASSERT(_begin_block == _end_block || block_num <= _end_block, chain::plugin_exception,
                 "missed a block in ${name}.log", ("name", name));

      if (_begin_block != _end_block && block_num > _begin_block) {
         if (block_num == _end_block) {
            EOS_ASSERT(prev_id == last_block_id, chain::plugin_exception, "missed a fork change in ${name}.log",
                       ("name", name));
         } else {
            state_history_log_header prev;
            get_entry(block_num - 1, prev);
            EOS_ASSERT(prev_id == prev.block_id, chain::plugin_exception, "missed a fork change in ${name}.log",
                       ("name", name));
         }
      }

      if (block_num < _end_block)
         truncate(block_num); //truncate is expected to always leave file pointer at the end
      else if (!prune_blocks)
         log.seek_end(0);
      else if (prune_blocks && _begin_block != _end_block)
         log.seek_end(-sizeof(uint32_t));  //overwrite the trailing block count marker on this write

      //if we're operating on a pruned block log and this is the first entry in the log, make note of the feature in the header
      if(prune_blocks && _begin_block == _end_block)
         header.magic = ship_magic(get_ship_version(header.magic), ship_feature_pruned_log);

      uint64_t pos = log.tellp();
      write_header(header);
      write_payload(log);
      uint64_t end = log.tellp();
      fc::raw::pack(log, pos);

      fc::raw::pack(index, pos);
      if (_begin_block == _end_block)
         _begin_block = block_num;
      _end_block    = block_num + 1;
      last_block_id = header.block_id;

      if(prune_blocks) {
         //only bother to try pruning every 4MB written. except for when the config is set very low (mainly for unit test purposes)
         uint64_t bother_every = 4*1024*1024;
         if(*prune_blocks < 16)
            bother_every = 4096;

         const uint64_t mask = ~(bother_every-1);
         if((pos&mask) != (end&mask))
            prune();

         const uint32_t num_blocks_in_log = _end_block - _begin_block;
         fc::raw::pack(log, num_blocks_in_log);
      }
   }

   // returns cfile positioned at payload
   fc::cfile& get_entry(uint32_t block_num, state_history_log_header& header) {
      EOS_ASSERT(block_num >= _begin_block && block_num < _end_block, chain::plugin_exception,
                 "read non-existing block in ${name}.log", ("name", name));
      log.seek(get_pos(block_num));
      read_header(header);
      return log;
   }

   chain::block_id_type get_block_id(uint32_t block_num) {
      state_history_log_header header;
      get_entry(block_num, header);
      return header.block_id;
   }

 private:
   //file position must be at start of last block's suffix (back pointer)
   bool get_last_block() {
      state_history_log_header header;
      uint64_t                 suffix;

      fc::raw::unpack(log, suffix);
      const size_t after_suffix_pos = log.tellp();
      if (suffix > after_suffix_pos || suffix + state_history_log_header_serial_size > after_suffix_pos) {
         elog("corrupt ${name}.log (2)", ("name", name));
         return false;
      }
      log.seek(suffix);
      read_header(header, false);
      if (!is_ship(header.magic) || !is_ship_supported_version(header.magic) ||
          suffix + state_history_log_header_serial_size + header.payload_size + sizeof(suffix) != after_suffix_pos) {
         elog("corrupt ${name}.log (3)", ("name", name));
         return false;
      }
      _end_block    = chain::block_header::num_from_id(header.block_id) + 1;
      last_block_id = header.block_id;
      if (_begin_block >= _end_block) {
         elog("corrupt ${name}.log (4)", ("name", name));
         return false;
      }
      return true;
   }

   void prune() {
      if(_end_block - _begin_block <= *prune_blocks)
         return;

      const uint32_t prune_to_num = _end_block - *prune_blocks;
      uint64_t prune_to_pos = get_pos(prune_to_num);

      log.punch_hole(state_history_log_header_serial_size, prune_to_pos);

      _begin_block = prune_to_num;
      log.flush();

      ilog("${name}.log pruned to blocks ${b}-${e}", ("name", name)("b", _begin_block)("e", _end_block - 1));
   }

   //only works on non-pruned logs
   void recover_blocks() {
      ilog("recover ${name}.log", ("name", name));
      uint64_t pos       = 0;
      uint32_t num_found = 0;
      log.seek_end(0);
      const size_t size = log.tellp();

      while (true) {
         state_history_log_header header;
         if (pos + state_history_log_header_serial_size > size)
            break;
         log.seek(pos);
         read_header(header, false);
         uint64_t suffix;
         if (!is_ship(header.magic) || !is_ship_supported_version(header.magic) || header.payload_size > size ||
             pos + state_history_log_header_serial_size + header.payload_size + sizeof(suffix) > size) {
            EOS_ASSERT(!is_ship(header.magic) || is_ship_supported_version(header.magic), chain::plugin_exception,
                       "${name}.log has an unsupported version", ("name", name));
            break;
         }
         log.seek(pos + state_history_log_header_serial_size + header.payload_size);
         log.read((char*)&suffix, sizeof(suffix));
         if (suffix != pos)
            break;
         pos = pos + state_history_log_header_serial_size + header.payload_size + sizeof(suffix);
         if (!(++num_found % 10000)) {
            dlog("${num_found} blocks found, log pos = ${pos}", ("num_found", num_found)("pos", pos));
         }
      }
      log.flush();
      boost::filesystem::resize_file(log_filename, pos);
      log.flush();

      log.seek_end(-sizeof(pos));
      EOS_ASSERT(get_last_block(), chain::plugin_exception, "recover ${name}.log failed", ("name", name));
   }

   void open_log() {
      log.set_file_path(log_filename);
      log.open("a+b");
      log.seek_end(0);
      uint64_t size = log.tellp();
      log.close();

      log.open("r+b");
      if (size >= state_history_log_header_serial_size) {
         state_history_log_header header;
         log.seek(0);
         read_header(header, false);
         EOS_ASSERT(is_ship(header.magic) && is_ship_supported_version(header.magic) &&
                        state_history_log_header_serial_size + header.payload_size + sizeof(uint64_t) <= size,
                    chain::plugin_exception, "corrupt ${name}.log (1)", ("name", name));

         log.seek_end(0);

         std::optional<uint32_t> pruned_count;
         if(is_ship_log_pruned(header.magic)) {
            //the existing log is a prune'ed log. find the count of blocks at the end
            log.skip(-sizeof(uint32_t));
            uint32_t count;
            fc::raw::unpack(log, count);
            pruned_count = count;
            log.skip(-sizeof(uint32_t));
         }

         _index_begin_block = _begin_block  = chain::block_header::num_from_id(header.block_id);
         last_block_id = header.block_id;
         log.skip(-sizeof(uint64_t));
         if(!get_last_block()) {
            EOS_ASSERT(!is_ship_log_pruned(header.magic), chain::plugin_exception, "${name}.log is pruned and cannot have recovery attempted", ("name", name));
            recover_blocks();
         }

         if(pruned_count)
            _begin_block = _end_block - *pruned_count;

         ilog("${name}.log has blocks ${b}-${e}", ("name", name)("b", _begin_block)("e", _end_block - 1));
      } else {
         EOS_ASSERT(!size, chain::plugin_exception, "corrupt ${name}.log (5)", ("name", name));
         ilog("${name}.log is empty", ("name", name));
      }
   }

   void open_index() {
      index.set_file_path(index_filename);
      index.open("a+b");
      index.seek_end(0);
      if (index.tellp() == (static_cast<int>(_end_block) - _index_begin_block) * sizeof(uint64_t))
         return;
      ilog("Regenerate ${name}.index", ("name", name));
      index.close();

      index.open("wb");
      log.seek_end(0);
      if(log.tellp()) {
         uint32_t remaining = _end_block - _begin_block;
         index.seek((_end_block - _index_begin_block)*sizeof(uint64_t));  //this can make the index sparse for a pruned log; but that's okay

         log.seek(0);
         state_history_log_header first_entry_header;
         read_header(first_entry_header);
         if(is_ship_log_pruned(first_entry_header.magic))
            log.seek_end(-sizeof(uint32_t));

         while(remaining--) {
            uint64_t pos = 0;
            state_history_log_header header;
            log.skip(-sizeof(pos));
            fc::raw::unpack(log, pos);
            log.seek(pos);
            read_header(header, false);
            log.seek(pos);
            EOS_ASSERT(is_ship(header.magic) && is_ship_supported_version(header.magic), chain::plugin_exception, "corrupt ${name}.log (6)", ("name", name));

            index.skip(-sizeof(uint64_t));
            fc::raw::pack(index, pos);
            index.skip(-sizeof(uint64_t));

            if (!(remaining % 10000))
               dlog("${remaining} blocks remaining, log pos = ${pos}", ("num_found", remaining)("pos", pos));
         }
      }

      index.close();
      index.open("a+b");
   }

   uint64_t get_pos(uint32_t block_num) {
      uint64_t pos;
      index.seek((block_num - _index_begin_block) * sizeof(pos));
      index.read((char*)&pos, sizeof(pos));
      return pos;
   }

   void truncate(uint32_t block_num) {
      log.flush();
      index.flush();
      uint64_t num_removed = 0;
      if (block_num <= _begin_block) {
         num_removed = _end_block - _begin_block;
         log.seek(0);
         boost::filesystem::resize_file(log_filename, 0);
         boost::filesystem::resize_file(index_filename, 0);
         _begin_block = _end_block = 0;
      } else {
         num_removed  = _end_block - block_num;
         uint64_t pos = get_pos(block_num);
         log.seek(0);
         boost::filesystem::resize_file(log_filename, pos);
         boost::filesystem::resize_file(index_filename, (block_num - _begin_block) * sizeof(uint64_t));
         _end_block = block_num;
      }
      log.seek_end(0);
      ilog("fork or replay: removed ${n} blocks from ${name}.log", ("n", num_removed)("name", name));
   }

   void vacuum() {
      //a completely empty log should have nothing on disk; don't touch anything
      if(_begin_block == _end_block)
         return;

      size_t copy_from_pos = get_pos(_begin_block) + sizeof(uint64_t); //skip first copied block's magic
      //go ahead and clear the pruned flag from the log file. If we fail to completely vacuum well maybe recovery can sort out something
      log.seek(0);
      fc::raw::pack(log, ship_magic(ship_current_version));
      size_t copy_to_pos = log.tellp();

      //may happen if _begin_block is still first block on-disk of log. just erase 4 byte trailer
      if(_begin_block == _index_begin_block) {
         log.flush();
         fc::resize_file(log.get_file_path(), fc::file_size(log.get_file_path()) - sizeof(uint32_t));
         return;
      }

      ilog("Vacuuming pruned log ${n}", ("n", name));

      const size_t offset_bytes = copy_from_pos - copy_to_pos;
      const size_t offset_blocks = _begin_block - _index_begin_block;
      log.seek_end(0);
      size_t copy_sz = log.tellp() - copy_from_pos - sizeof(uint32_t); //don't copy trailer in to new unpruned log
      const uint32_t num_blocks_in_log = _end_block - _begin_block;

      std::vector<char> buff;
      buff.resize(4*1024*1024);

      auto tick = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
      while(copy_sz) {
         const size_t copy_this_round = std::min(buff.size(), copy_sz);
         log.seek(copy_from_pos);
         log.read(buff.data(), copy_this_round);
         log.punch_hole(copy_from_pos, copy_from_pos+copy_this_round);
         log.seek(copy_to_pos);
         log.write(buff.data(), copy_this_round);

         copy_from_pos += copy_this_round;
         copy_to_pos += copy_this_round;
         copy_sz -= copy_this_round;

         const auto tock = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
         if(tick != tock)
            ilog("Vacuuming pruned log ${n}, ${b} bytes remaining", ("b", copy_sz)("n", name));
         tick = tock;
      }
      log.flush();
      fc::resize_file(log.get_file_path(), log.tellp());

      index.flush();
      {
         struct wrap_cfile {
            wrap_cfile(fc::cfile& f) : f(f) {}
            boost::interprocess::mapping_handle_t get_mapping_handle() const {return {f.fileno(), false};};
            fc::cfile& f;
         } cfile_map_handle(index);
         boost::interprocess::mapped_region index_mapped(cfile_map_handle, boost::interprocess::read_write);
         uint64_t* index_ptr = (uint64_t*)index_mapped.get_address();

         for(uint32_t new_block_num = 0; new_block_num < num_blocks_in_log; ++new_block_num) {
            const uint64_t new_pos = index_ptr[new_block_num + offset_blocks] - offset_bytes;
            index_ptr[new_block_num] = new_pos;

            if(new_block_num + 1 != num_blocks_in_log)
               log.seek(index_ptr[new_block_num + offset_blocks + 1] - offset_bytes - sizeof(uint64_t));
            else
               log.seek_end(-sizeof(uint64_t));
            log.write((char*)&new_pos, sizeof(new_pos));
         }
      }
      fc::resize_file(index.get_file_path(), num_blocks_in_log*sizeof(uint64_t));

      _index_begin_block = _begin_block;
      ilog("Vacuum of pruned log ${n} complete",("n", name));
   }
}; // state_history_log

} // namespace eosio

FC_REFLECT(eosio::state_history_log_header, (magic)(block_id)(payload_size))
