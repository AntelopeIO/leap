#include <eosio/chain/fork_database.hpp>
#include <eosio/chain/exceptions.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <fc/io/fstream.hpp>
#include <fc/io/cfile.hpp>
#include <fstream>
#include <mutex>

namespace eosio::chain {
   using boost::multi_index_container;
   using namespace boost::multi_index;

   /**
    * History:
    * Version 1: initial version of the new refactored fork database portable format
    */

   // call while holding fork database lock
   struct block_state_accessor {
      static bool is_valid(const block_state& bs) {
         return bs.is_valid();
      }
      static void set_valid(block_state& bs, bool v) {
         bs.validated = v;
      }
      static uint32_t last_final_block_num(const block_state& bs) {
         return bs.updated_core.last_final_block_num;
      }
      static uint32_t final_on_strong_qc_block_num(const block_state& bs) {
         return bs.updated_core.final_on_strong_qc_block_num;
      }
      static uint32_t lastest_qc_claim_block_num(const block_state& bs) {
         return bs.updated_core.latest_qc_claim_block_num;
      }
      static bool qc_claim_update_needed(block_state& bs, const qc_claim_t& most_recent_ancestor_with_qc) {
         return bs.most_recent_ancestor_with_qc < most_recent_ancestor_with_qc;
      }
      static bool qc_claim_update_needed(block_state& bs, const block_state& prev) {
         return bs.most_recent_ancestor_with_qc < prev.most_recent_ancestor_with_qc;
      }
      static void update_best_qc(block_state& bs, const qc_claim_t& most_recent_ancestor_with_qc) {
         assert(bs.most_recent_ancestor_with_qc < most_recent_ancestor_with_qc);
         bs.updated_core = bs.core.next_metadata(most_recent_ancestor_with_qc);
         bs.most_recent_ancestor_with_qc = most_recent_ancestor_with_qc;
      }
      static void update_best_qc(block_state& bs, const block_state& prev) {
         assert(bs.most_recent_ancestor_with_qc < prev.most_recent_ancestor_with_qc);
         update_best_qc(bs, prev.most_recent_ancestor_with_qc);
      }

      // thread safe
      static uint32_t block_height(const block_state& bs) {
         return bs.timestamp().slot;
      }
   };

   struct block_state_legacy_accessor {
      static bool is_valid(const block_state_legacy& bs) { return bs.is_valid(); }
      static void set_valid(block_state_legacy& bs, bool v) { bs.validated = v; }
      static uint32_t last_final_block_num(const block_state_legacy& bs) { return bs.irreversible_blocknum(); }
      static uint32_t final_on_strong_qc_block_num(const block_state_legacy& bs) { return bs.irreversible_blocknum(); }
      static uint32_t lastest_qc_claim_block_num(const block_state_legacy& bs) { return bs.irreversible_blocknum(); }
      static bool qc_claim_update_needed(block_state_legacy&, const qc_claim_t&) { return false; }
      static bool qc_claim_update_needed(block_state_legacy&, const block_state_legacy&) { return false; }
      static void update_best_qc(block_state_legacy&, const qc_claim_t&) { }
      static void update_best_qc(block_state_legacy&, const block_state_legacy&) { }

      // thread safe
      static uint32_t block_height(const block_state_legacy& bs) { return bs.block_num(); }
   };

   struct by_block_id;
   struct by_best_branch;
   struct by_prev;

   template<class BS, class BSP>
   void log_bs(const char* desc, fork_database_impl<BSP>& fork_db, const BS& lhs) {
      using BSA = BS::fork_db_block_state_accessor;
      dlog( "fork_db ${f}, ${d} ${bn}, last_final_block_num ${lfbn}, final_on_strong_qc_block_num ${fsbn}, lastest_qc_claim_block_num ${lbn}, block_height ${bh}, id ${id}",
             ("f", (uint64_t)(&fork_db))("d", desc)("bn", lhs.block_num())("lfbn", BSA::last_final_block_num(lhs))("fsbn", BSA::final_on_strong_qc_block_num(lhs))("lbn", BSA::lastest_qc_claim_block_num(lhs))("bh", BSA::block_height(lhs))("id", lhs.id()) );
   }

   // match comparison of by_best_branch
   template<class BS>
   bool first_preferred( const BS& lhs, const BS& rhs ) {
      using BSA = BS::fork_db_block_state_accessor;
      return std::make_tuple(BSA::last_final_block_num(lhs), BSA::lastest_qc_claim_block_num(lhs), BSA::block_height(lhs)) >
             std::make_tuple(BSA::last_final_block_num(rhs), BSA::lastest_qc_claim_block_num(rhs), BSA::block_height(rhs));
   }

   template<class BSP>  // either [block_state_legacy_ptr, block_state_ptr], same with block_header_state_ptr
   struct fork_database_impl {
      using BS               = BSP::element_type;
      using BSAccessor       = BS::fork_db_block_state_accessor;
      using BHSP             = BS::bhsp_t;
      using BHS              = BHSP::element_type;

      using fork_db_t        = fork_database_t<BSP>;
      using branch_type      = fork_db_t::branch_type;
      using full_branch_type = fork_db_t::full_branch_type;
      using branch_type_pair = fork_db_t::branch_type_pair;

      using fork_multi_index_type = multi_index_container<
         BSP,
         indexed_by<
            hashed_unique<tag<by_block_id>, BOOST_MULTI_INDEX_CONST_MEM_FUN(BS, const block_id_type&, id), std::hash<block_id_type>>,
            ordered_non_unique<tag<by_prev>, const_mem_fun<BS, const block_id_type&, &BS::previous>>,
            ordered_unique<tag<by_best_branch>,
               composite_key<BS,
                  global_fun<const BS&,             bool,     &BSAccessor::is_valid>,
                  global_fun<const BS&,             uint32_t, &BSAccessor::last_final_block_num>,
                  global_fun<const BS&,             uint32_t, &BSAccessor::lastest_qc_claim_block_num>,
                  global_fun<const BS&,             uint32_t, &BSAccessor::block_height>,
                  const_mem_fun<BS,     const block_id_type&, &BS::id>
               >,
               composite_key_compare<std::greater<bool>,
                                     std::greater<uint32_t>, std::greater<uint32_t>, std::greater<uint32_t>,
                                     sha256_less
               >
            >
         >
      >;

      std::mutex             mtx;
      fork_multi_index_type  index;
      BSP                    root; // Only uses the block_header_state portion of block_state
      BSP                    head;
      const uint32_t         magic_number;

      explicit fork_database_impl(uint32_t magic_number) : magic_number(magic_number) {}

      void             open_impl( const std::filesystem::path& fork_db_file, validator_t& validator );
      void             close_impl( const std::filesystem::path& fork_db_file );
      void             add_impl( const BSP& n, mark_valid_t mark_valid, ignore_duplicate_t ignore_duplicate, bool validate, validator_t& validator );

      BHSP             get_block_header_impl( const block_id_type& id ) const;
      BSP              get_block_impl( const block_id_type& id ) const;
      void             reset_root_impl( const BHS& root_bhs );
      void             rollback_head_to_root_impl();
      void             advance_root_impl( const block_id_type& id );
      void             remove_impl( const block_id_type& id );
      branch_type      fetch_branch_impl( const block_id_type& h, uint32_t trim_after_block_num ) const;
      block_branch_t   fetch_block_branch_impl( const block_id_type& h, uint32_t trim_after_block_num ) const;
      full_branch_type fetch_full_branch_impl(const block_id_type& h) const;
      BSP              search_on_branch_impl( const block_id_type& h, uint32_t block_num ) const;
      void             mark_valid_impl( const BSP& h );
      void             update_best_qc_impl( const block_id_type& id, const qc_claim_t& most_recent_ancestor_with_qc );
      branch_type_pair fetch_branch_from_impl( const block_id_type& first, const block_id_type& second ) const;

   };

   template<class BSP>
   fork_database_t<BSP>::fork_database_t(uint32_t magic_number)
      :my( new fork_database_impl<BSP>(magic_number) )
   {}


   template<class BSP>
   void fork_database_t<BSP>::open( const std::filesystem::path& fork_db_file, validator_t& validator ) {
      std::lock_guard g( my->mtx );
      my->open_impl( fork_db_file, validator );
   }

   template<class BSP>
   void fork_database_impl<BSP>::open_impl( const std::filesystem::path& fork_db_file, validator_t& validator ) {
      if( std::filesystem::exists( fork_db_file ) ) {
         try {
            string content;
            fc::read_file_contents( fork_db_file, content );

            fc::datastream<const char*> ds( content.data(), content.size() );

            // validate totem
            uint32_t totem = 0;
            fc::raw::unpack( ds, totem );
            EOS_ASSERT( totem == magic_number, fork_database_exception,
                        "Fork database file '${filename}' has unexpected magic number: ${actual_totem}. Expected ${expected_totem}",
                        ("filename", fork_db_file)("actual_totem", totem)("expected_totem", magic_number)
            );

            // validate version
            uint32_t version = 0;
            fc::raw::unpack( ds, version );
            EOS_ASSERT( version >= fork_database::min_supported_version && version <= fork_database::max_supported_version,
                        fork_database_exception,
                       "Unsupported version of fork database file '${filename}'. "
                       "Fork database version is ${version} while code supports version(s) [${min},${max}]",
                       ("filename", fork_db_file)
                       ("version", version)
                       ("min", fork_database::min_supported_version)
                       ("max", fork_database::max_supported_version)
            );

            BHS state;
            fc::raw::unpack( ds, state );
            reset_root_impl( state );

            unsigned_int size; fc::raw::unpack( ds, size );
            for( uint32_t i = 0, n = size.value; i < n; ++i ) {
               BS s;
               fc::raw::unpack( ds, s );
               // do not populate transaction_metadatas, they will be created as needed in apply_block with appropriate key recovery
               s.header_exts = s.block->validate_and_extract_header_extensions();
               add_impl( std::make_shared<BS>( std::move( s ) ), mark_valid_t::no, ignore_duplicate_t::no, true, validator );
            }
            block_id_type head_id;
            fc::raw::unpack( ds, head_id );

            if( root->id() == head_id ) {
               head = root;
            } else {
               head = get_block_impl( head_id );
               EOS_ASSERT( head, fork_database_exception,
                           "could not find head while reconstructing fork database from file; '${filename}' is likely corrupted",
                           ("filename", fork_db_file) );
            }

            auto candidate = index.template get<by_best_branch>().begin();
            if( candidate == index.template get<by_best_branch>().end() || !BSAccessor::is_valid(**candidate) ) {
               EOS_ASSERT( head->id() == root->id(), fork_database_exception,
                           "head not set to root despite no better option available; '${filename}' is likely corrupted",
                           ("filename", fork_db_file) );
            } else {
               EOS_ASSERT( !first_preferred( **candidate, *head ), fork_database_exception,
                           "head not set to best available option available; '${filename}' is likely corrupted",
                           ("filename", fork_db_file) );
            }
         } FC_CAPTURE_AND_RETHROW( (fork_db_file) )

         std::filesystem::remove( fork_db_file );
      }
   }

   template<class BSP>
   void fork_database_t<BSP>::close(const std::filesystem::path& fork_db_file) {
      std::lock_guard g( my->mtx );
      my->close_impl(fork_db_file);
   }

   template<class BSP>
   void fork_database_impl<BSP>::close_impl(const std::filesystem::path& fork_db_file) {
      if( !root ) {
         if( index.size() > 0 ) {
            elog( "fork_database is in a bad state when closing; not writing out '${filename}'",
                  ("filename", fork_db_file) );
         }
         return;
      }

      std::ofstream out( fork_db_file.generic_string().c_str(), std::ios::out | std::ios::binary | std::ofstream::trunc );
      fc::raw::pack( out, magic_number );
      fc::raw::pack( out, fork_database::max_supported_version ); // write out current version which is always max_supported_version
      fc::raw::pack( out, *static_cast<BHS*>(&*root) );
      uint32_t num_blocks_in_fork_db = index.size();
      fc::raw::pack( out, unsigned_int{num_blocks_in_fork_db} );

      const auto& indx = index.template get<by_best_branch>();

      auto unvalidated_itr = indx.rbegin();
      auto unvalidated_end = boost::make_reverse_iterator( indx.lower_bound( false ) );

      auto validated_itr = unvalidated_end;
      auto validated_end = indx.rend();

      for(  bool unvalidated_remaining = (unvalidated_itr != unvalidated_end),
                 validated_remaining   = (validated_itr != validated_end);

            unvalidated_remaining || validated_remaining;

            unvalidated_remaining = (unvalidated_itr != unvalidated_end),
            validated_remaining   = (validated_itr != validated_end)
         )
      {
         auto itr = (validated_remaining ? validated_itr : unvalidated_itr);

         if( unvalidated_remaining && validated_remaining ) {
            if( first_preferred( **validated_itr, **unvalidated_itr ) ) {
               itr = unvalidated_itr;
               ++unvalidated_itr;
            } else {
               ++validated_itr;
            }
         } else if( unvalidated_remaining ) {
            ++unvalidated_itr;
         } else {
            ++validated_itr;
         }

         fc::raw::pack( out, *(*itr) );
      }

      if( head ) {
         fc::raw::pack( out, head->id() );
      } else {
         elog( "head not set in fork database; '${filename}' will be corrupted",
               ("filename", fork_db_file) );
      }

      index.clear();
   }

   template<class BSP>
   void fork_database_t<BSP>::reset_root( const BHS& root_bhs ) {
      std::lock_guard g( my->mtx );
      my->reset_root_impl(root_bhs);
   }

   template<class BSP>
   void fork_database_impl<BSP>::reset_root_impl( const BHS& root_bhs ) {
      index.clear();
      root = std::make_shared<BS>();
      static_cast<BHS&>(*root) = root_bhs;
      BSAccessor::set_valid(*root, true);
      head = root;
   }

   template<class BSP>
   void fork_database_t<BSP>::rollback_head_to_root() {
      std::lock_guard g( my->mtx );
      my->rollback_head_to_root_impl();
   }

   template<class BSP>
   void fork_database_impl<BSP>::rollback_head_to_root_impl() {
      auto& by_id_idx = index.template get<by_block_id>();
      auto itr = by_id_idx.begin();
      while (itr != by_id_idx.end()) {
         by_id_idx.modify( itr, []( auto& i ) {
            BSAccessor::set_valid(*i, false);
         } );
         ++itr;
      }
      head = root;
   }

   template<class BSP>
   void fork_database_t<BSP>::advance_root( const block_id_type& id ) {
      std::lock_guard g( my->mtx );
      my->advance_root_impl( id );
   }

   template<class BSP>
   void fork_database_impl<BSP>::advance_root_impl( const block_id_type& id ) {
      EOS_ASSERT( root, fork_database_exception, "root not yet set" );

      auto new_root = get_block_impl( id );
      EOS_ASSERT( new_root, fork_database_exception,
                  "cannot advance root to a block that does not exist in the fork database" );
      EOS_ASSERT( BSAccessor::is_valid(*new_root), fork_database_exception,
                  "cannot advance root to a block that has not yet been validated" );


      deque<block_id_type> blocks_to_remove;
      for( auto b = new_root; b; ) {
         blocks_to_remove.emplace_back( b->previous() );
         b = get_block_impl( blocks_to_remove.back() );
         EOS_ASSERT( b || blocks_to_remove.back() == root->id(), fork_database_exception,
                     "invariant violation: orphaned branch was present in forked database" );
      }

      // The new root block should be erased from the fork database index individually rather than with the remove method,
      // because we do not want the blocks branching off of it to be removed from the fork database.
      index.erase( index.find( id ) );

      // The other blocks to be removed are removed using the remove method so that orphaned branches do not remain in the fork database.
      for( const auto& block_id : blocks_to_remove ) {
         remove_impl( block_id );
      }

      // Even though fork database no longer needs block or trxs when a block state becomes a root of the tree,
      // avoid mutating the block state at all, for example clearing the block shared pointer, because other
      // parts of the code which run asynchronously may later expect it remain unmodified.

      root = new_root;
   }

   template<class BSP>
   fork_database_t<BSP>::BHSP fork_database_t<BSP>::get_block_header( const block_id_type& id ) const {
      std::lock_guard g( my->mtx );
      return my->get_block_header_impl( id );
   }

   template<class BSP>
   fork_database_impl<BSP>::BHSP fork_database_impl<BSP>::get_block_header_impl( const block_id_type& id ) const {
      if( root->id() == id ) {
         return root;
      }

      auto itr = index.find( id );
      if( itr != index.end() )
         return *itr;

      return BHSP();
   }

   template <class BSP>
   void fork_database_impl<BSP>::add_impl(const BSP& n, mark_valid_t mark_valid, ignore_duplicate_t ignore_duplicate, bool validate, validator_t& validator) {
      EOS_ASSERT( root, fork_database_exception, "root not yet set" );
      EOS_ASSERT( n, fork_database_exception, "attempt to add null block state" );

      auto prev_bh = get_block_header_impl( n->previous() );
      EOS_ASSERT( prev_bh, unlinkable_block_exception,
                  "unlinkable block", ("id", n->id())("previous", n->previous()) );

      if (validate) {
         try {
            const auto& exts = n->header_exts;

            if( exts.count(protocol_feature_activation::extension_id()) > 0 ) {
               const auto& new_protocol_features = std::get<protocol_feature_activation>(exts.lower_bound(protocol_feature_activation::extension_id())->second).protocol_features;
               validator( n->header.timestamp, prev_bh->activated_protocol_features->protocol_features, new_protocol_features );
            }
         }
         EOS_RETHROW_EXCEPTIONS( fork_database_exception, "serialized fork database is incompatible with configured protocol features" )
      }

      if (mark_valid == mark_valid_t::yes)
         BSAccessor::set_valid(*n, true);

      auto inserted = index.insert(n);
      if( !inserted.second ) {
         if( ignore_duplicate == ignore_duplicate_t::yes ) return;
         EOS_THROW( fork_database_exception, "duplicate block added", ("id", n->id()) );
      }

      // ancestor might have been updated since block_state was created
      if (auto prev = index.find(n->previous()); prev != index.end()) {
         if (BSAccessor::qc_claim_update_needed(*n, **prev)) {
            auto& by_id_idx = index.template get<by_block_id>();
            auto itr = by_id_idx.find(n->id());
            by_id_idx.modify( itr, [&]( auto& i ) {
               BSAccessor::update_best_qc(*i, **prev);
            } );
         }
      }

      auto candidate = index.template get<by_best_branch>().begin();
      if( BSAccessor::is_valid(**candidate) ) {
         head = *candidate;
      }
   }

   template<class BSP>
   void fork_database_t<BSP>::add( const BSP& n, mark_valid_t mark_valid, ignore_duplicate_t ignore_duplicate ) {
      std::lock_guard g( my->mtx );
      my->add_impl( n, mark_valid, ignore_duplicate, false,
                    []( block_timestamp_type timestamp,
                        const flat_set<digest_type>& cur_features,
                        const vector<digest_type>& new_features )
                    {}
      );
   }

   template<class BSP>
   bool fork_database_t<BSP>::has_root() const {
      return !!my->root;
   }

   template<class BSP>
   BSP fork_database_t<BSP>::root() const {
      std::lock_guard g( my->mtx );
      return my->root;
   }

   template<class BSP>
   BSP fork_database_t<BSP>::head() const {
      std::lock_guard g( my->mtx );
      return my->head;
   }

   template<class BSP>
   BSP fork_database_t<BSP>::pending_head() const {
      std::lock_guard g( my->mtx );
      const auto& indx = my->index.template get<by_best_branch>();

      auto itr = indx.lower_bound( false );
      if( itr != indx.end() && !fork_database_impl<BSP>::BSAccessor::is_valid(**itr) ) {
         if( first_preferred( **itr, *my->head ) )
            return *itr;
      }

      return my->head;
   }

   template <class BSP>
   fork_database_t<BSP>::branch_type
   fork_database_t<BSP>::fetch_branch(const block_id_type& h, uint32_t trim_after_block_num) const {
      std::lock_guard g(my->mtx);
      return my->fetch_branch_impl(h, trim_after_block_num);
   }

   template <class BSP>
   fork_database_t<BSP>::branch_type
   fork_database_impl<BSP>::fetch_branch_impl(const block_id_type& h, uint32_t trim_after_block_num) const {
      branch_type result;
      result.reserve(index.size());
      for (auto i = index.find(h); i != index.end(); i = index.find((*i)->previous())) {
         if ((*i)->block_num() <= trim_after_block_num)
            result.push_back(*i);
      }

      return result;
   }

   template <class BSP>
   block_branch_t
   fork_database_t<BSP>::fetch_block_branch(const block_id_type& h, uint32_t trim_after_block_num) const {
      std::lock_guard g(my->mtx);
      return my->fetch_block_branch_impl(h, trim_after_block_num);
   }

   template <class BSP>
   block_branch_t
   fork_database_impl<BSP>::fetch_block_branch_impl(const block_id_type& h, uint32_t trim_after_block_num) const {
      block_branch_t result;
      result.reserve(index.size());
      for (auto i = index.find(h); i != index.end(); i = index.find((*i)->previous())) {
         if ((*i)->block_num() <= trim_after_block_num)
            result.push_back((*i)->block);
      }

      return result;
   }

   template <class bsp>
   fork_database_t<bsp>::full_branch_type
   fork_database_t<bsp>::fetch_full_branch(const block_id_type& h) const {
      std::lock_guard g(my->mtx);
      return my->fetch_full_branch_impl(h);
   }

   template <class bsp>
   fork_database_t<bsp>::full_branch_type
   fork_database_impl<bsp>::fetch_full_branch_impl(const block_id_type& h) const {
      full_branch_type result;
      result.reserve(index.size());
      for (auto i = index.find(h); i != index.end(); i = index.find((*i)->previous())) {
         result.push_back(*i);
      }
      result.push_back(root);
      return result;
   }

   template<class BSP>
   BSP fork_database_t<BSP>::search_on_branch( const block_id_type& h, uint32_t block_num ) const {
      std::lock_guard g( my->mtx );
      return my->search_on_branch_impl( h, block_num );
   }

   template<class BSP>
   BSP fork_database_impl<BSP>::search_on_branch_impl( const block_id_type& h, uint32_t block_num ) const {
      for( auto i = index.find(h); i != index.end(); i = index.find( (*i)->previous() ) ) {
         if ((*i)->block_num() == block_num)
            return *i;
      }

      return {};
   }

   /**
    *  Given two head blocks, return two branches of the fork graph that
    *  end with a common ancestor (same prior block)
    */
   template <class BSP>
   fork_database_t<BSP>::branch_type_pair
   fork_database_t<BSP>::fetch_branch_from(const block_id_type& first, const block_id_type& second) const {
      std::lock_guard g(my->mtx);
      return my->fetch_branch_from_impl(first, second);
   }

   template <class BSP>
   fork_database_t<BSP>::branch_type_pair
   fork_database_impl<BSP>::fetch_branch_from_impl(const block_id_type& first, const block_id_type& second) const {
      pair<branch_type, branch_type> result;
      auto first_branch = (first == root->id()) ? root : get_block_impl(first);
      auto second_branch = (second == root->id()) ? root : get_block_impl(second);

      EOS_ASSERT(first_branch, fork_db_block_not_found, "block ${id} does not exist", ("id", first));
      EOS_ASSERT(second_branch, fork_db_block_not_found, "block ${id} does not exist", ("id", second));

      while( first_branch->block_num() > second_branch->block_num() )
      {
         result.first.push_back(first_branch);
         const auto& prev = first_branch->previous();
         first_branch = (prev == root->id()) ? root : get_block_impl( prev );
         EOS_ASSERT( first_branch, fork_db_block_not_found,
                     "block ${id} does not exist",
                     ("id", prev)
         );
      }

      while( second_branch->block_num() > first_branch->block_num() )
      {
         result.second.push_back( second_branch );
         const auto& prev = second_branch->previous();
         second_branch = (prev == root->id()) ? root : get_block_impl( prev );
         EOS_ASSERT( second_branch, fork_db_block_not_found,
                     "block ${id} does not exist",
                     ("id", prev)
         );
      }

      if (first_branch->id() == second_branch->id()) return result;

      while( first_branch->previous() != second_branch->previous() )
      {
         result.first.push_back(first_branch);
         result.second.push_back(second_branch);
         const auto &first_prev = first_branch->previous();
         first_branch = get_block_impl( first_prev );
         const auto &second_prev = second_branch->previous();
         second_branch = get_block_impl( second_prev );
         EOS_ASSERT( first_branch, fork_db_block_not_found,
                     "block ${id} does not exist",
                     ("id", first_prev)
         );
         EOS_ASSERT( second_branch, fork_db_block_not_found,
                     "block ${id} does not exist",
                     ("id", second_prev)
         );
      }

      if( first_branch && second_branch )
      {
         result.first.push_back(first_branch);
         result.second.push_back(second_branch);
      }
      return result;
   } /// fetch_branch_from_impl

   /// remove all of the invalid forks built off of this id including this id
   template<class BSP>
   void fork_database_t<BSP>::remove( const block_id_type& id ) {
      std::lock_guard g( my->mtx );
      return my->remove_impl( id );
   }

   template<class BSP>
   void fork_database_impl<BSP>::remove_impl( const block_id_type& id ) {
      deque<block_id_type> remove_queue{id};
      const auto& previdx = index.template get<by_prev>();
      const auto& head_id = head->id();

      for( uint32_t i = 0; i < remove_queue.size(); ++i ) {
         EOS_ASSERT( remove_queue[i] != head_id, fork_database_exception,
                     "removing the block and its descendants would remove the current head block" );

         auto previtr = previdx.lower_bound( remove_queue[i] );
         while( previtr != previdx.end() && (*previtr)->previous() == remove_queue[i] ) {
            remove_queue.emplace_back( (*previtr)->id() );
            ++previtr;
         }
      }

      for( const auto& block_id : remove_queue ) {
         index.erase( block_id );
      }
   }

   template<class BSP>
   void fork_database_t<BSP>::mark_valid( const BSP& h ) {
      std::lock_guard g( my->mtx );
      my->mark_valid_impl( h );
   }

   template<class BSP>
   void fork_database_impl<BSP>::mark_valid_impl( const BSP& h ) {
      if( BSAccessor::is_valid(*h) ) return;

      auto& by_id_idx = index.template get<by_block_id>();

      auto itr = by_id_idx.find( h->id() );
      EOS_ASSERT( itr != by_id_idx.end(), fork_database_exception,
                  "block state not in fork database; cannot mark as valid",
                  ("id", h->id()) );

      by_id_idx.modify( itr, []( auto& i ) {
         BSAccessor::set_valid(*i, true);
      } );

      auto candidate = index.template get<by_best_branch>().begin();
      if( first_preferred( **candidate, *head ) ) {
         head = *candidate;
      }
   }

   template<class BSP>
   void fork_database_t<BSP>::update_best_qc( const block_id_type& id, const qc_claim_t& most_recent_ancestor_with_qc ) {
      std::lock_guard g( my->mtx );
      my->update_best_qc_impl( id, most_recent_ancestor_with_qc );
   }

   template<class BSP>
   void fork_database_impl<BSP>::update_best_qc_impl( const block_id_type& id, const qc_claim_t& most_recent_ancestor_with_qc ) {
      auto& by_id_idx = index.template get<by_block_id>();

      auto itr = by_id_idx.find( id );
      EOS_ASSERT( itr != by_id_idx.end(), fork_database_exception,
                  "block state not in fork database; cannot update", ("id", id) );

      if (BSAccessor::qc_claim_update_needed(**itr, most_recent_ancestor_with_qc)) {
         by_id_idx.modify( itr, [&]( auto& i ) {
            BSAccessor::update_best_qc(*i, most_recent_ancestor_with_qc);
         } );
      }

      // process descendants
      vector<block_id_type> descendants;
      descendants.reserve(index.size());
      descendants.push_back(id);
      auto& previdx = index.template get<by_prev>();
      for( uint32_t i = 0; i < descendants.size(); ++i ) {
         auto previtr = previdx.lower_bound( descendants[i] );
         while( previtr != previdx.end() && (*previtr)->previous() == descendants[i] ) {
            if (BSAccessor::qc_claim_update_needed(**previtr, most_recent_ancestor_with_qc)) {
               previdx.modify( previtr, [&](auto& i) {
                  BSAccessor::update_best_qc(*i, most_recent_ancestor_with_qc);
               });
            } else {
               break;
            }

            descendants.emplace_back( (*previtr)->id() );
            ++previtr;
         }
      }

      auto candidate = index.template get<by_best_branch>().begin();
      if( first_preferred( **candidate, *head ) ) {
         head = *candidate;
      }
   }

   template<class BSP>
   BSP fork_database_t<BSP>::get_block(const block_id_type& id) const {
      std::lock_guard g( my->mtx );
      return my->get_block_impl(id);
   }

   template<class BSP>
   BSP fork_database_impl<BSP>::get_block_impl(const block_id_type& id) const {
      auto itr = index.find( id );
      if( itr != index.end() )
         return *itr;
      return {};
   }

   // ------------------ fork_database -------------------------

   fork_database::fork_database(const std::filesystem::path& data_dir)
      : data_dir(data_dir)
        // currently needed because chain_head is accessed before fork database open
      , fork_db_legacy{std::make_unique<fork_database_legacy_t>(fork_database_legacy_t::legacy_magic_number)}
   {
   }

   fork_database::~fork_database() {
      close();
   }

   void fork_database::close() {
      apply<void>([&](auto& forkdb) { forkdb.close(data_dir / config::forkdb_filename); });
   }

   void fork_database::open( validator_t& validator ) {
      if (!std::filesystem::is_directory(data_dir))
         std::filesystem::create_directories(data_dir);

      auto fork_db_file = data_dir / config::forkdb_filename;
      if( std::filesystem::exists( fork_db_file ) ) {
         try {
            fc::cfile f;
            f.set_file_path(fork_db_file);
            f.open("rb");

            fc::cfile_datastream ds(f);

            // determine file type, validate totem
            uint32_t totem = 0;
            fc::raw::unpack( ds, totem );
            EOS_ASSERT( totem == fork_database_legacy_t::legacy_magic_number ||
                        totem == fork_database_if_t::magic_number, fork_database_exception,
                        "Fork database file '${filename}' has unexpected magic number: ${actual_totem}. Expected ${t1} or ${t2}",
                        ("filename", fork_db_file)
                        ("actual_totem", totem)("t1", fork_database_legacy_t::legacy_magic_number)("t2", fork_database_if_t::magic_number)
            );

            if (totem == fork_database_legacy_t::legacy_magic_number) {
               // fork_db_legacy created in constructor
               apply_legacy<void>([&](auto& forkdb) {
                  forkdb.open(fork_db_file, validator);
               });
            } else {
               // file is instant-finality data, so switch to fork_database_if_t
               fork_db_if = std::make_unique<fork_database_if_t>(fork_database_if_t::magic_number);
               legacy = false;
               apply_if<void>([&](auto& forkdb) {
                  forkdb.open(fork_db_file, validator);
               });
            }
         } FC_CAPTURE_AND_RETHROW( (fork_db_file) )
      }
   }

   void fork_database::switch_from_legacy() {
      // no need to close fork_db because we don't want to write anything out, file is removed on open
      // threads may be accessing (or locked on mutex about to access legacy forkdb) so don't delete it until program exit
      assert(legacy);
      block_state_legacy_ptr head = fork_db_legacy->chain_head; // will throw if called after transistion
      auto new_head = std::make_shared<block_state>(*head);
      fork_db_if = std::make_unique<fork_database_if_t>(fork_database_if_t::magic_number);
      legacy = false;
      apply_if<void>([&](auto& forkdb) {
         forkdb.chain_head = new_head;
         forkdb.reset_root(*new_head);
      });
   }

   block_branch_t fork_database::fetch_branch_from_head() const {
      return apply<block_branch_t>([&](auto& forkdb) {
         return forkdb.fetch_block_branch(forkdb.head()->id());
      });
   }

   // do class instantiations
   template class fork_database_t<block_state_legacy_ptr>;
   template class fork_database_t<block_state_ptr>;
   
   template struct fork_database_impl<block_state_legacy_ptr>;
   template struct fork_database_impl<block_state_ptr>;

} /// eosio::chain
