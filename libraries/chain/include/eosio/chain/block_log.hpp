#pragma once
#include <fc/filesystem.hpp>
#include <eosio/chain/block.hpp>
#include <eosio/chain/genesis_state.hpp>
#include <eosio/chain/block_log_config.hpp>

namespace eosio { namespace chain {

   namespace detail { struct block_log_impl; }

   /* The block log is an external append only log of the blocks with a header. Blocks should only
    * be written to the log after they irreverisble as the log is append only. The log is a doubly
    * linked list of blocks. There is a secondary index file of only block positions that enables
    * O(1) random access lookup by block number.
    *
    * +---------+----------------+---------+----------------+-----+------------+-------------------+
    * | Block 1 | Pos of Block 1 | Block 2 | Pos of Block 2 | ... | Head Block | Pos of Head Block |
    * +---------+----------------+---------+----------------+-----+------------+-------------------+
    *
    * +----------------+----------------+-----+-------------------+
    * | Pos of Block 1 | Pos of Block 2 | ... | Pos of Head Block |
    * +----------------+----------------+-----+-------------------+
    *
    * The block log can be walked in order by deserializing a block, skipping 8 bytes, deserializing a
    * block, repeat... The head block of the file can be found by seeking to the position contained
    * in the last 8 bytes the file. The block log can be read backwards by jumping back 8 bytes, following
    * the position, reading the block, jumping back 8 bytes, etc.
    *
    * Blocks can be accessed at random via block number through the index file. Seek to 8 * (block_num - 1)
    * to find the position of the block in the main file.
    *
    * The main file is the only file that needs to persist. The index file can be reconstructed during a
    * linear scan of the main file.
    *
    * An optional "pruned" mode can be activated which stores a 4 byte trailer on the log file indicating
    * how many blocks at the end of the log are valid. Any earlier blocks in the log are assumed destroyed
    * and unreadable due to reclamation for purposes of saving space.
    *
    * Object thread-safe. Not safe to have multiple block_log objects to same data_dir.
    */


   class block_log {
      public:
         explicit block_log(const std::filesystem::path& data_dir, const block_log_config& config = block_log_config{});
         block_log(block_log&& other) noexcept;
         ~block_log();

         void append(const signed_block_ptr& b, const block_id_type& id);
         void append(const signed_block_ptr& b, const block_id_type& id, const std::vector<char>& packed_block);

         void flush();
         void reset( const genesis_state& gs, const signed_block_ptr& genesis_block );
         void reset( const chain_id_type& chain_id, uint32_t first_block_num );

         signed_block_ptr read_block_by_num(uint32_t block_num)const;
         std::optional<signed_block_header> read_block_header_by_num(uint32_t block_num)const;
         block_id_type    read_block_id_by_num(uint32_t block_num)const;

         signed_block_ptr read_block_by_id(const block_id_type& id)const {
            return read_block_by_num(block_header::num_from_id(id));
         }

         /**
          * Return offset of block in file, or block_log::npos if it does not exist.
          */

         signed_block_ptr read_head()const; //use blocklog
         signed_block_ptr head()const;
         block_id_type head_id()const;

         uint32_t                first_block_num() const;

         static const uint64_t npos = std::numeric_limits<uint64_t>::max();

         static const uint32_t min_supported_version;
         static const uint32_t max_supported_version;

         /**
          * All static methods expected to be called on quiescent block log
          */

         static std::filesystem::path repair_log( const std::filesystem::path& data_dir, uint32_t truncate_at_block = 0, const char* reversible_block_dir_name="" );

         static std::optional<genesis_state> extract_genesis_state( const std::filesystem::path& data_dir );

         static chain_id_type extract_chain_id( const std::filesystem::path& data_dir );

         static void construct_index(const std::filesystem::path& block_file_name, const std::filesystem::path& index_file_name);

         static bool contains_genesis_state(uint32_t version, uint32_t first_block_num);

         static bool contains_chain_id(uint32_t version, uint32_t first_block_num);

         static bool is_supported_version(uint32_t version);

         static bool is_pruned_log(const std::filesystem::path& data_dir);

         static void extract_block_range(const std::filesystem::path& block_dir, const std::filesystem::path&output_dir, block_num_type start, block_num_type end);

         static bool trim_blocklog_front(const std::filesystem::path& block_dir, const std::filesystem::path& temp_dir, uint32_t truncate_at_block);
         static int  trim_blocklog_end(const std::filesystem::path& block_dir, uint32_t n);

         // used for unit test to generate older version blocklog
         static void set_initial_version(uint32_t);
         uint32_t    version() const;
         uint64_t get_block_pos(uint32_t block_num) const;

         /**
          * @param n Only test 1 block out of every n blocks. If n is 0, the interval is adjusted so that at most 8 blocks are tested.
          */
         static void smoke_test(const std::filesystem::path& block_dir, uint32_t n);

         static void split_blocklog(const std::filesystem::path& block_dir, const std::filesystem::path& dest_dir, uint32_t stride);
         static void merge_blocklogs(const std::filesystem::path& block_dir, const std::filesystem::path& dest_dir);
   private:
         std::unique_ptr<detail::block_log_impl> my;
   };
} }
