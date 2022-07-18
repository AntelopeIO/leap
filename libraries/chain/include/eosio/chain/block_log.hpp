#pragma once
#include <fc/filesystem.hpp>
#include <eosio/chain/block.hpp>
#include <eosio/chain/genesis_state.hpp>

namespace eosio { namespace chain {

   namespace detail { class block_log_impl; }

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
    */

   struct block_log_prune_config {
      uint32_t                prune_blocks;                  //number of blocks to prune to when doing a prune
      size_t                  prune_threshold = 4*1024*1024; //(approximately) how many bytes need to be added before a prune is performed
      std::optional<size_t>   vacuum_on_close;               //when set, a vacuum is performed on dtor if log contains less than this many live bytes
   };

   class block_log {
      public:
         block_log(const fc::path& data_dir, std::optional<block_log_prune_config> prune_config);
         block_log(block_log&& other);
         ~block_log();

         void append(const signed_block_ptr& b, const block_id_type& id);
         void append(const signed_block_ptr& b, const block_id_type& id, const std::vector<char>& packed_block);

         void flush();
         void reset( const genesis_state& gs, const signed_block_ptr& genesis_block );
         void reset( const chain_id_type& chain_id, uint32_t first_block_num );
         void remove(); // remove blocks.log and blocks.index

         signed_block_ptr read_block(uint64_t file_pos)const;
         void             read_block_header(block_header& bh, uint64_t file_pos)const;
         signed_block_ptr read_block_by_num(uint32_t block_num)const;
         block_id_type    read_block_id_by_num(uint32_t block_num)const;
         signed_block_ptr read_block_by_id(const block_id_type& id)const {
            return read_block_by_num(block_header::num_from_id(id));
         }

         /**
          * Return offset of block in file, or block_log::npos if it does not exist.
          */
         uint64_t get_block_pos(uint32_t block_num) const;
         signed_block_ptr        read_head()const;
         const signed_block_ptr& head()const;
         const block_id_type&    head_id()const;
         uint32_t                first_block_num() const;

         static const uint64_t npos = std::numeric_limits<uint64_t>::max();

         static const uint32_t min_supported_version;
         static const uint32_t max_supported_version;

         static fc::path repair_log( const fc::path& data_dir, uint32_t truncate_at_block = 0, const char* reversible_block_dir_name="" );

         static std::optional<genesis_state> extract_genesis_state( const fc::path& data_dir );

         static chain_id_type extract_chain_id( const fc::path& data_dir );

         static void construct_index(const fc::path& block_file_name, const fc::path& index_file_name);

         static bool contains_genesis_state(uint32_t version, uint32_t first_block_num);

         static bool contains_chain_id(uint32_t version, uint32_t first_block_num);

         static bool is_supported_version(uint32_t version);

         static bool is_pruned_log(const fc::path& data_dir);

         static bool extract_block_range(const fc::path& block_dir, const fc::path&output_dir, block_num_type& start, block_num_type& end, bool rename_input=false);

   private:
         void open(const fc::path& data_dir);
         void construct_index();

         std::unique_ptr<detail::block_log_impl> my;
   };

//to derive blknum_offset==14 see block_header.hpp and note on disk struct is packed
//   block_timestamp_type timestamp;                  //bytes 0:3
//   account_name         producer;                   //bytes 4:11
//   uint16_t             confirmed;                  //bytes 12:13
//   block_id_type        previous;                   //bytes 14:45, low 4 bytes is big endian block number of previous block

   struct trim_data {            //used by trim_blocklog_front(), trim_blocklog_end(), and smoke_test()
      trim_data(fc::path block_dir);
      ~trim_data();
      uint64_t block_index(uint32_t n) const;
      uint64_t block_pos(uint32_t n);
      fc::path block_file_name, index_file_name;        //full pathname for blocks.log and blocks.index
      uint32_t version = 0;                              //blocklog version
      uint32_t first_block = 0;                          //first block in blocks.log
      uint32_t last_block = 0;                          //last block in blocks.log
      FILE* blk_in = nullptr;                            //C style files for reading blocks.log and blocks.index
      FILE* ind_in = nullptr;                            //C style files for reading blocks.log and blocks.index
      //we use low level file IO because it is distinctly faster than C++ filebuf or iostream
      uint64_t first_block_pos = 0;                      //file position in blocks.log for the first block in the log
      genesis_state gs;
      chain_id_type chain_id;

      static constexpr int blknum_offset{14};            //offset from start of block to 4 byte block number, valid for the only allowed versions
   };
} }
