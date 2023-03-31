#pragma once

#include <boost/filesystem.hpp>
#include <fstream>
#include <stdint.h>

#include <boost/asio.hpp>

#include <eosio/chain/block_header.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/types.hpp>
#include <fc/io/cfile.hpp>
#include <fc/log/logger.hpp>
#include <fc/log/logger_config.hpp> //set_thread_name

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
 */

inline uint64_t       ship_magic(uint16_t version, uint16_t features = 0) {
   using namespace eosio::chain::literals;
   return "ship"_n.to_uint64_t() | version | features<<16;
}
inline bool is_ship(uint64_t magic) {
   using namespace eosio::chain::literals;
   return (magic & 0xffff'ffff'0000'0000) == "ship"_n.to_uint64_t();
}
inline uint16_t       get_ship_version(uint64_t magic) { return magic; }
inline uint16_t       get_ship_features(uint64_t magic) { return magic>>16; }
inline bool           is_ship_supported_version(uint64_t magic) { return get_ship_version(magic) == 0; }
static const uint16_t ship_current_version = 0;
static const uint16_t ship_feature_pruned_log = 1;
inline bool           is_ship_log_pruned(uint64_t magic) { return get_ship_features(magic) & ship_feature_pruned_log; }
inline uint64_t       clear_ship_log_pruned_feature(uint64_t magic) { return ship_magic(get_ship_version(magic), get_ship_features(magic) & ~ship_feature_pruned_log); }

struct state_history_log_header {
   uint64_t             magic        = ship_magic(ship_current_version);
   chain::block_id_type block_id     = {};
   uint64_t             payload_size = 0;
};
static const int state_history_log_header_serial_size = sizeof(state_history_log_header::magic) +
                                                        sizeof(state_history_log_header::block_id) +
                                                        sizeof(state_history_log_header::payload_size);
struct state_history_log_prune_config {
   uint32_t                prune_blocks;                  //number of blocks to prune to when doing a prune
   size_t                  prune_threshold = 4*1024*1024; //(approximately) how many bytes need to be added before a prune is performed
   std::optional<size_t>   vacuum_on_close;               //when set, a vacuum is performed on dtor if log contains less than this many bytes
};

class state_history_log {
 private:
   const char* const       name = "";
   std::string             log_filename;
   std::string             index_filename;
   std::optional<state_history_log_prune_config> prune_config; //is set, log is in pruned mode
   fc::cfile               log;
   fc::cfile               index;
   uint32_t                _begin_block = 0;        //always tracks the first block available even after pruning
   uint32_t                _index_begin_block = 0;  //the first block of the file; even after pruning. it's what index 0 in the index file points to
   uint32_t                _end_block   = 0;
   chain::block_id_type    last_block_id;

   std::thread                                                              thr;
   std::atomic<bool>                                                        write_thread_has_exception = false;
   std::exception_ptr                                                       eptr;
   boost::asio::io_context                                                  ctx;
   boost::asio::io_context::strand                                          work_strand{ctx};
   boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard =
       boost::asio::make_work_guard(ctx);
   std::recursive_mutex                                                     mx;

 public:
   state_history_log(const char* const name, std::string log_filename, std::string index_filename,
                     std::optional<state_history_log_prune_config> prune_conf = std::optional<state_history_log_prune_config>())
       : name(name)
       , log_filename(std::move(log_filename))
       , index_filename(std::move(index_filename))
       , prune_config(prune_conf) {
      open_log();
      open_index();
     
      if(prune_config) {
         EOS_ASSERT(prune_config->prune_blocks, chain::plugin_exception, "state history log prune configuration requires at least one block");
         EOS_ASSERT(__builtin_popcount(prune_config->prune_threshold) == 1, chain::plugin_exception, "state history prune threshold must be power of 2");
         //switch this over to the mask that will be used
         prune_config->prune_threshold = ~(prune_config->prune_threshold-1);
      }

      //check for conversions to/from pruned log, as long as log contains something
      if(_begin_block != _end_block) {
         state_history_log_header first_header;
         log.seek(0);
         read_header(first_header);

         if((is_ship_log_pruned(first_header.magic) == false) && prune_config) {
            //need to convert non-pruned to pruned; first prune any ranges we can (might be none)
            prune(fc::log_level::info);

            //update first header to indicate prune feature is enabled
            log.seek(0);
            first_header.magic = ship_magic(get_ship_version(first_header.magic), ship_feature_pruned_log);
            write_header(first_header);

            //write trailer on log with num blocks
            log.seek_end(0);
            const uint32_t num_blocks_in_log = _end_block - _begin_block;
            fc::raw::pack(log, num_blocks_in_log);
         }
         else if(is_ship_log_pruned(first_header.magic) && !prune_config) {
            vacuum();
         }
      }

       thr = std::thread([this] {
         try {
            fc::set_os_thread_name(this->name);
            this->ctx.run();
         } catch (...) {
            elog("catched exception from ${name} write thread", ("name", this->name));
            eptr                       = std::current_exception();
            write_thread_has_exception = true;
         }
      });
   }

   void stop() {
      if (thr.joinable()) {
         work_guard.reset();
         thr.join();
      }
   }

   ~state_history_log() {
      // complete execution before possible vacuuming
      if (thr.joinable()) {
         work_guard.reset();
         thr.join();
      }     

      //nothing to do if log is empty or we aren't pruning
      if(_begin_block == _end_block)
         return;
      if(!prune_config || !prune_config->vacuum_on_close)
         return;

      const size_t first_data_pos = get_pos(_begin_block);
      const size_t last_data_pos = fc::file_size(log.get_file_path());
      if(last_data_pos - first_data_pos < *prune_config->vacuum_on_close)
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
      if (write_thread_has_exception) {
         std::rethrow_exception(eptr);
      }

      std::unique_lock<std::recursive_mutex> lock(mx);
      
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

      if (block_num < _end_block) {
         // This is typically because of a fork, and we need to truncate the log back to the beginning of the fork.
         static uint32_t start_block_num = block_num;
         // Guard agaisnt accidently starting a fresh chain with an existing ship log, require manual removal of ship logs.
         EOS_ASSERT( block_num > 2, chain::plugin_exception, "Existing ship log with ${eb} blocks when starting from genesis block ${b}",
                     ("eb", _end_block)("b", block_num) );
         // block_num < _begin_block = pruned log, need to call truncate() to reset
         // get_block_id_i check is an optimization to avoid writing a block that is already in the log (snapshot or replay)
         if ( block_num < _begin_block || get_block_id_i(block_num) != header.block_id ) {
            truncate(block_num); //truncate is expected to always leave file pointer at the end
         } else {
            if (start_block_num == block_num || block_num % 1000 == 0 )
               ilog("log ${name}.log already contains block ${b}, end block ${eb}", ("name", name)("b", block_num)("eb", _end_block));
            return;
         }
      } else if (!prune_config) {
         log.seek_end(0);
      } else if (prune_config && _begin_block != _end_block) {
         log.seek_end(-sizeof(uint32_t));  //overwrite the trailing block count marker on this write
      }

      //if we're operating on a pruned block log and this is the first entry in the log, make note of the feature in the header
      if(prune_config && _begin_block == _end_block)
         header.magic = ship_magic(get_ship_version(header.magic), ship_feature_pruned_log);

      uint64_t pos = log.tellp();
            
      write_header(header);
      write_payload(log);

      EOS_ASSERT(log.tellp() == pos + state_history_log_header_serial_size + header.payload_size, chain::plugin_exception,
                 "wrote payload with incorrect size to ${name}.log", ("name", name));
      fc::raw::pack(log, pos);

      fc::raw::pack(index, pos);
      if (_begin_block == _end_block)
         _index_begin_block = _begin_block = block_num;
      _end_block    = block_num + 1;
      last_block_id = header.block_id;

      if(prune_config) {
         if((pos&prune_config->prune_threshold) != (log.tellp()&prune_config->prune_threshold))
            prune(fc::log_level::debug);

         const uint32_t num_blocks_in_log = _end_block - _begin_block;
         fc::raw::pack(log, num_blocks_in_log);
      }

      log.flush();
      index.flush();
   }

   // returns cfile positioned at payload
   fc::cfile& get_entry(uint32_t block_num, state_history_log_header& header) {
      std::lock_guard lock(mx);
      return get_entry_i(block_num, header);
   }

   chain::block_id_type get_block_id(uint32_t block_num) {
      std::lock_guard lock(mx);
      return get_block_id_i(block_num);
   }

 private:

   fc::cfile& get_entry_i(uint32_t block_num, state_history_log_header& header) {
      EOS_ASSERT(block_num >= _begin_block && block_num < _end_block, chain::plugin_exception,
                 "read non-existing block in ${name}.log", ("name", name));
      log.seek(get_pos(block_num));
      read_header(header);
      return log;
   }

   chain::block_id_type get_block_id_i(uint32_t block_num) {
      state_history_log_header header;
      get_entry_i(block_num, header);
      return header.block_id;
   }

   //file position must be at start of last block's suffix (back pointer)
   //called from open_log / ctor 
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

   void prune(const fc::log_level& loglevel) {
      if(!prune_config)
         return;
      if(_end_block - _begin_block <= prune_config->prune_blocks)
         return;

      const uint32_t prune_to_num = _end_block - prune_config->prune_blocks;
      uint64_t prune_to_pos = get_pos(prune_to_num);

      log.punch_hole(state_history_log_header_serial_size, prune_to_pos);

      _begin_block = prune_to_num;
      log.flush();

      if(auto l = fc::logger::get(); l.is_enabled(loglevel))
         l.log(fc::log_message(fc::log_context(loglevel, __FILE__, __LINE__, __func__),
                               "${name}.log pruned to blocks ${b}-${e}", fc::mutable_variant_object()("name", name)("b", _begin_block)("e", _end_block - 1)));
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
            ilog("${num_found} blocks found, log pos = ${pos}", ("num_found", num_found)("pos", pos));
         }
      }
      log.flush();
      boost::filesystem::resize_file(log_filename, pos);
      log.flush();

      log.seek_end(-sizeof(pos));
      EOS_ASSERT(get_last_block(), chain::plugin_exception, "recover ${name}.log failed", ("name", name));
   }

   // only called from constructor
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

   // only called from constructor
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
         log.seek_end(0);
         if(is_ship_log_pruned(first_entry_header.magic))
            log.skip(-sizeof(uint32_t));

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
               ilog("${r} blocks remaining, log pos = ${pos}", ("r", remaining)("pos", pos));
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
         boost::filesystem::resize_file(index_filename, (block_num - _index_begin_block) * sizeof(uint64_t));
         _end_block = block_num;
         //this will leave the end of the log with the last block's suffix no matter if the log is operating in pruned
         // mode or not. The assumption is truncate() is always immediately followed up with an append to the log thus
         // restoring the prune trailer if required
      }
      log.seek_end(0);
      ilog("fork or replay: removed ${n} blocks from ${name}.log", ("n", num_removed)("name", name));
   }

   void vacuum() {
      //a completely empty log should have nothing on disk; don't touch anything
      if(_begin_block == _end_block)
         return;

      log.seek(0);
      uint64_t magic;
      fc::raw::unpack(log, magic);
      EOS_ASSERT(is_ship_log_pruned(magic), chain::plugin_exception, "vacuum can only be performed on pruned logs");

      //may happen if _begin_block is still first block on-disk of log. clear the pruned feature flag & erase
      // the 4 byte trailer. The pruned flag is only set on the first header in the log, so it does not need
      // to be touched up if we actually vacuum up any other blocks to the front.
      if(_begin_block == _index_begin_block) {
         log.seek(0);
         fc::raw::pack(log, clear_ship_log_pruned_feature(magic));
         log.flush();
         fc::resize_file(log.get_file_path(), fc::file_size(log.get_file_path()) - sizeof(uint32_t));
         return;
      }

      ilog("Vacuuming pruned log ${n}", ("n", name));

      size_t copy_from_pos = get_pos(_begin_block);
      size_t copy_to_pos = 0;

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
         log.punch_hole(copy_to_pos, copy_from_pos+copy_this_round);
         log.seek(copy_to_pos);
         log.write(buff.data(), copy_this_round);

         copy_from_pos += copy_this_round;
         copy_to_pos += copy_this_round;
         copy_sz -= copy_this_round;

         const auto tock = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
         if(tick < tock - std::chrono::seconds(5)) {
            ilog("Vacuuming pruned log ${n}, ${b} bytes remaining", ("b", copy_sz)("n", name));
            tick = tock;
         }
      }
      log.flush();
      fc::resize_file(log.get_file_path(), log.tellp());

      index.flush();
      {
         boost::interprocess::mapped_region index_mapped(index, boost::interprocess::read_write);
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
