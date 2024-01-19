#pragma once
#include <eosio/chain/block_state_legacy.hpp>
#include <eosio/chain/block_state.hpp>
#include <boost/signals2/signal.hpp>

namespace eosio::chain {

   using boost::signals2::signal;

   template<class bsp>
   struct fork_database_impl;

   /**
    * @class fork_database_t
    * @brief manages light-weight state for all potential unconfirmed forks
    *
    * As new blocks are received, they are pushed into the fork database. The fork
    * database tracks the longest chain and the last irreversible block number. All
    * blocks older than the last irreversible block are freed after emitting the
    * irreversible signal.
    *
    * Not thread safe, thread safety provided by fork_database below.
    */
   template<class bsp>  // either block_state_legacy_ptr or block_state_ptr
   class fork_database_t {
   public:
      static constexpr uint32_t legacy_magic_number = 0x30510FDB;
      static constexpr uint32_t magic_number = 0x4242FDB;

      using bs               = bsp::element_type;
      using bhsp             = bs::bhsp_t;
      using bhs              = bhsp::element_type;
      using bsp_t            = bsp;
      using branch_type      = deque<bsp>;
      using branch_type_pair = pair<branch_type, branch_type>;
      
      explicit fork_database_t(uint32_t magic_number = legacy_magic_number);

      void open( const std::filesystem::path& fork_db_file, validator_t& validator );
      void close( const std::filesystem::path& fork_db_file );

      bhsp get_block_header( const block_id_type& id ) const;
      bsp  get_block( const block_id_type& id ) const;

      /**
       *  Purges any existing blocks from the fork database and resets the root block_header_state to the provided value.
       *  The head will also be reset to point to the root.
       */
      void reset( const bhs& root_bhs );

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
      void add( const bsp& next_block, bool ignore_duplicate = false );

      void remove( const block_id_type& id );

      bsp  root() const;
      bsp  head() const;
      bsp  pending_head() const;

      // only accessed by main thread, no mutex protection
      bsp  chain_head;

      /**
       *  Returns the sequence of block states resulting from trimming the branch from the
       *  root block (exclusive) to the block with an id of `h` (inclusive) by removing any
       *  block states corresponding to block numbers greater than `trim_after_block_num`.
       *
       *  The order of the sequence is in descending block number order.
       *  A block with an id of `h` must exist in the fork database otherwise this method will throw an exception.
       */
      branch_type fetch_branch( const block_id_type& h, uint32_t trim_after_block_num = std::numeric_limits<uint32_t>::max() ) const;


      /**
       *  Returns the block state with a block number of `block_num` that is on the branch that
       *  contains a block with an id of`h`, or the empty shared pointer if no such block can be found.
       */
      bsp  search_on_branch( const block_id_type& h, uint32_t block_num ) const;

      /**
       *  Given two head blocks, return two branches of the fork graph that
       *  end with a common ancestor (same prior block)
       */
      branch_type_pair fetch_branch_from(const block_id_type& first, const block_id_type& second) const;

      void mark_valid( const bsp& h );

   private:
      unique_ptr<fork_database_impl<bsp>> my;
   };

   using fork_database_legacy_t = fork_database_t<block_state_legacy_ptr>;
   using fork_database_if_t     = fork_database_t<block_state_ptr>;

   /**
    * Provides thread safety on fork_database_t and provide mechanism for opening the correct type
    * as well as switching from legacy to instant-finality.
    */
   class fork_database {
      mutable std::recursive_mutex m;
      const std::filesystem::path data_dir;
      std::variant<fork_database_t<block_state_legacy_ptr>, fork_database_t<block_state_ptr>> v;
   public:
      explicit fork_database(const std::filesystem::path& data_dir);
      ~fork_database(); // close on destruction

      void open( validator_t& validator );

      void switch_from_legacy();

      // see fork_database_t::fetch_branch(forkdb->head()->id())
      std::vector<signed_block_ptr> fetch_branch_from_head();

      template <class R, class F>
      R apply(const F& f) {
         std::lock_guard g(m);
         if constexpr (std::is_same_v<void, R>)
            std::visit([&](auto& forkdb) { f(forkdb); }, v);
         else
            return std::visit([&](auto& forkdb) -> R { return f(forkdb); }, v);
      }

      template <class R, class F>
      R apply(const F& f) const {
         std::lock_guard g(m);
         if constexpr (std::is_same_v<void, R>)
            std::visit([&](const auto& forkdb) { f(forkdb); }, v);
         else
            return std::visit([&](const auto& forkdb) -> R { return f(forkdb); }, v);
      }

      template <class R, class F>
      R apply_if(const F& f) {
         if constexpr (std::is_same_v<void, R>)
            std::visit(overloaded{[&](fork_database_legacy_t&) {},
                                  [&](fork_database_if_t& forkdb) {
                                     std::lock_guard g(m);
                                     f(forkdb);
                                  }}, v);
         else
            return std::visit(overloaded{[&](fork_database_legacy_t&) -> R { return {}; },
                                         [&](fork_database_if_t& forkdb) -> R {
                                            std::lock_guard g(m);
                                            return f(forkdb);
                                         }}, v);
      }

      template <class R, class F>
      R apply_dpos(const F& f) {
         if constexpr (std::is_same_v<void, R>)
            std::visit(overloaded{[&](fork_database_legacy_t& forkdb) {
                                     std::lock_guard g(m);
                                     f(forkdb);
                                  },
                                  [&](fork_database_if_t&) {}}, v);
         else
            return std::visit(overloaded{[&](fork_database_legacy_t& forkdb) -> R {
                                            std::lock_guard g(m);
                                            return f(forkdb);
                                         },
                                         [&](fork_database_if_t&) -> R {
                                            return {};
                                         }}, v);
      }

      // if we every support more than one version then need to save min/max in fork_database_t
      static constexpr uint32_t min_supported_version = 1;
      static constexpr uint32_t max_supported_version = 1;
   };
} /// eosio::chain
