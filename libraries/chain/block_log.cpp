#include <eosio/chain/block_log.hpp>
#include <eosio/chain/exceptions.hpp>
#include <fc/bitutil.hpp>
#include <fc/io/cfile.hpp>
#include <fc/io/raw.hpp>


#define LOG_READ  (std::ios::in | std::ios::binary)
#define LOG_WRITE (std::ios::out | std::ios::binary | std::ios::app)
#define LOG_RW ( std::ios::in | std::ios::out | std::ios::binary )
#define LOG_WRITE_C "ab+"
#define LOG_RW_C "rb+"

#ifndef _WIN32
#define FC_FOPEN(p, m) fopen(p, m)
#else
#define FC_CAT(s1, s2) s1 ## s2
#define FC_PREL(s) FC_CAT(L, s)
#define FC_FOPEN(p, m) _wfopen(p, FC_PREL(m))
#endif

namespace eosio { namespace chain {

   const uint32_t block_log::min_supported_version = 1;

   /**
    * History:
    * Version 1: complete block log from genesis
    * Version 2: adds optional partial block log, cannot be used for replay without snapshot
    *            this is in the form of an first_block_num that is written immediately after the version
    * Version 3: improvement on version 2 to not require the genesis state be provided when not starting
    *            from block 1
    */
   const uint32_t block_log::max_supported_version = 3;

   namespace detail {
      using unique_file = std::unique_ptr<FILE, decltype(&fclose)>;

      class block_log_impl {
         public:
            signed_block_ptr         head;
            block_id_type            head_id;
            fc::cfile                block_file;
            fc::cfile                index_file;
            bool                     open_files = false;
            bool                     genesis_written_to_block_log = false;
            uint32_t                 version = 0;
            uint32_t                 first_block_num = 0;       //the first number available to read
            uint32_t                 index_first_block_num = 0; //the first number in index & the log had it not been pruned
            std::optional<block_log_prune_config> prune_config;
            bool                     not_generate_block_log = false;

            explicit block_log_impl(std::optional<block_log_prune_config> prune_conf) :
              prune_config(prune_conf) {
               if(prune_config) {
                  if (prune_config->prune_blocks == 0 ) {
                     // not to generate blocks.log
                     // disable prune log handling by resetting prune_config
                     prune_config.reset();
                     not_generate_block_log = true;
                  } else {
                     EOS_ASSERT(__builtin_popcount(prune_config->prune_threshold) == 1, block_log_exception, "block log prune threshold must be power of 2");
                     //switch this over to the mask that will be used
                     prune_config->prune_threshold = ~(prune_config->prune_threshold-1);
                  }
               }
            }

            inline void check_open_files() {
               if( !open_files ) {
                  reopen();
               }
            }

            void reopen();

            //close() is called all over the place. Let's make this an explict call to ensure it only is called when
            // we really want: when someone is destroying the blog instance
            void try_exit_vacuum() {
               //for a pruned log that has at least one block, see if we should vacuum it
               if( block_file.is_open() && index_file.is_open() && prune_config && prune_config->vacuum_on_close) {
                  if(!head) {
                     //disregard vacuum_on_close size if there isn't even a block and just do it silently anyways
                     vacuum();
                  }
                  else {
                     const size_t first_data_pos = get_block_pos(first_block_num);
                     block_file.seek_end(-sizeof(uint32_t));
                     const size_t last_data_pos = block_file.tellp();
                     if(last_data_pos - first_data_pos < prune_config->vacuum_on_close) {
                        ilog("Vacuuming pruned block log");
                        vacuum();
                     }
                  }
               }
            }

            void close() {
               if( block_file.is_open() )
                  block_file.close();
               if( index_file.is_open() )
                  index_file.close();
               open_files = false;
            }

            template<typename T>
            void reset( const T& t, const signed_block_ptr& genesis_block, uint32_t first_block_num );

            void remove();

            void write( const genesis_state& gs );

            void write( const chain_id_type& chain_id );

            void flush();

            void append(const signed_block_ptr& b, const block_id_type& id, const std::vector<char>& packed_block);

            void update_head(const signed_block_ptr& b, const std::optional<block_id_type>& id={});

            void prune(const fc::log_level& loglevel);

            void vacuum();

            size_t convert_existing_header_to_vacuumed();

            uint64_t get_block_pos(uint32_t block_num);

            template <typename ChainContext, typename Lambda>
            static std::optional<ChainContext> extract_chain_context( const fc::path& data_dir, Lambda&& lambda );
      };

      constexpr uint32_t pruned_version_flag = 1<<31;

      static bool is_pruned_log_and_mask_version(uint32_t& version) {
         bool ret = version & pruned_version_flag;
         version &= ~pruned_version_flag;
         return ret;
      }

      void detail::block_log_impl::reopen() {
         close();

         // open to create files if they don't exist
         //ilog("Opening block log at ${path}", ("path", my->block_file.generic_string()));
         block_file.open( LOG_WRITE_C );
         index_file.open( LOG_WRITE_C );

         close();

         block_file.open( LOG_RW_C );
         index_file.open( LOG_RW_C );

         open_files = true;
      }

      class reverse_iterator {
      public:
         reverse_iterator();
         // open a block log file and return the total number of blocks in it
         uint32_t open(const fc::path& block_file_name);
         uint64_t previous();
         uint32_t version() const { return _version; }
         uint32_t first_block_num() const { return _first_block_num; }
         static uint32_t      _buf_len;
      private:
         void update_buffer();

         unique_file                    _file;
         uint32_t                       _version                          = 0;
         uint32_t                       _first_block_num                  = 0;
         uint32_t                       _last_block_num                   = 0;
         uint32_t                       _blocks_found                     = 0;
         uint32_t                       _blocks_expected                  = 0;
         std::optional<uint32_t>        _prune_block_limit;
         uint64_t                       _current_position_in_file         = 0;
         uint64_t                       _end_of_buffer_position           = _unset_position;
         uint64_t                       _start_of_buffer_position         = 0;
         std::unique_ptr<char[]>        _buffer_ptr;
         std::string                    _block_file_name;
         constexpr static int64_t       _unset_position                   = -1;
         constexpr static uint64_t      _position_size                    = sizeof(_current_position_in_file);
      };

      constexpr uint64_t buffer_location_to_file_location(uint32_t buffer_location) { return buffer_location << 3; }
      constexpr uint32_t file_location_to_buffer_location(uint32_t file_location) { return file_location >> 3; }

      class index_writer {
      public:
         index_writer(const fc::path& block_index_name, uint32_t blocks_expected);
         void write(uint64_t pos);
      private:
         std::optional<boost::interprocess::file_mapping>  _file;
         std::optional<boost::interprocess::mapped_region> _mapped_file_region;

         uint32_t                                          _blocks_remaining;
      };

      /*
       *  @brief datastream adapter that adapts FILE* for use with fc unpack
       *
       *  This class supports unpack functionality but not pack.
       */
      class fileptr_datastream {
      public:
         explicit fileptr_datastream( FILE* file, const std::string& filename ) : _file(file), _filename(filename) {}

         void skip( size_t s ) {
            auto status = fseek(_file, s, SEEK_CUR);
            EOS_ASSERT( status == 0, block_log_exception,
                        "Could not seek past ${bytes} bytes in Block log file at '${blocks_log}'. Returned status: ${status}",
                        ("bytes", s)("blocks_log", _filename)("status", status) );
         }

         bool read( char* d, size_t s ) {
            size_t result = fread( d, 1, s, _file );
            EOS_ASSERT( result == s, block_log_exception,
                        "only able to read ${act} bytes of the expected ${exp} bytes in file: ${file}",
                        ("act",result)("exp",s)("file", _filename) );
            return true;
         }

         bool get( unsigned char& c ) { return get( *(char*)&c ); }

         bool get( char& c ) { return read(&c, 1); }

      private:
         FILE* const _file;
         const std::string _filename;
      };
   }

   block_log::block_log(const fc::path& data_dir, std::optional<block_log_prune_config> prune_config)
   :my(new detail::block_log_impl(prune_config)) {
      open(data_dir);
   }

   block_log::block_log(block_log&& other) {
      my = std::move(other.my);
   }

   block_log::~block_log() {
      if (my) {
         flush();
         my->try_exit_vacuum();
         my->close();
         my.reset();
      }
   }

   void block_log::open(const fc::path& data_dir) {
      my->close();

      if (!fc::is_directory(data_dir))
         fc::create_directories(data_dir);

      my->block_file.set_file_path( data_dir / "blocks.log" );
      my->index_file.set_file_path( data_dir / "blocks.index" );

      my->reopen();

      /* On startup of the block log, there are several states the log file and the index file can be
       * in relation to each other.
       *
       *                          Block Log
       *                     Exists       Is New
       *                 +------------+------------+
       *          Exists |    Check   |   Delete   |
       *   Index         |    Head    |    Index   |
       *    File         +------------+------------+
       *          Is New |   Replay   |     Do     |
       *                 |    Log     |   Nothing  |
       *                 +------------+------------+
       *
       * Checking the heads of the files has several conditions as well.
       *  - If they are the same, do nothing.
       *  - If the index file head is not in the log file, delete the index and replay.
       *  - If the index file head is in the log, but not up to date, replay from index head.
       */
      auto log_size = fc::file_size( my->block_file.get_file_path() );
      auto index_size = fc::file_size( my->index_file.get_file_path() );

      if (log_size) {
         ilog("Log is nonempty");
         my->block_file.seek( 0 );
         my->version = 0;
         fc::raw::unpack(my->block_file, my->version);
         const bool is_currently_pruned = detail::is_pruned_log_and_mask_version(my->version);
         EOS_ASSERT( my->version > 0, block_log_exception, "Block log was not setup properly" );
         EOS_ASSERT( is_supported_version(my->version), block_log_unsupported_version,
                     "Unsupported version of block log. Block log version is ${version} while code supports version(s) [${min},${max}]",
                     ("version", my->version)("min", block_log::min_supported_version)("max", block_log::max_supported_version) );


         my->genesis_written_to_block_log = true; // Assume it was constructed properly.
         if (my->version > 1){
            my->first_block_num = 0;
            fc::raw::unpack(my->block_file, my->first_block_num);
            EOS_ASSERT(my->first_block_num > 0, block_log_exception, "Block log is malformed, first recorded block number is 0 but must be greater than or equal to 1");
         } else {
            my->first_block_num = 1;
         }
         my->index_first_block_num = my->first_block_num;

         my->update_head(read_head());

         my->block_file.seek_end(0);
         if(is_currently_pruned && my->head) {
            uint32_t prune_log_count;
            my->block_file.skip(-sizeof(uint32_t));
            fc::raw::unpack(my->block_file, prune_log_count);
            my->first_block_num = chain::block_header::num_from_id(my->head_id) - prune_log_count + 1;
            my->block_file.skip(-sizeof(uint32_t));
         }

         if (index_size) {
            ilog("Index is nonempty");
            uint64_t block_pos;
            my->block_file.skip(-sizeof(uint64_t));
            my->block_file.read((char*)&block_pos, sizeof(block_pos));

            uint64_t index_pos;
            my->index_file.seek_end(-sizeof(uint64_t));
            my->index_file.read((char*)&index_pos, sizeof(index_pos));

            if (block_pos < index_pos) {
               ilog("block_pos < index_pos, close and reopen index_file");
               construct_index();
            } else if (block_pos > index_pos) {
               ilog("Index is incomplete");
               construct_index();
            }
         } else {
            ilog("Index is empty");
            construct_index();
         }

         if(!is_currently_pruned && my->prune_config) {
            //need to convert non-pruned log to pruned log. prune any blocks to start with
            my->prune(fc::log_level::info);

            //update version
            my->block_file.seek(0);
            fc::raw::pack(my->block_file, my->version | detail::pruned_version_flag);

            //and write out the trailing block count
            my->block_file.seek_end(0);
            uint32_t num_blocks_in_log = 0;
            if(my->head)
               num_blocks_in_log = chain::block_header::num_from_id(my->head_id) - my->first_block_num + 1;
            fc::raw::pack(my->block_file, num_blocks_in_log);
         }
         else if(is_currently_pruned && !my->prune_config) {
            my->vacuum();
         }
      } else if (index_size) {
         ilog("Index is nonempty, remove and recreate it");
         my->close();
         fc::remove_all( my->index_file.get_file_path() );
         my->reopen();
      }
   }

   void block_log::append(const signed_block_ptr& b, const block_id_type& id) {
      my->append(b, id, fc::raw::pack(*b));
   }

   void block_log::append(const signed_block_ptr& b, const block_id_type& id, const std::vector<char>& packed_block) {
      my->append(b, id, packed_block);
   }

   void detail::block_log_impl::append(const signed_block_ptr& b, const block_id_type& id, const std::vector<char>& packed_block) {
      try {
         EOS_ASSERT( genesis_written_to_block_log, block_log_append_fail, "Cannot append to block log until the genesis is first written" );

         if (not_generate_block_log) {
            update_head(b, id);
            return;
         }

         check_open_files();

         block_file.seek_end(0);
         index_file.seek_end(0);
         //if pruned log, rewind over count trailer if any block is already present
         if(prune_config && head)
            block_file.skip(-sizeof(uint32_t));
         uint64_t pos = block_file.tellp();

         EOS_ASSERT(index_file.tellp() == sizeof(uint64_t) * (b->block_num() - index_first_block_num),
                   block_log_append_fail,
                   "Append to index file occuring at wrong position.",
                   ("position", (uint64_t) index_file.tellp())
                   ("expected", (b->block_num() - index_first_block_num) * sizeof(uint64_t)));
         block_file.write(packed_block.data(), packed_block.size());
         block_file.write((char*)&pos, sizeof(pos));
         const uint64_t end = block_file.tellp();
         index_file.write((char*)&pos, sizeof(pos));

         update_head(b, id);

         if(prune_config) {
            if((pos&prune_config->prune_threshold) != (end&prune_config->prune_threshold))
               prune(fc::log_level::debug);

            const uint32_t num_blocks_in_log = chain::block_header::num_from_id(head_id) - first_block_num + 1;
            fc::raw::pack(block_file, num_blocks_in_log);
         }

         flush();
      }
      FC_LOG_AND_RETHROW()
   }

   void detail::block_log_impl::update_head(const signed_block_ptr& b, const std::optional<block_id_type>& id) {
      head = b;
      if (id) {
         head_id = *id;
      } else {
         if (head) {
            head_id = b->calculate_id();
         } else {
            head_id = {};
         }
      }
   }

   void detail::block_log_impl::prune(const fc::log_level& loglevel) {
      if(!head)
         return;
      const uint32_t head_num = chain::block_header::num_from_id(head_id);
      if(head_num - first_block_num < prune_config->prune_blocks)
         return;

      const uint32_t prune_to_num = head_num - prune_config->prune_blocks + 1;

      static_assert( block_log::max_supported_version == 3, "Code was written to support version 3 format, need to update this code for latest format." );
      const genesis_state gs;
      const size_t max_header_size_v1  = sizeof(uint32_t) + fc::raw::pack_size(gs) + sizeof(uint64_t);
      const size_t max_header_size_v23 = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(chain_id_type) + sizeof(uint64_t);
      const auto max_header_size = std::max(max_header_size_v1, max_header_size_v23);

      block_file.punch_hole(max_header_size, get_block_pos(prune_to_num));

      first_block_num = prune_to_num;
      block_file.flush();

      if(auto l = fc::logger::get(); l.is_enabled(loglevel))
         l.log(fc::log_message(fc::log_context(loglevel, __FILE__, __LINE__, __func__),
                               "blocks.log pruned to blocks ${b}-${e}", fc::mutable_variant_object()("b", first_block_num)("e", head_num)));
   }

   void block_log::flush() {
      if (my->not_generate_block_log) {
         return;
      }
      my->flush();
   }

   void detail::block_log_impl::flush() {
      block_file.flush();
      index_file.flush();
   }

   size_t detail::block_log_impl::convert_existing_header_to_vacuumed() {
      uint32_t old_version;
      uint32_t old_first_block_num;
      const auto totem = block_log::npos;

      block_file.seek(0);
      fc::raw::unpack(block_file, old_version);
      fc::raw::unpack(block_file, old_first_block_num);
      EOS_ASSERT(is_pruned_log_and_mask_version(old_version), block_log_exception, "Trying to vacuumed a non-pruned block log");

      if(block_log::contains_genesis_state(old_version, old_first_block_num)) {
         //we'll always write a v3 log, but need to possibly mutate the genesis_state to a chainid should we have pruned a log starting with a genesis_state
         genesis_state gs;
         auto ds = block_file.create_datastream();
         fc::raw::unpack(ds, gs);

         block_file.seek(0);
         fc::raw::pack(block_file, block_log::max_supported_version);
         fc::raw::pack(block_file, first_block_num);
         if(first_block_num == 1) {
            EOS_ASSERT(old_first_block_num == 1, block_log_exception, "expected an old first blocknum of 1");
            fc::raw::pack(block_file, gs);
         }
         else
            fc::raw::pack(block_file, gs.compute_chain_id());
         fc::raw::pack(block_file, totem);
      }
      else {
         //read in the existing chainid, to parrot back out
         fc::sha256 chainid;
         fc::raw::unpack(block_file, chainid);

         block_file.seek(0);
         fc::raw::pack(block_file, block_log::max_supported_version);
         fc::raw::pack(block_file, first_block_num);
         fc::raw::pack(block_file, chainid);
         fc::raw::pack(block_file, totem);
      }

      return block_file.tellp();
   }

   void detail::block_log_impl::vacuum() {
      //go ahead and write a new valid header now. if the vacuum fails midway, at least this means maybe the
      // block recovery can get through some blocks.
      size_t copy_to_pos = convert_existing_header_to_vacuumed();

      version = block_log::max_supported_version;
      prune_config.reset();

      //if there is no head block though, bail now, otherwise first_block_num won't actually be available
      // and it'll mess this all up. Be sure to still remove the 4 byte trailer though.
      if(!head) {
         block_file.flush();
         fc::resize_file(block_file.get_file_path(), fc::file_size(block_file.get_file_path()) - sizeof(uint32_t));
         return;
      }

      size_t copy_from_pos = get_block_pos(first_block_num);
      block_file.seek_end(-sizeof(uint32_t));
      size_t copy_sz = block_file.tellp() - copy_from_pos;
      const uint32_t num_blocks_in_log = chain::block_header::num_from_id(head_id) - first_block_num + 1;

      const size_t offset_bytes = copy_from_pos - copy_to_pos;
      const size_t offset_blocks = first_block_num - index_first_block_num;

      std::vector<char> buff;
      buff.resize(4*1024*1024);

      auto tick = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
      while(copy_sz) {
         const size_t copy_this_round = std::min(buff.size(), copy_sz);
         block_file.seek(copy_from_pos);
         block_file.read(buff.data(), copy_this_round);
         block_file.punch_hole(copy_to_pos, copy_from_pos+copy_this_round);
         block_file.seek(copy_to_pos);
         block_file.write(buff.data(), copy_this_round);

         copy_from_pos += copy_this_round;
         copy_to_pos += copy_this_round;
         copy_sz -= copy_this_round;

         const auto tock = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
         if(tick < tock - std::chrono::seconds(5)) {
            ilog("Vacuuming pruned block log, ${b} bytes remaining", ("b", copy_sz));
            tick = tock;
         }
      }
      block_file.flush();
      fc::resize_file(block_file.get_file_path(), block_file.tellp());

      index_file.flush();
      {
         boost::interprocess::mapped_region index_mapped(index_file, boost::interprocess::read_write);
         uint64_t* index_ptr = (uint64_t*)index_mapped.get_address();

         for(uint32_t new_block_num = 0; new_block_num < num_blocks_in_log; ++new_block_num) {
            const uint64_t new_pos = index_ptr[new_block_num + offset_blocks] - offset_bytes;
            index_ptr[new_block_num] = new_pos;

            if(new_block_num + 1 != num_blocks_in_log)
               block_file.seek(index_ptr[new_block_num + offset_blocks + 1] - offset_bytes - sizeof(uint64_t));
            else
               block_file.seek_end(-sizeof(uint64_t));
            block_file.write((char*)&new_pos, sizeof(new_pos));
         }
      }
      fc::resize_file(index_file.get_file_path(), num_blocks_in_log*sizeof(uint64_t));

      index_first_block_num = first_block_num;
   }

   template<typename T>
   void detail::block_log_impl::reset( const T& t, const signed_block_ptr& first_block, uint32_t first_bnum ) {
      close();

      fc::remove_all( block_file.get_file_path() );
      fc::remove_all( index_file.get_file_path() );

      reopen();

      version = 0; // version of 0 is invalid; it indicates that subsequent data was not properly written to the block log
      index_first_block_num = first_block_num = first_bnum;

      block_file.seek_end(0);
      block_file.write((char*)&version, sizeof(version));
      block_file.write((char*)&first_block_num, sizeof(first_block_num));

      write(t);
      genesis_written_to_block_log = true;

      // append a totem to indicate the division between blocks and header
      auto totem = block_log::npos;
      block_file.write((char*)&totem, sizeof(totem));

      if (first_block) {
         append(first_block, first_block->calculate_id(), fc::raw::pack(*first_block));
      } else {
         head.reset();
         head_id = {};
         if(prune_config)
            fc::raw::pack(block_file, (uint32_t)0);
      }

      auto pos = block_file.tellp();

      static_assert( block_log::max_supported_version > 0, "a version number of zero is not supported" );

      // going back to write correct version to indicate that all block log header data writes completed successfully
      version = block_log::max_supported_version;
      if(prune_config)
         version |= pruned_version_flag;
      block_file.seek( 0 );
      block_file.write( (char*)&version, sizeof(version) );
      block_file.seek( pos );
      flush();
   }

   void block_log::reset( const genesis_state& gs, const signed_block_ptr& first_block ) {
      // At startup, OK to be called in no blocks.log mode from controller.cpp
      my->reset(gs, first_block, 1);
   }

   void block_log::reset( const chain_id_type& chain_id, uint32_t first_block_num ) {
      // At startup, OK to be called in no blocks.log mode from controller.cpp
      EOS_ASSERT( first_block_num > 1, block_log_exception,
                  "Block log version ${ver} needs to be created with a genesis state if starting from block number 1." );
      my->reset(chain_id, signed_block_ptr(), first_block_num);
   }

   void detail::block_log_impl::remove() {
      close();

      fc::remove( block_file.get_file_path() );
      fc::remove( index_file.get_file_path() );

      ilog("block log ${l}, block index ${i} removed", ("l", block_file.get_file_path()) ("i", index_file.get_file_path()));
   }

   void block_log::remove() {
      my->remove();
   }

   void detail::block_log_impl::write( const genesis_state& gs ) {
      auto data = fc::raw::pack(gs);
      block_file.write(data.data(), data.size());
   }

   void detail::block_log_impl::write( const chain_id_type& chain_id ) {
      block_file << chain_id;
   }

   signed_block_ptr block_log::read_block(uint64_t pos)const {
      if (my->not_generate_block_log) {
         return nullptr;
      }

      my->check_open_files();

      my->block_file.seek(pos);
      signed_block_ptr result = std::make_shared<signed_block>();
      auto ds = my->block_file.create_datastream();
      fc::raw::unpack(ds, *result);
      return result;
   }

   void block_log::read_block_header(block_header& bh, uint64_t pos)const {
      if (my->not_generate_block_log) {
         return;
      }

      my->check_open_files();

      my->block_file.seek(pos);
      auto ds = my->block_file.create_datastream();
      fc::raw::unpack(ds, bh);
   }

   signed_block_ptr block_log::read_block_by_num(uint32_t block_num)const {
      try {
         signed_block_ptr b;

         if (my->not_generate_block_log) {
            // No blocks exist. Avoid cascading failures if going further.
            return b;
         }

         uint64_t pos = get_block_pos(block_num);
         if (pos != npos) {
            b = read_block(pos);
            EOS_ASSERT(b->block_num() == block_num, reversible_blocks_exception,
                      "Wrong block was read from block log.", ("returned", b->block_num())("expected", block_num));
         }
         return b;
      } FC_LOG_AND_RETHROW()
   }

   block_id_type block_log::read_block_id_by_num(uint32_t block_num)const {
      try {
         if (my->not_generate_block_log) {
            return {};
         }
         uint64_t pos = get_block_pos(block_num);
         if (pos != npos) {
            block_header bh;
            read_block_header(bh, pos);
            EOS_ASSERT(bh.block_num() == block_num, reversible_blocks_exception,
                       "Wrong block header was read from block log.", ("returned", bh.block_num())("expected", block_num));
            return bh.calculate_id();
         }
         return {};
      } FC_LOG_AND_RETHROW()
   }

   uint64_t detail::block_log_impl::get_block_pos(uint32_t block_num) {
      check_open_files();
      if (!(head && block_num <= block_header::num_from_id(head_id) && block_num >= first_block_num))
         return block_log::npos;
      index_file.seek(sizeof(uint64_t) * (block_num - index_first_block_num));
      uint64_t pos;
      index_file.read((char*)&pos, sizeof(pos));
      return pos;
   }

   uint64_t block_log::get_block_pos(uint32_t block_num) const {
      if (my->not_generate_block_log) {
         return block_log::npos;
      }
      return my->get_block_pos(block_num);
   }

   signed_block_ptr block_log::read_head()const {
      if (my->not_generate_block_log) {
         return {};
      }

      my->check_open_files();

      uint64_t pos;

      // Check that the file is not empty
      my->block_file.seek_end(0);
      if (my->block_file.tellp() <= sizeof(pos))
         return {};

      //figure out if this is a pruned log or not. we can't just look at the configuration since
      // read_head() is called early on, and this isn't hot enough to warrant a member bool to track it
      my->block_file.seek(0);
      uint32_t current_version;
      fc::raw::unpack(my->block_file, current_version);
      const bool is_currently_pruned = detail::is_pruned_log_and_mask_version(current_version);

      my->block_file.seek_end(0);
      if(is_currently_pruned)
         my->block_file.skip(-sizeof(uint32_t)); //skip the trailer containing block count
      my->block_file.skip(-sizeof(pos));
      fc::raw::unpack(my->block_file, pos);
      if (pos != npos)
         return read_block(pos);
      return {};
   }

   const signed_block_ptr& block_log::head()const {
      return my->head;
   }

   const block_id_type&    block_log::head_id()const {
      return my->head_id;
   }

   uint32_t block_log::first_block_num() const {
      return my->first_block_num;
   }

   void block_log::construct_index() {
      if (my->not_generate_block_log) {
         ilog("Not need to construct index in no blocks.log mode (block-log-retain-blocks=0)");
         return;
      }

      ilog("Reconstructing Block Log Index...");
      my->close();

      fc::remove_all( my->index_file.get_file_path() );

      my->reopen();


      my->close();

      block_log::construct_index(my->block_file.get_file_path(), my->index_file.get_file_path());

      my->reopen();
   } // construct_index

   void block_log::construct_index(const fc::path& block_file_name, const fc::path& index_file_name) {
      detail::reverse_iterator block_log_iter;

      ilog("Will read existing blocks.log file ${file}", ("file", block_file_name.generic_string()));
      ilog("Will write new blocks.index file ${file}", ("file", index_file_name.generic_string()));

      //for a pruned log, this will still return blocks in the count that have been removed. that's okay and desirable
      const uint32_t num_blocks = block_log_iter.open(block_file_name);

      ilog("block log version= ${version}", ("version", block_log_iter.version()));

      if (num_blocks == 0) {
         return;
      }

      ilog("first block= ${first}         last block= ${last}",
           ("first", block_log_iter.first_block_num())("last", (block_log_iter.first_block_num() + num_blocks)));

      detail::index_writer index(index_file_name, num_blocks);
      uint64_t position;
      while ((position = block_log_iter.previous()) != npos) {
         index.write(position);
      }
   }

   fc::path block_log::repair_log(const fc::path& data_dir, uint32_t truncate_at_block, const char* reversible_block_dir_name) {
      ilog("Recovering Block Log...");
      EOS_ASSERT( fc::is_directory(data_dir) && fc::is_regular_file(data_dir / "blocks.log"), block_log_not_found,
                 "Block log not found in '${blocks_dir}'", ("blocks_dir", data_dir)          );

      auto now = fc::time_point::now();

      auto blocks_dir = fc::canonical( data_dir );
      if( blocks_dir.filename().generic_string() == "." ) {
         blocks_dir = blocks_dir.parent_path();
      }
      auto backup_dir = blocks_dir.parent_path();
      auto blocks_dir_name = blocks_dir.filename();
      EOS_ASSERT( blocks_dir_name.generic_string() != ".", block_log_exception, "Invalid path to blocks directory" );
      backup_dir = backup_dir / blocks_dir_name.generic_string().append("-").append( now );

      EOS_ASSERT( !fc::exists(backup_dir), block_log_backup_dir_exist,
                 "Cannot move existing blocks directory to already existing directory '${new_blocks_dir}'",
                 ("new_blocks_dir", backup_dir) );

      fc::rename( blocks_dir, backup_dir );
      ilog( "Moved existing blocks directory to backup location: '${new_blocks_dir}'", ("new_blocks_dir", backup_dir) );

      if (strlen(reversible_block_dir_name) && fc::is_directory(blocks_dir/reversible_block_dir_name)) {
         fc::rename(blocks_dir/ reversible_block_dir_name, backup_dir/ reversible_block_dir_name);
      }

      fc::create_directories(blocks_dir);
      auto block_log_path = blocks_dir / "blocks.log";

      ilog( "Reconstructing '${new_block_log}' from backed up block log", ("new_block_log", block_log_path) );

      std::fstream  old_block_stream;
      std::fstream  new_block_stream;

      old_block_stream.open( (backup_dir / "blocks.log").generic_string().c_str(), LOG_READ );
      new_block_stream.open( block_log_path.generic_string().c_str(), LOG_WRITE );

      old_block_stream.seekg( 0, std::ios::end );
      uint64_t end_pos = old_block_stream.tellg();
      old_block_stream.seekg( 0 );

      uint32_t version = 0;
      old_block_stream.read( (char*)&version, sizeof(version) );
      EOS_ASSERT( version > 0, block_log_exception, "Block log was not setup properly" );
      EOS_ASSERT( is_supported_version(version), block_log_unsupported_version,
                 "Unsupported version of block log. Block log version is ${version} while code supports version(s) [${min},${max}]",
                 ("version", version)("min", block_log::min_supported_version)("max", block_log::max_supported_version) );

      new_block_stream.write( (char*)&version, sizeof(version) );

      uint32_t first_block_num = 1;
      if (version != 1) {
         old_block_stream.read ( (char*)&first_block_num, sizeof(first_block_num) );

         // this assert is only here since repair_log is only used for --hard-replay-blockchain, which removes any
         // existing state, if another API needs to use it, this can be removed and the check for the first block's
         // previous block id will need to accommodate this.
         EOS_ASSERT( first_block_num == 1, block_log_exception,
                     "Block log ${file} must contain a genesis state and start at block number 1.  This block log "
                     "starts at block number ${first_block_num}.",
                     ("file", (backup_dir / "blocks.log").generic_string())("first_block_num", first_block_num));

         new_block_stream.write( (char*)&first_block_num, sizeof(first_block_num) );
      }

      if (contains_genesis_state(version, first_block_num)) {
         genesis_state gs;
         fc::raw::unpack(old_block_stream, gs);

         auto data = fc::raw::pack( gs );
         new_block_stream.write( data.data(), data.size() );
      }
      else if (contains_chain_id(version, first_block_num)) {
         chain_id_type chain_id;
         old_block_stream >> chain_id;

         new_block_stream << chain_id;
      }
      else {
         EOS_THROW( block_log_exception,
                    "Block log ${file} is not supported. version: ${ver} and first_block_num: ${fbn} does not contain "
                    "a genesis_state nor a chain_id.",
                    ("file", (backup_dir / "blocks.log").generic_string())("ver", version)("fbn", first_block_num));
      }

      if (version != 1) {
         auto expected_totem = npos;
         std::decay_t<decltype(npos)> actual_totem;
         old_block_stream.read ( (char*)&actual_totem, sizeof(actual_totem) );

         EOS_ASSERT(actual_totem == expected_totem, block_log_exception,
                    "Expected separator between block log header and blocks was not found( expected: ${e}, actual: ${a} )",
                    ("e", fc::to_hex((char*)&expected_totem, sizeof(expected_totem) ))("a", fc::to_hex((char*)&actual_totem, sizeof(actual_totem) )));

         new_block_stream.write( (char*)&actual_totem, sizeof(actual_totem) );
      }

      std::exception_ptr          except_ptr;
      vector<char>                incomplete_block_data;
      std::optional<signed_block> bad_block;
      uint32_t                    block_num = 0;

      block_id_type previous;

      uint64_t pos = old_block_stream.tellg();
      while( pos < end_pos ) {
         signed_block tmp;

         try {
            fc::raw::unpack(old_block_stream, tmp);
         } catch( ... ) {
            except_ptr = std::current_exception();
            incomplete_block_data.resize( end_pos - pos );
            old_block_stream.read( incomplete_block_data.data(), incomplete_block_data.size() );
            break;
         }

         auto id = tmp.calculate_id();
         if( block_header::num_from_id(previous) + 1 != block_header::num_from_id(id) ) {
            elog( "Block ${num} (${id}) skips blocks. Previous block in block log is block ${prev_num} (${previous})",
                  ("num", block_header::num_from_id(id))("id", id)
                  ("prev_num", block_header::num_from_id(previous))("previous", previous) );
         }
         if( previous != tmp.previous ) {
            elog( "Block ${num} (${id}) does not link back to previous block. "
                  "Expected previous: ${expected}. Actual previous: ${actual}.",
                  ("num", block_header::num_from_id(id))("id", id)("expected", previous)("actual", tmp.previous) );
         }
         previous = id;

         uint64_t tmp_pos = std::numeric_limits<uint64_t>::max();
         if( (static_cast<uint64_t>(old_block_stream.tellg()) + sizeof(pos)) <= end_pos ) {
            old_block_stream.read( reinterpret_cast<char*>(&tmp_pos), sizeof(tmp_pos) );
         }
         if( pos != tmp_pos ) {
            bad_block.emplace(std::move(tmp));
            break;
         }

         auto data = fc::raw::pack(tmp);
         new_block_stream.write( data.data(), data.size() );
         new_block_stream.write( reinterpret_cast<char*>(&pos), sizeof(pos) );
         block_num = tmp.block_num();
         if(block_num % 1000 == 0)
            ilog( "Recovered block ${num}", ("num", block_num) );
         pos = new_block_stream.tellp();
         if( block_num == truncate_at_block )
            break;
      }

      if( bad_block ) {
         ilog( "Recovered only up to block number ${num}. Last block in block log was not properly committed:\n${last_block}",
               ("num", block_num)("last_block", *bad_block) );
      } else if( except_ptr ) {
         std::string error_msg;

         try {
            std::rethrow_exception(except_ptr);
         } catch( const fc::exception& e ) {
            error_msg = e.what();
         } catch( const std::exception& e ) {
            error_msg = e.what();
         } catch( ... ) {
            error_msg = "unrecognized exception";
         }

         ilog( "Recovered only up to block number ${num}. "
               "The block ${next_num} could not be deserialized from the block log due to error:\n${error_msg}",
               ("num", block_num)("next_num", block_num+1)("error_msg", error_msg) );

         auto tail_path = blocks_dir / std::string("blocks-bad-tail-").append( now ).append(".log");
         if( !fc::exists(tail_path) && incomplete_block_data.size() > 0 ) {
            std::fstream tail_stream;
            tail_stream.open( tail_path.generic_string().c_str(), LOG_WRITE );
            tail_stream.write( incomplete_block_data.data(), incomplete_block_data.size() );

            ilog( "Data at tail end of block log which should contain the (incomplete) serialization of block ${num} "
                  "has been written out to '${tail_path}'.",
                  ("num", block_num+1)("tail_path", tail_path) );
         }
      } else if( block_num == truncate_at_block && pos < end_pos ) {
         ilog( "Stopped recovery of block log early at specified block number: ${stop}.", ("stop", truncate_at_block) );
      } else {
         ilog( "Existing block log was undamaged. Recovered all irreversible blocks up to block number ${num}.", ("num", block_num) );
      }

      return backup_dir;
   }

   template <typename ChainContext, typename Lambda>
   std::optional<ChainContext> detail::block_log_impl::extract_chain_context( const fc::path& data_dir, Lambda&& lambda ) {
      EOS_ASSERT( fc::is_directory(data_dir) && fc::is_regular_file(data_dir / "blocks.log"), block_log_not_found,
                  "Block log not found in '${blocks_dir}'", ("blocks_dir", data_dir)          );

      std::fstream  block_stream;
      block_stream.open( (data_dir / "blocks.log").generic_string().c_str(), LOG_READ );

      uint32_t version = 0;
      block_stream.read( (char*)&version, sizeof(version) );
      is_pruned_log_and_mask_version(version);
      EOS_ASSERT( version >= block_log::min_supported_version && version <= block_log::max_supported_version, block_log_unsupported_version,
                  "Unsupported version of block log. Block log version is ${version} while code supports version(s) [${min},${max}]",
                  ("version", version)("min", block_log::min_supported_version)("max", block_log::max_supported_version) );

      uint32_t first_block_num = 1;
      if (version != 1) {
         block_stream.read ( (char*)&first_block_num, sizeof(first_block_num) );
      }

      return lambda(block_stream, version, first_block_num);
   }

   std::optional<genesis_state> block_log::extract_genesis_state( const fc::path& data_dir ) {
      return detail::block_log_impl::extract_chain_context<genesis_state>(data_dir, [](std::fstream& block_stream, uint32_t version, uint32_t first_block_num ) -> std::optional<genesis_state> {
         if (contains_genesis_state(version, first_block_num)) {
            genesis_state gs;
            fc::raw::unpack(block_stream, gs);
            return gs;
         }

         // current versions only have a genesis state if they start with block number 1
         return std::optional<genesis_state>();
      });
   }

   chain_id_type block_log::extract_chain_id( const fc::path& data_dir ) {
      return *(detail::block_log_impl::extract_chain_context<chain_id_type>(data_dir, [](std::fstream& block_stream, uint32_t version, uint32_t first_block_num ) -> std::optional<chain_id_type> {
         // supported versions either contain a genesis state, or else the chain id only
         if (contains_genesis_state(version, first_block_num)) {
            genesis_state gs;
            fc::raw::unpack(block_stream, gs);
            return gs.compute_chain_id();
         }
         EOS_ASSERT( contains_chain_id(version, first_block_num), block_log_exception,
                     "Block log error! version: ${version} with first_block_num: ${num} does not contain a "
                     "chain id or genesis state, so the chain id cannot be determined.",
                     ("version", version)("num", first_block_num) );
         chain_id_type chain_id;
         fc::raw::unpack(block_stream, chain_id);
         return chain_id;
      }));
   }

   bool block_log::is_pruned_log(const fc::path& data_dir) {
      uint32_t version = 0;
      try {
         fc::cfile log_file;
         log_file.set_file_path(data_dir / "blocks.log");
         log_file.open("rb");
         fc::raw::unpack(log_file, version);
      }
      catch(...) {
         return false;
      }
      return detail::is_pruned_log_and_mask_version(version);
   }

   detail::reverse_iterator::reverse_iterator()
   : _file(nullptr, &fclose)
   , _buffer_ptr(std::make_unique<char[]>(_buf_len)) {
   }

   uint32_t detail::reverse_iterator::_buf_len = 1U << 24;

   uint32_t detail::reverse_iterator::open(const fc::path& block_file_name) {
      _block_file_name = block_file_name.generic_string();
      _file.reset( FC_FOPEN(_block_file_name.c_str(), "r"));
      EOS_ASSERT( _file, block_log_exception, "Could not open Block log file at '${blocks_log}'", ("blocks_log", _block_file_name) );
      _end_of_buffer_position = _unset_position;

      //read block log to see if version 1 or 2 and get first blocknum (implicit 1 if version 1)
      _version = 0;
      auto size = fread((char*)&_version, sizeof(_version), 1, _file.get());
      EOS_ASSERT( size == 1, block_log_exception, "Block log file at '${blocks_log}' could not be read.", ("file", _block_file_name) );
      const bool is_prune_log = is_pruned_log_and_mask_version(_version);
      EOS_ASSERT( block_log::is_supported_version(_version), block_log_unsupported_version,
                  "block log version ${v} is not supported", ("v", _version));
      if (_version == 1) {
         _first_block_num = 1;
      }
      else {
         size = fread((char*)&_first_block_num, sizeof(_first_block_num), 1, _file.get());
         EOS_ASSERT( size == 1, block_log_exception, "Block log file at '${blocks_log}' not formatted consistently with version ${v}.", ("file", _block_file_name)("v", _version) );
      }

      auto status = fseek(_file.get(), 0, SEEK_END);
      EOS_ASSERT( status == 0, block_log_exception, "Could not open Block log file at '${blocks_log}'. Returned status: ${status}", ("blocks_log", _block_file_name)("status", status) );

      auto eof_position_in_file = ftell(_file.get());
      EOS_ASSERT( eof_position_in_file > 0, block_log_exception, "Block log file at '${blocks_log}' could not be read.", ("blocks_log", _block_file_name) );

      if(is_prune_log) {
         fseek(_file.get(), -sizeof(uint32_t), SEEK_CUR);
         uint32_t prune_count;
         size = fread((char*)&prune_count, sizeof(prune_count), 1, _file.get());
         EOS_ASSERT( size == 1, block_log_exception, "Block log file at '${blocks_log}' not formatted consistently with pruned version ${v}.", ("file", _block_file_name)("v", _version) );
         _prune_block_limit = prune_count;
         eof_position_in_file -= sizeof(prune_count);
      }

      _current_position_in_file = eof_position_in_file - _position_size;

      update_buffer();

      _blocks_found = 0;
      char* buf = _buffer_ptr.get();
      const uint32_t index_of_pos = _current_position_in_file - _start_of_buffer_position;
      const uint64_t block_pos = *reinterpret_cast<uint64_t*>(buf + index_of_pos);

      if (block_pos == block_log::npos) {
         return 0;
      }

      uint32_t bnum = 0;
      if (block_pos >= _start_of_buffer_position) {
         const uint32_t index_of_block = block_pos - _start_of_buffer_position;
         bnum = *reinterpret_cast<uint32_t*>(buf + index_of_block + trim_data::blknum_offset);  //block number of previous block (is big endian)
      }
      else {
         const auto blknum_offset_pos = block_pos + trim_data::blknum_offset;
         auto status = fseek(_file.get(), blknum_offset_pos, SEEK_SET);
         EOS_ASSERT( status == 0, block_log_exception, "Could not seek in '${blocks_log}' to position: ${pos}. Returned status: ${status}", ("blocks_log", _block_file_name)("pos", blknum_offset_pos)("status", status) );
         auto size = fread((void*)&bnum, sizeof(bnum), 1, _file.get());
         EOS_ASSERT( size == 1, block_log_exception, "Could not read in '${blocks_log}' at position: ${pos}", ("blocks_log", _block_file_name)("pos", blknum_offset_pos) );
      }
      _last_block_num = fc::endian_reverse_u32(bnum) + 1;                     //convert from big endian to little endian and add 1
      _blocks_expected = _last_block_num - _first_block_num + 1;
      return _blocks_expected;
   }

   uint64_t detail::reverse_iterator::previous() {
      EOS_ASSERT( _current_position_in_file != block_log::npos,
                  block_log_exception,
                  "Block log file at '${blocks_log}' first block already returned by former call to previous(), it is no longer valid to call this function.", ("blocks_log", _block_file_name) );

      if ((_version == 1 && _blocks_found == _blocks_expected) || (_prune_block_limit && _blocks_found == *_prune_block_limit)) {
         _current_position_in_file = block_log::npos;
         return _current_position_in_file;
      }
	 
      if (_start_of_buffer_position > _current_position_in_file) {
         update_buffer();
      }

      char* buf = _buffer_ptr.get();
      auto offset = _current_position_in_file - _start_of_buffer_position;
      uint64_t block_location_in_file = *reinterpret_cast<uint64_t*>(buf + offset);

      ++_blocks_found;
      if (block_location_in_file == block_log::npos) {
         _current_position_in_file = block_location_in_file;
         EOS_ASSERT( _blocks_found != _blocks_expected,
                    block_log_exception,
                    "Block log file at '${blocks_log}' formatting indicated last block: ${last_block_num}, first block: ${first_block_num}, but found ${num} blocks",
                    ("blocks_log", _block_file_name)("last_block_num", _last_block_num)("first_block_num", _first_block_num)("num", _blocks_found) );
      }
      else {
         const uint64_t previous_position_in_file = _current_position_in_file;
         _current_position_in_file = block_location_in_file - _position_size;
         EOS_ASSERT( _current_position_in_file < previous_position_in_file,
                     block_log_exception,
                     "Block log file at '${blocks_log}' formatting is incorrect, indicates position later location in file: ${pos}, which was retrieved at: ${orig_pos}.",
                     ("blocks_log", _block_file_name)("pos", _current_position_in_file)("orig_pos", previous_position_in_file) );
      }

      return block_location_in_file;
   }

   void detail::reverse_iterator::update_buffer() {
      EOS_ASSERT( _current_position_in_file != block_log::npos, block_log_exception, "Block log file not setup properly" );

      // since we need to read in a new section, just need to ensure the next position is at the very end of the buffer
      _end_of_buffer_position = _current_position_in_file + _position_size;
      if (_end_of_buffer_position < _buf_len) {
         _start_of_buffer_position = 0;
      }
      else {
         _start_of_buffer_position = _end_of_buffer_position - _buf_len;
      }

      auto status = fseek(_file.get(), _start_of_buffer_position, SEEK_SET);
      EOS_ASSERT( status == 0, block_log_exception, "Could not seek in '${blocks_log}' to position: ${pos}. Returned status: ${status}", ("blocks_log", _block_file_name)("pos", _start_of_buffer_position)("status", status) );
      char* buf = _buffer_ptr.get();
      auto size = fread((void*)buf, (_end_of_buffer_position - _start_of_buffer_position), 1, _file.get());//read tail of blocks.log file into buf
      EOS_ASSERT( size == 1, block_log_exception, "blocks.log read fails" );
   }

   detail::index_writer::index_writer(const fc::path& block_index_name, uint32_t blocks_expected)
   : _blocks_remaining(blocks_expected) {
      const size_t file_sz = blocks_expected*sizeof(uint64_t);

      fc::cfile file;
      file.set_file_path(block_index_name);
      file.open(LOG_WRITE_C);
      file.close();

      fc::resize_file(block_index_name, file_sz);
      _file.emplace(block_index_name.string().c_str(), boost::interprocess::read_write);
      _mapped_file_region.emplace(*_file, boost::interprocess::read_write);
   }

   void detail::index_writer::write(uint64_t pos) {
      EOS_ASSERT( _blocks_remaining, block_log_exception, "No more blocks were expected for the block log index" );

      char* base = (char*)_mapped_file_region->get_address();
      base += --_blocks_remaining*sizeof(uint64_t);
      memcpy(base, &pos, sizeof(pos));

      if ((_blocks_remaining & 0xfffff) == 0)
         ilog("blocks remaining to index: ${blocks_left}      position in log file: ${pos}", ("blocks_left", _blocks_remaining)("pos",pos));
   }

   bool block_log::contains_genesis_state(uint32_t version, uint32_t first_block_num) {
      return version <= 2 || first_block_num == 1;
   }

   bool block_log::contains_chain_id(uint32_t version, uint32_t first_block_num) {
      return version >= 3 && first_block_num > 1;
   }

   bool block_log::is_supported_version(uint32_t version) {
      return std::clamp(version, min_supported_version, max_supported_version) == version;
   }

   namespace {
      template <typename T>
      T read_buffer(const char* buf) {
         T result;
         memcpy(&result, buf, sizeof(T));
         return result;
      }

      template <typename T>
      void write_buffer(char* des, const T* src) {
          memcpy(des, src, sizeof(T));
      }
   }

   bool block_log::extract_block_range(const fc::path& block_dir, const fc::path&output_dir, block_num_type& start, block_num_type& end, bool rename_input) {
      EOS_ASSERT( block_dir != output_dir, block_log_exception, "block_dir and output_dir need to be different directories" );
      trim_data original_block_log(block_dir);
      if(start < original_block_log.first_block) {
         dlog("Requested start block of ${start} is less than the first available block ${n}; adjusting to ${n}", ("start", start)("n", original_block_log.first_block));
         start = original_block_log.first_block;
      }
      if(end > original_block_log.last_block) {
         dlog("Requested end block of ${end} is greater than the last available block ${n}; adjusting to ${n}", ("end", end)("n", original_block_log.last_block));
         end = original_block_log.last_block;
      }
      ilog("In directory ${output} will create new block log with range ${start}-${end}",
           ("output", output_dir.generic_string())("start", start)("end", end));
      // ****** create the new block log file and write out the header for the file
      fc::create_directories(output_dir);
      fc::path new_block_filename = output_dir / "blocks.log";
      if (fc::remove(new_block_filename)) {
         ilog("Removing existing blocks.log file");
      }
      fc::cfile new_block_file;
      new_block_file.set_file_path(new_block_filename);
      // need to open as write since the file doesn't already exist, then reopen
      // with read/write to allow writing the file in any order
      new_block_file.open( LOG_WRITE_C );
      new_block_file.close();
      new_block_file.open( LOG_RW_C );

      static_assert( block_log::max_supported_version == 3,
                     "Code was written to support version 3 format, need to update this code for latest format." );
      uint32_t version = block_log::max_supported_version;
      new_block_file.seek(0);
      new_block_file.write((char*)&version, sizeof(version));
      new_block_file.write((char*)&start, sizeof(start));

      if (start > 1) {
         new_block_file << original_block_log.chain_id;
      } else {
         fc::raw::pack(new_block_file, original_block_log.gs);
      }

      // append a totem to indicate the division between blocks and header
      auto totem = block_log::npos;
      new_block_file.write((char*)&totem, sizeof(totem));
      // ****** end of new block log header

      const auto new_block_file_first_block_pos = new_block_file.tellp();

      // copy over remainder of block log to new block log
      auto buffer =  std::make_unique<char[]>(detail::reverse_iterator::_buf_len);
      char* buf =  buffer.get();

      // offset bytes to shift from old blocklog position to new blocklog position
      const uint64_t original_file_start_block_pos = original_block_log.block_pos(start);
      const uint64_t pos_delta = original_file_start_block_pos - new_block_file_first_block_pos;
      uint64_t original_file_end_block_pos;
      if (end == original_block_log.last_block) {
         auto status = fseek(original_block_log.blk_in, 0, SEEK_END);
         EOS_ASSERT( status == 0, block_log_exception, "blocks.log seek failed" );
         original_file_end_block_pos = ftell(original_block_log.blk_in);
      } else {
         original_file_end_block_pos = original_block_log.block_pos(end+1);
         auto status = fseek(original_block_log.blk_in, original_file_end_block_pos, SEEK_SET);
         EOS_ASSERT( status == 0, block_log_exception, "blocks.log seek failed" );
      }

      // all bytes to copy to the new blocklog
      const uint64_t to_write = original_file_end_block_pos - original_file_start_block_pos;

      // start with the last block's position stored at the end of the block
      const auto pos_size = sizeof(uint64_t);
      uint64_t original_pos = original_file_end_block_pos - pos_size;

      const auto num_blocks = end - start + 1;

      fc::path new_index_filename = output_dir / "blocks.index";
      detail::index_writer index(new_index_filename, num_blocks);

      uint64_t read_size = 0;
      uint64_t write_size = 0;
      for(uint64_t to_write_remaining = to_write; to_write_remaining > 0; to_write_remaining -= write_size) {
         read_size = to_write_remaining;
         if (read_size > detail::reverse_iterator::_buf_len) {
            read_size = detail::reverse_iterator::_buf_len;
         }

         // read in the previous contiguous memory into the read buffer
         const auto start_of_blk_buffer_pos = original_file_start_block_pos + to_write_remaining - read_size;
         auto status = fseek(original_block_log.blk_in, start_of_blk_buffer_pos, SEEK_SET);
         EOS_ASSERT( status == 0, block_log_exception, "original blocks.log seek failed" );
         const auto num_read = fread(buf, read_size, 1, original_block_log.blk_in);
         EOS_ASSERT( num_read == 1, block_log_exception, "original blocks.log read failed" );

         // walk this memory section to adjust block position to match the adjusted location
         // of the block start and store in the new index file
         write_size = read_size;
         while(original_pos >= start_of_blk_buffer_pos) {
            const auto buffer_index = original_pos - start_of_blk_buffer_pos;
            uint64_t pos_content = read_buffer<uint64_t>(buf + buffer_index);

            if ( (pos_content - start_of_blk_buffer_pos) > 0 && (pos_content - start_of_blk_buffer_pos) < pos_size ) {
               // avoid the whole 8 bytes that contains a blk pos being split by the buffer
               write_size = read_size - (pos_content - start_of_blk_buffer_pos);
            }
            const auto start_of_this_block = pos_content;
            pos_content = start_of_this_block - pos_delta;
            write_buffer<uint64_t>(buf + buffer_index, &pos_content);
            index.write(pos_content);
            original_pos = start_of_this_block - pos_size;
         }
         new_block_file.seek(new_block_file_first_block_pos + to_write_remaining - write_size);
         uint64_t offset = read_size - write_size;
         new_block_file.write(buf+offset, write_size);
      }

      fclose(original_block_log.blk_in);
      original_block_log.blk_in = nullptr;
      new_block_file.flush();
      new_block_file.close();

      if (rename_input) {
         fc::path old_log = output_dir / "old.log";
         rename(original_block_log.block_file_name, old_log);
         rename(new_block_filename, original_block_log.block_file_name);
         fc::path old_ind = output_dir / "old.index";
         rename(original_block_log.index_file_name, old_ind);
         rename(new_index_filename, original_block_log.index_file_name);
      }

      return true;
   }

   trim_data::trim_data(fc::path block_dir) {

      // code should follow logic in block_log::repair_log

      using namespace std;
      block_file_name = block_dir / "blocks.log";
      index_file_name = block_dir / "blocks.index";
      blk_in = FC_FOPEN(block_file_name.generic_string().c_str(), "rb");
      EOS_ASSERT( blk_in != nullptr, block_log_not_found, "cannot read file ${file}", ("file",block_file_name.string()) );
      ind_in = FC_FOPEN(index_file_name.generic_string().c_str(), "rb");
      EOS_ASSERT( ind_in != nullptr, block_log_not_found, "cannot read file ${file}", ("file",index_file_name.string()) );
      auto size = fread((void*)&version,sizeof(version), 1, blk_in);
      EOS_ASSERT( size == 1, block_log_unsupported_version, "invalid format for file ${file}", ("file",block_file_name.string()));
      ilog("block log version= ${version}",("version",version));
      bool is_pruned = detail::is_pruned_log_and_mask_version(version);
      EOS_ASSERT( !is_pruned, block_log_unsupported_version, "Block log is currently in pruned format, it must be vacuumed before doing this operation");
      EOS_ASSERT( block_log::is_supported_version(version), block_log_unsupported_version, "block log version ${v} is not supported", ("v",version));

      detail::fileptr_datastream ds(blk_in, block_file_name.string());
      if (version == 1) {
         first_block = 1;
         genesis_state gs;
         fc::raw::unpack(ds, gs);
         chain_id = gs.compute_chain_id();
      }
      else {
         size = fread((void *) &first_block, sizeof(first_block), 1, blk_in);
         EOS_ASSERT(size == 1, block_log_exception, "invalid format for file ${file}",
                    ("file", block_file_name.string()));
         if (block_log::contains_genesis_state(version, first_block)) {
            fc::raw::unpack(ds, gs);
            chain_id = gs.compute_chain_id();
         }
         else if (block_log::contains_chain_id(version, first_block)) {
            ds >> chain_id;
         }
         else {
            EOS_THROW( block_log_exception,
                       "Block log ${file} is not supported. version: ${ver} and first_block: ${first_block} does not contain "
                       "a genesis_state nor a chain_id.",
                       ("file", block_file_name.string())("ver", version)("first_block", first_block));
         }

         const auto expected_totem = block_log::npos;
         std::decay_t<decltype(block_log::npos)> actual_totem;
         size = fread ( (char*)&actual_totem, sizeof(actual_totem), 1, blk_in);

         EOS_ASSERT(size == 1, block_log_exception,
                    "Expected to read ${size} bytes, but did not read any bytes", ("size", sizeof(actual_totem)));
         EOS_ASSERT(actual_totem == expected_totem, block_log_exception,
                    "Expected separator between block log header and blocks was not found( expected: ${e}, actual: ${a} )",
                    ("e", fc::to_hex((char*)&expected_totem, sizeof(expected_totem) ))("a", fc::to_hex((char*)&actual_totem, sizeof(actual_totem) )));
      }

      const uint64_t start_of_blocks = ftell(blk_in);

      const auto status = fseek(ind_in, 0, SEEK_END);                //get length of blocks.index (gives number of blocks)
      EOS_ASSERT( status == 0, block_log_exception, "cannot seek to ${file} end", ("file", index_file_name.string()) );
      const uint64_t file_end = ftell(ind_in);                //get length of blocks.index (gives number of blocks)
      last_block = first_block + file_end/sizeof(uint64_t) - 1;

      first_block_pos = block_pos(first_block);
      EOS_ASSERT(start_of_blocks == first_block_pos, block_log_exception,
                 "Block log ${file} was determined to have its first block at ${determined}, but the block index "
                 "indicates the first block is at ${index}",
                 ("file", block_file_name.string())("determined", start_of_blocks)("index",first_block_pos));
      ilog("first block= ${first}",("first",first_block));
      ilog("last block= ${last}",("last",last_block));
   }

   trim_data::~trim_data() {
      if (blk_in != nullptr)
         fclose(blk_in);
      if (ind_in != nullptr)
         fclose(ind_in);
   }

   uint64_t trim_data::block_index(uint32_t n) const {
      using namespace std;
      EOS_ASSERT( first_block <= n, block_log_exception,
                  "cannot seek in ${file} to block number ${b}, block number ${first} is the first block",
                  ("file", index_file_name.string())("b",n)("first",first_block) );
      EOS_ASSERT( n <= last_block, block_log_exception,
                  "cannot seek in ${file} to block number ${b}, block number ${last} is the last block",
                  ("file", index_file_name.string())("b",n)("last",last_block) );
      return sizeof(uint64_t) * (n - first_block);
   }

   uint64_t trim_data::block_pos(uint32_t n) {
      using namespace std;
      // can indicate the location of the block after the last block
      if (n == last_block + 1) {
         return ftell(blk_in);
      }
      const uint64_t index_pos = block_index(n);
      auto status = fseek(ind_in, index_pos, SEEK_SET);
      EOS_ASSERT( status == 0, block_log_exception, "cannot seek to ${file} ${pos} from beginning of file for block ${b}", ("file", index_file_name.string())("pos", index_pos)("b",n) );
      const uint64_t pos = ftell(ind_in);
      EOS_ASSERT( pos == index_pos, block_log_exception, "cannot seek to ${file} entry for block ${b}", ("file", index_file_name.string())("b",n) );
      uint64_t block_n_pos;
      auto size = fread((void*)&block_n_pos, sizeof(block_n_pos), 1, ind_in);                   //filepos of block n
      EOS_ASSERT( size == 1, block_log_exception, "cannot read ${file} entry for block ${b}", ("file", index_file_name.string())("b",n) );

      //read blocks.log and verify block number n is found at the determined file position
      const auto calc_blknum_pos = block_n_pos + blknum_offset;
      status = fseek(blk_in, calc_blknum_pos, SEEK_SET);
      EOS_ASSERT( status == 0, block_log_exception, "cannot seek to ${file} ${pos} from beginning of file", ("file", block_file_name.string())("pos", calc_blknum_pos) );
      const uint64_t block_offset_pos = ftell(blk_in);
      EOS_ASSERT( block_offset_pos == calc_blknum_pos, block_log_exception, "cannot seek to ${file} ${pos} from beginning of file", ("file", block_file_name.string())("pos", calc_blknum_pos) );
      uint32_t prior_blknum;
      size = fread((void*)&prior_blknum, sizeof(prior_blknum), 1, blk_in);     //read bigendian block number of prior block
      EOS_ASSERT( size == 1, block_log_exception, "cannot read prior block");
      const uint32_t bnum = fc::endian_reverse_u32(prior_blknum) + 1;          //convert to little endian, add 1 since prior block
      EOS_ASSERT( bnum == n, block_log_exception,
                  "At position ${pos} in ${file} expected to find ${exp_bnum} but found ${act_bnum}",
                  ("pos",block_offset_pos)("file", block_file_name.string())("exp_bnum",n)("act_bnum",bnum) );

      return block_n_pos;
   }

   } } /// eosio::chain

// used only for unit test to adjust the buffer length
void block_log_set_buff_len(uint64_t len){
    eosio::chain::detail::reverse_iterator::_buf_len = len;
}
