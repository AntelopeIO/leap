#pragma once
#include <eosio/chain/block_state_legacy.hpp>
#include <eosio/chain/block_state.hpp>

namespace eosio::chain {

   template<class bsp>
   struct fork_database_impl;

   using block_branch_t = deque<signed_block_ptr>;

   /**
    * @class fork_database_t
    * @brief manages light-weight state for all potential unconfirmed forks
    *
    * As new blocks are received, they are pushed into the fork database. The fork
    * database tracks the longest chain and the last irreversible block number. All
    * blocks older than the last irreversible block are freed after emitting the
    * irreversible signal.
    *
    * An internal mutex is used to provide thread-safety.
    *
    * fork_database should be used instead of fork_database_t directly as it manages
    * the different supported types.
    */
   template<class BSP>  // either block_state_legacy_ptr or block_state_ptr
   class fork_database_t {
   public:
      static constexpr uint32_t legacy_magic_number = 0x30510FDB;
      static constexpr uint32_t magic_number = 0x4242FDB;

      using BS               = BSP::element_type;
      using BHSP             = BS::bhsp_t;
      using BHS              = BHSP::element_type;
      using bsp_t            = BSP;
      using branch_type      = deque<bsp_t>;
      using branch_type_pair = pair<branch_type, branch_type>;
      
      explicit fork_database_t(uint32_t magic_number = legacy_magic_number);

      void open( const std::filesystem::path& fork_db_file, validator_t& validator );
      void close( const std::filesystem::path& fork_db_file );

      BHSP get_block_header( const block_id_type& id ) const;
      BSP  get_block( const block_id_type& id ) const;

      /**
       *  Purges any existing blocks from the fork database and resets the root block_header_state to the provided value.
       *  The head will also be reset to point to the root.
       */
      void reset_root( const BHS& root_bhs );

      /**
       *  Removes validated flag from all blocks in fork database and resets head to point to the root.
       */
      void rollback_head_to_root();

      /**
       *  Advance root block forward to some other block in the tree.
       */
      void advance_root( const block_id_type& id );

      /**
       *  Add block state to fork database.
       *  Must link to existing block in fork database or the root.
       */
      void add( const BSP& next_block, bool ignore_duplicate = false );

      void remove( const block_id_type& id );

      bool has_root() const;
      BSP  root() const; // undefined if !has_root()
      BSP  head() const;
      BSP  pending_head() const;

      // only accessed by main thread, no mutex protection
      BSP  chain_head;

      /**
       *  Returns the sequence of block states resulting from trimming the branch from the
       *  root block (exclusive) to the block with an id of `h` (inclusive) by removing any
       *  block states corresponding to block numbers greater than `trim_after_block_num`.
       *
       *  The order of the sequence is in descending block number order.
       *  A block with an id of `h` must exist in the fork database otherwise this method will throw an exception.
       */
      branch_type fetch_branch( const block_id_type& h, uint32_t trim_after_block_num = std::numeric_limits<uint32_t>::max() ) const;
      block_branch_t fetch_block_branch( const block_id_type& h, uint32_t trim_after_block_num = std::numeric_limits<uint32_t>::max() ) const;


      /**
       *  Returns the block state with a block number of `block_num` that is on the branch that
       *  contains a block with an id of`h`, or the empty shared pointer if no such block can be found.
       */
      BSP  search_on_branch( const block_id_type& h, uint32_t block_num ) const;

      /**
       *  Given two head blocks, return two branches of the fork graph that
       *  end with a common ancestor (same prior block)
       */
      branch_type_pair fetch_branch_from(const block_id_type& first, const block_id_type& second) const;

      void mark_valid( const BSP& h );

      /**
       * Update block_state_core for best qc strong
       */
      void update_best_qc_strong( const block_id_type& id );

   private:
      unique_ptr<fork_database_impl<BSP>> my;
   };

   using fork_database_legacy_t = fork_database_t<block_state_legacy_ptr>;
   using fork_database_if_t     = fork_database_t<block_state_ptr>;

   /**
    * Provides mechanism for opening the correct type
    * as well as switching from legacy (old dpos) to instant-finality.
    *
    * All methods assert until open() is closed.
    */
   class fork_database {
      const std::filesystem::path data_dir;
      std::atomic<bool> legacy = true;
      std::unique_ptr<fork_database_legacy_t> fork_db_legacy;
      std::unique_ptr<fork_database_if_t>     fork_db_if;
   public:
      explicit fork_database(const std::filesystem::path& data_dir);
      ~fork_database(); // close on destruction

      // not thread safe, expected to be called from main thread before allowing concurrent access
      void open( validator_t& validator );
      void close();

      // expected to be called from main thread, accesses chain_head
      void switch_from_legacy();

      // see fork_database_t::fetch_branch(forkdb->head()->id())
      block_branch_t fetch_branch_from_head() const;

      template <class R, class F>
      R apply(const F& f) {
         if constexpr (std::is_same_v<void, R>) {
            if (legacy) {
               f(*fork_db_legacy);
            } else {
               f(*fork_db_if);
            }
         } else {
            if (legacy) {
               return f(*fork_db_legacy);
            } else {
               return f(*fork_db_if);
            }
         }
      }

      template <class R, class F>
      R apply(const F& f) const {
         if constexpr (std::is_same_v<void, R>) {
            if (legacy) {
               f(*fork_db_legacy);
            } else {
               f(*fork_db_if);
            }
         } else {
            if (legacy) {
               return f(*fork_db_legacy);
            } else {
               return f(*fork_db_if);
            }
         }
      }

      /// Apply for when only need lambda executed when in instant-finality mode
      template <class R, class F>
      R apply_if(const F& f) {
         if constexpr (std::is_same_v<void, R>) {
            if (!legacy) {
               f(*fork_db_if);
            }
         } else {
            if (!legacy) {
               return f(*fork_db_if);
            }
            return {};
         }
      }

      /// Apply for when only need lambda executed when in legacy mode
      template <class R, class F>
      R apply_legacy(const F& f) {
         if constexpr (std::is_same_v<void, R>) {
            if (legacy) {
               f(*fork_db_legacy);
            }
         } else {
            if (legacy) {
               return f(*fork_db_legacy);
            }
            return {};
         }
      }

      /// @param legacy_f the lambda to execute if in legacy mode
      /// @param if_f the lambda to execute if in instant-finality mode
      template <class R, class LegacyF, class IfF>
      R apply(const LegacyF& legacy_f, const IfF& if_f) {
         if constexpr (std::is_same_v<void, R>) {
            if (legacy) {
               legacy_f(*fork_db_legacy);
            } else {
               if_f(*fork_db_if);
            }
         } else {
            if (legacy) {
               return legacy_f(*fork_db_legacy);
            } else {
               return if_f(*fork_db_if);
            }
         }
      }

      // if we ever support more than one version then need to save min/max in fork_database_t
      static constexpr uint32_t min_supported_version = 1;
      static constexpr uint32_t max_supported_version = 1;
   };
} /// eosio::chain
