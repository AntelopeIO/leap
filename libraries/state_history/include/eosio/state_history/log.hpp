#pragma once

#include <eosio/state_history/compression.hpp>
#include <eosio/chain/block_header.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/log_catalog.hpp>
#include <eosio/chain/log_data_base.hpp>
#include <eosio/chain/log_index.hpp>

#include <fc/io/cfile.hpp>
#include <fc/log/logger.hpp>
#include <fc/log/logger_config.hpp> //set_thread_name
#include <fc/bitutil.hpp>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/restrict.hpp>

#include <fstream>
#include <cstdint>


struct state_history_test_fixture;

namespace eosio {
namespace bio = boost::iostreams;

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

namespace state_history {
   struct prune_config {
      uint32_t                prune_blocks;                  //number of blocks to prune to when doing a prune
      size_t                  prune_threshold = 4*1024*1024; //(approximately) how many bytes need to be added before a prune is performed
      std::optional<size_t>   vacuum_on_close;               //when set, a vacuum is performed on dtor if log contains less than this many bytes
   };

   struct partition_config {
      fc::path retained_dir       = "retained";
      fc::path archive_dir        = "archive";
      uint32_t stride             = 1000000;
      uint32_t max_retained_files = 10;
   };
} // namespace state_history

using state_history_log_config = std::variant<std::monostate, state_history::prune_config, state_history::partition_config>;

struct locked_decompress_stream {
   std::unique_lock<std::mutex> lock; // state_history_log mutex
   std::variant<std::vector<char>, std::unique_ptr<bio::filtering_istreambuf>> buf;

   locked_decompress_stream() = delete;
   locked_decompress_stream(locked_decompress_stream&&) = default;

   explicit locked_decompress_stream(std::unique_lock<std::mutex> l)
   : lock(std::move(l)) {};

   template <typename StateHistoryLog>
   void init(StateHistoryLog&& log, fc::cfile& stream, uint64_t compressed_size) {
      auto istream = std::make_unique<bio::filtering_istreambuf>();
      istream->push(bio::zlib_decompressor());
      istream->push(bio::restrict(bio::file_source(stream.get_file_path().string()), stream.tellp(), compressed_size));
      buf = std::move(istream);
   }

   template <typename LogData>
   void init(LogData&& log, fc::datastream<const char*>& stream, uint64_t compressed_size) {
      auto istream = std::make_unique<bio::filtering_istreambuf>();
      istream->push(bio::zlib_decompressor());
      istream->push(bio::restrict(bio::file_source(log.filename), stream.pos() - log.data(), compressed_size));
      buf = std::move(istream);
   }

   size_t init(std::vector<char> cbuf) {
      buf.emplace<std::vector<char>>( std::move(cbuf) );
      return std::get<std::vector<char>>(buf).size();
   }
};

namespace detail {

std::vector<char> zlib_decompress(fc::cfile& file, uint64_t compressed_size) {
   if (compressed_size) {
      std::vector<char> compressed(compressed_size);
      file.read(compressed.data(), compressed_size);
      return state_history::zlib_decompress({compressed.data(), compressed_size});
   }
   return {};
}

std::vector<char> zlib_decompress(fc::datastream<const char*>& strm, uint64_t compressed_size) {
   if (compressed_size) {
      return state_history::zlib_decompress({strm.pos(), compressed_size});
   }
   return {};
}

template <typename Log, typename Stream>
uint64_t read_unpacked_entry(Log&& log, Stream& stream, uint64_t payload_size, locked_decompress_stream& result) {
   // result has state_history_log mutex locked

   uint32_t s;
   stream.read((char*)&s, sizeof(s));
   if (s == 1 && payload_size > (s + sizeof(uint32_t))) {
      uint64_t compressed_size = payload_size - sizeof(uint32_t) - sizeof(uint64_t);
      uint64_t decompressed_size;
      stream.read((char*)&decompressed_size, sizeof(decompressed_size));
      result.init(log, stream, compressed_size);
      return decompressed_size;
   } else {
      // Compressed deltas now exceeds 4GB on one of the public chains. This length prefix
      // was intended to support adding additional fields in the future after the
      // packed deltas or packed traces. For now we're going to ignore on read.

      uint64_t compressed_size = payload_size - sizeof(uint32_t);
      return result.init( zlib_decompress(stream, compressed_size) );
   }
}

class state_history_log_data : public chain::log_data_base<state_history_log_data> {
   uint32_t version_;
   bool     is_currently_pruned_;
   uint64_t size_;

 public:
   state_history_log_data() = default;
   explicit state_history_log_data(const fc::path& path) { open(path); }

   void open(const fc::path& path) {
      if (file.is_open())
         file.close();
      file.set_file_path(path);
      file.open("rb");
      uint64_t v           = chain::read_data_at<uint64_t>(file, 0);
      version_             = get_ship_version(v);
      is_currently_pruned_ = is_ship_log_pruned(v);
      file.seek_end(0);
      size_ = file.tellp();
   }

   uint64_t size() const { return size_; }
   uint32_t version() const { return version_; }
   uint32_t first_block_num() { return block_num_at(0); }
   uint32_t first_block_position() const { return 0; }

   bool is_currently_pruned() const { return is_currently_pruned_; }

   uint64_t ro_stream_at(uint64_t pos, locked_decompress_stream& result) {
      uint64_t                    payload_size = payload_size_at(pos);
      file.seek(pos + sizeof(state_history_log_header));
      // fc::datastream<const char*> stream(file.const_data() + pos + sizeof(state_history_log_header), payload_size);
      return read_unpacked_entry(*this, file, payload_size, result);
   }

   uint32_t block_num_at(uint64_t position) {
      return fc::endian_reverse_u32(
          chain::read_data_at<uint32_t>(file, position + offsetof(state_history_log_header, block_id)));
   }

   chain::block_id_type block_id_at(uint64_t position) {
      return chain::read_data_at<chain::block_id_type>(file, position +
                                                      offsetof(state_history_log_header, block_id));
   }

   uint64_t payload_size_at(uint64_t pos) {
      std::string filename = file.get_file_path().generic_string();
      EOS_ASSERT(size() >= pos + sizeof(state_history_log_header), chain::plugin_exception,
                 "corrupt ${name}: invalid entry size at at position ${pos}", ("name", filename)("pos", pos));

      state_history_log_header header = chain::read_data_at<state_history_log_header>(file, pos);

      EOS_ASSERT(is_ship(header.magic) && is_ship_supported_version(header.magic), chain::plugin_exception,
                 "corrupt ${name}: invalid header for entry at position ${pos}", ("name", filename)("pos", pos));

      EOS_ASSERT(size() >= pos + sizeof(state_history_log_header) + header.payload_size, chain::plugin_exception,
                 "corrupt ${name}: invalid payload size for entry at position ${pos}", ("name", filename)("pos", pos));
      return header.payload_size;
   }

   void construct_index(const fc::path& index_file_name) {
      fc::cfile index_file;
      index_file.set_file_path(index_file_name);
      index_file.open("w+b");

      uint64_t pos = 0;
      while (pos < size()) {
         uint64_t payload_size = payload_size_at(pos);
         index_file.write(reinterpret_cast<const char*>(&pos), sizeof(pos));
         pos += (sizeof(state_history_log_header) + payload_size + sizeof(uint64_t));
      }
   }
};

// directly adapt from boost/iostreams/filter/counter.hpp and change the type of chars_ to uint64_t.
class counter  {
public:
    typedef char char_type;
    struct category
        : bio::dual_use,
          bio::filter_tag,
          bio::multichar_tag,
          bio::optimally_buffered_tag
        { };
    explicit counter(uint64_t first_char = 0)
        : chars_(first_char)
        { }
    uint64_t characters() const { return chars_; }
    std::streamsize optimal_buffer_size() const { return 0; }

    template<typename Source>
    std::streamsize read(Source& src, char_type* s, std::streamsize n)
    {
        std::streamsize result = bio::read(src, s, n);
        if (result == -1)
            return -1;
        chars_ += result;
        return result;
    }

    template<typename Sink>
    std::streamsize write(Sink& snk, const char_type* s, std::streamsize n)
    {
        std::streamsize result = bio::write(snk, s, n);
        chars_ += result;
        return result;
    }
private:
    uint64_t chars_;
};

} // namespace detail

class state_history_log {
 private:
   const char* const       name = "";
   state_history_log_config config;

   // provide exclusive access to all data of this object since accessed from the main thread and the ship thread
   mutable std::mutex      _mx;
   fc::cfile               log;
   fc::cfile               index;
   uint32_t                _begin_block = 0;        //always tracks the first block available even after pruning
   uint32_t                _index_begin_block = 0;  //the first block of the file; even after pruning. it's what index 0 in the index file points to
   uint32_t                _end_block   = 0;
   chain::block_id_type    last_block_id;

   using catalog_t = chain::log_catalog<detail::state_history_log_data, chain::log_index<chain::plugin_exception>>;
   catalog_t catalog;

 public:
   friend struct ::state_history_test_fixture;

   state_history_log( const state_history_log&) = delete;

   state_history_log(const char* name, const fc::path& log_dir,
                     state_history_log_config conf = {})
       : name(name)
       , config(std::move(conf)) {

      log.set_file_path(log_dir/(std::string(name) + ".log"));
      index.set_file_path(log_dir/(std::string(name) + ".index"));

      open_log();
      open_index();

      std::visit(eosio::chain::overloaded{
         [](std::monostate&) {},
         [](state_history::prune_config& conf) {
            EOS_ASSERT(conf.prune_blocks, chain::plugin_exception, "state history log prune configuration requires at least one block");
            EOS_ASSERT(__builtin_popcount(conf.prune_threshold) == 1, chain::plugin_exception, "state history prune threshold must be power of 2");
            //switch this over to the mask that will be used
            conf.prune_threshold = ~(conf.prune_threshold-1);
         }, [name, log_dir, this](state_history::partition_config& conf) {
            catalog.open(log_dir, conf.retained_dir, conf.archive_dir, name);
            catalog.max_retained_files = conf.max_retained_files;
            if (_end_block == 0) {
               _begin_block = _end_block = catalog.last_block_num() +1;
            }
         }
      }, config);

      //check for conversions to/from pruned log, as long as log contains something
      if(_begin_block != _end_block) {
         state_history_log_header first_header;
         log.seek(0);
         read_header(first_header);

         auto prune_config = std::get_if<state_history::prune_config>(&config);

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
   }

   ~state_history_log() {
      //nothing to do if log is empty or we aren't pruning
      if(_begin_block == _end_block)
         return;

      auto prune_config = std::get_if<state_history::prune_config>(&config);
      if(!prune_config || !prune_config->vacuum_on_close)
         return;

      const size_t first_data_pos = get_pos(_begin_block);
      const size_t last_data_pos = fc::file_size(log.get_file_path());
      if(last_data_pos - first_data_pos < *prune_config->vacuum_on_close)
         vacuum();
   }

   //        begin     end
   std::pair<uint32_t, uint32_t> block_range() const {
      std::lock_guard g(_mx);
      return { std::min(catalog.first_block_num(), _begin_block), _end_block };
   }

   bool empty() const {
      auto r = block_range();
      return r.first == r.second;
   }

   locked_decompress_stream create_locked_decompress_stream() {
      return locked_decompress_stream{ std::unique_lock<std::mutex>( _mx ) };
   }

   /// @return the decompressed entry size
   uint64_t get_unpacked_entry(uint32_t block_num, locked_decompress_stream& result) {

      // result has mx locked

      auto opt_decompressed_size = catalog.ro_stream_for_block(block_num, result);
      if (opt_decompressed_size)
         return *opt_decompressed_size;

      if (block_num < _begin_block || block_num >= _end_block)
         return 0;

      state_history_log_header header;
      log.seek(get_pos(block_num));
      read_header(header);

      return detail::read_unpacked_entry(*this, log, header.payload_size, result);
   }

   template <typename F>
   void pack_and_write_entry(state_history_log_header header, const chain::block_id_type& prev_id, F&& pack_to) {
      std::lock_guard g(_mx);
      write_entry(header, prev_id, [&, pack_to = std::forward<F>(pack_to)](auto& stream) {
         size_t payload_pos = stream.tellp();

         // In order to conserve memory usage for reading the chain state later, we need to
         // encode the uncompressed data size to the disk so that the reader can send the
         // decompressed data size before decompressing data. Here we use the number
         // 1 indicates the format contains a 64 bits unsigned integer for decompressed data
         // size and then the actually compressed data. The compressed data size can be
         // computed from the payload size in the header minus sizeof(uint32_t) + sizeof(uint64_t).

         uint32_t s = 1;
         stream.write((char*)&s, sizeof(s));
         uint64_t uncompressioned_size = 0;
         stream.skip(sizeof(uncompressioned_size));

         namespace bio = boost::iostreams;

         detail::counter cnt;
         {
            bio::filtering_ostreambuf buf;
            buf.push(boost::ref(cnt));
            buf.push(bio::zlib_compressor());
            buf.push(bio::file_descriptor_sink(stream.fileno(), bio::never_close_handle));
            pack_to(buf);
         }

         // calculate the payload size and rewind back to header to write the payload size
         stream.seek_end(0);
         size_t   end_payload_pos = stream.tellp();
         uint64_t payload_size    = end_payload_pos - payload_pos;
         stream.seek(payload_pos - sizeof(uint64_t));
         stream.write((char*)&payload_size, sizeof(payload_size));

         // write the uncompressed data size
         stream.skip(sizeof(s));
         uncompressioned_size = cnt.characters();
         stream.write((char*)&uncompressioned_size, sizeof(uncompressioned_size));

         // make sure we reset the file position to end_payload_pos to preserve API behavior
         stream.seek(end_payload_pos);
      });
   }

   std::optional<chain::block_id_type> get_block_id(uint32_t block_num) {
      std::lock_guard g(_mx);
      return get_block_id_i(block_num);
   }

#ifdef BOOST_TEST_MODULE
   fc::cfile& get_log_file() { return log;}
#endif

 private:

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

      auto prune_config = std::get_if<state_history::prune_config>(&config);
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

      if (header.payload_size != 0)
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

      auto partition_config = std::get_if<state_history::partition_config>(&config);
      if (partition_config && block_num % partition_config->stride == 0) {
         split_log();
      }
   }

   fc::cfile& get_entry(uint32_t block_num, state_history_log_header& header) {
      EOS_ASSERT(block_num >= _begin_block && block_num < _end_block, chain::plugin_exception,
                 "read non-existing block in ${name}.log", ("name", name));
      log.seek(get_pos(block_num));
      read_header(header);
      return log;
   }

   std::optional<chain::block_id_type> get_block_id_i(uint32_t block_num) {
      auto result = catalog.id_for_block(block_num);
      if (!result) {
         if (block_num >= _begin_block && block_num < _end_block) {
            state_history_log_header header;
            get_entry(block_num, header);
            return header.block_id;
         }
         return {};
      }
      return result;
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
      auto prune_config = std::get_if<state_history::prune_config>(&config);

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
      boost::filesystem::resize_file(log.get_file_path().string(), pos);
      log.flush();

      log.seek_end(-sizeof(pos));
      EOS_ASSERT(get_last_block(), chain::plugin_exception, "recover ${name}.log failed", ("name", name));
   }

   // only called from constructor
   void open_log() {
      log.open(fc::cfile::create_or_update_rw_mode);
      log.seek_end(0);
      uint64_t size = log.tellp();
      log.close();

      log.open(fc::cfile::update_rw_mode);
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
      index.open(fc::cfile::create_or_update_rw_mode);
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
      index.open(fc::cfile::create_or_update_rw_mode);
   }

   uint64_t get_pos(uint32_t block_num) {
      uint64_t pos;
      index.seek((block_num - _index_begin_block) * sizeof(pos));
      index.read((char*)&pos, sizeof(pos));
      return pos;
   }

   void truncate(uint32_t block_num) {
      log.close();
      index.close();

      auto first_block_num     = catalog.empty() ? _begin_block : catalog.first_block_num();
      auto new_begin_block_num = catalog.truncate(block_num, log.get_file_path());

      // notice that catalog.truncate() can replace existing log and index files, so we have to
      // close the files and reopen them again; otherwise we might operate on the obsolete files instead.

      if (new_begin_block_num > 0) {
         _begin_block = new_begin_block_num;
         _index_begin_block = new_begin_block_num;
      }

      uint32_t num_removed;

      if (block_num <= _begin_block) {
         num_removed = _end_block - first_block_num;
         boost::filesystem::resize_file(log.get_file_path().string(), 0);
         boost::filesystem::resize_file(index.get_file_path().string(), 0);
         _begin_block = _end_block = block_num;
      } else {
         num_removed  = _end_block - block_num;

         index.open("rb");
         uint64_t pos = get_pos(block_num);
         index.close();

         auto path = log.get_file_path().string();

         boost::filesystem::resize_file(log.get_file_path().string(), pos);
         boost::filesystem::resize_file(index.get_file_path().string(), (block_num - _index_begin_block) * sizeof(uint64_t));
         _end_block = block_num;
         //this will leave the end of the log with the last block's suffix no matter if the log is operating in pruned
         // mode or not. The assumption is truncate() is always immediately followed up with an append to the log thus
         // restoring the prune trailer if required
      }

      log.open(fc::cfile::update_rw_mode);
      log.seek_end(0);
      index.open(fc::cfile::create_or_update_rw_mode);

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

   void split_log() {

      fc::path log_file_path = log.get_file_path();
      fc::path index_file_path = index.get_file_path();

      fc::datastream<fc::cfile>  new_log_file;
      fc::datastream<fc::cfile> new_index_file;

      fc::path tmp_log_file_path = log_file_path;
      tmp_log_file_path.replace_extension("log.tmp");
      fc::path tmp_index_file_path = index_file_path;
      tmp_index_file_path.replace_extension("index.tmp");

      new_log_file.set_file_path(tmp_log_file_path);
      new_index_file.set_file_path(tmp_index_file_path);

      try {
         new_log_file.open(fc::cfile::truncate_rw_mode);
         new_index_file.open(fc::cfile::truncate_rw_mode);

      } catch (...) {
         wlog("Unable to open new state history log or index file for writing during log spliting, "
              "continue writing to existing block log file\n");
         return;
      }

      index.close();
      log.close();

      catalog.add(_begin_block, _end_block - 1, log.get_file_path().parent_path(), name);

      _begin_block = _end_block;

      using std::swap;
      swap(new_log_file, log);
      swap(new_index_file, index);

      fc::rename(tmp_log_file_path, log_file_path);
      fc::rename(tmp_index_file_path, index_file_path);

      log.set_file_path(log_file_path);
      index.set_file_path(index_file_path);
   }
}; // state_history_log

} // namespace eosio

FC_REFLECT(eosio::state_history_log_header, (magic)(block_id)(payload_size))
