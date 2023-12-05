#include <eosio/chain/fork_database.hpp>
#include <eosio/chain/exceptions.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <fc/io/fstream.hpp>
#include <fstream>
#include <shared_mutex>

namespace eosio { namespace chain {
   using boost::multi_index_container;
   using namespace boost::multi_index;

   const uint32_t fork_database::magic_number = 0x30510FDB;

   const uint32_t fork_database::min_supported_version = 1;
   const uint32_t fork_database::max_supported_version = 1;

   // work around block_state_legacy::is_valid being private
   inline bool block_state_is_valid( const block_state_legacy& bs ) {
      return bs.is_valid();
   }

   /**
    * History:
    * Version 1: initial version of the new refactored fork database portable format
    */

   struct by_block_id;
   struct by_lib_block_num;
   struct by_prev;
   typedef multi_index_container<
      block_state_legacy_ptr,
      indexed_by<
         hashed_unique< tag<by_block_id>, member<block_header_state_legacy, block_id_type, &block_header_state_legacy::id>, std::hash<block_id_type>>,
         ordered_non_unique< tag<by_prev>, const_mem_fun<block_header_state_legacy, const block_id_type&, &block_header_state_legacy::prev> >,
         ordered_unique< tag<by_lib_block_num>,
            composite_key< block_state_legacy,
               global_fun<const block_state_legacy&,            bool,          &block_state_is_valid>,
               member<detail::block_header_state_legacy_common, uint32_t,      &detail::block_header_state_legacy_common::dpos_irreversible_blocknum>,
               member<detail::block_header_state_legacy_common, uint32_t,      &detail::block_header_state_legacy_common::block_num>,
               member<block_header_state_legacy,                block_id_type, &block_header_state_legacy::id>
            >,
            composite_key_compare<
               std::greater<bool>,
               std::greater<uint32_t>,
               std::greater<uint32_t>,
               sha256_less
            >
         >
      >
   > fork_multi_index_type;

   bool first_preferred( const block_header_state_legacy& lhs, const block_header_state_legacy& rhs ) {
      return std::tie( lhs.dpos_irreversible_blocknum, lhs.block_num )
               > std::tie( rhs.dpos_irreversible_blocknum, rhs.block_num );
   }

   struct fork_database_impl {
      explicit fork_database_impl( const std::filesystem::path& data_dir )
      :datadir(data_dir)
      {}

      std::shared_mutex      mtx;
      fork_multi_index_type  index;
      block_state_legacy_ptr root; // Only uses the block_header_state_legacy portion
      block_state_legacy_ptr head;
      std::filesystem::path  datadir;

      void open_impl( const std::function<void( block_timestamp_type,
                                                const flat_set<digest_type>&,
                                                const vector<digest_type>& )>& validator );
      void close_impl();


      block_header_state_legacy_ptr  get_block_header_impl( const block_id_type& id )const;
      block_state_legacy_ptr         get_block_impl( const block_id_type& id )const;
      void            reset_impl( const block_header_state_legacy& root_bhs );
      void            rollback_head_to_root_impl();
      void            advance_root_impl( const block_id_type& id );
      void            remove_impl( const block_id_type& id );
      branch_type     fetch_branch_impl( const block_id_type& h, uint32_t trim_after_block_num )const;
      block_state_legacy_ptr search_on_branch_impl( const block_id_type& h, uint32_t block_num )const;
      pair<branch_type, branch_type> fetch_branch_from_impl( const block_id_type& first,
                                                             const block_id_type& second )const;
      void mark_valid_impl( const block_state_legacy_ptr& h );

      void add_impl( const block_state_legacy_ptr& n,
                     bool ignore_duplicate, bool validate,
                     const std::function<void( block_timestamp_type,
                                               const flat_set<digest_type>&,
                                               const vector<digest_type>& )>& validator );
   };


   fork_database::fork_database( const std::filesystem::path& data_dir )
   :my( new fork_database_impl( data_dir ) )
   {}


   void fork_database::open( const std::function<void( block_timestamp_type,
                                                       const flat_set<digest_type>&,
                                                       const vector<digest_type>& )>& validator )
   {
      std::lock_guard g( my->mtx );
      my->open_impl( validator );
   }

   void fork_database_impl::open_impl( const std::function<void( block_timestamp_type,
                                                                 const flat_set<digest_type>&,
                                                                 const vector<digest_type>& )>& validator )
   {
      if (!std::filesystem::is_directory(datadir))
         std::filesystem::create_directories(datadir);

      auto fork_db_dat = datadir / config::forkdb_filename;
      if( std::filesystem::exists( fork_db_dat ) ) {
         try {
            string content;
            fc::read_file_contents( fork_db_dat, content );

            fc::datastream<const char*> ds( content.data(), content.size() );

            // validate totem
            uint32_t totem = 0;
            fc::raw::unpack( ds, totem );
            EOS_ASSERT( totem == fork_database::magic_number, fork_database_exception,
                        "Fork database file '${filename}' has unexpected magic number: ${actual_totem}. Expected ${expected_totem}",
                        ("filename", fork_db_dat)
                        ("actual_totem", totem)
                        ("expected_totem", fork_database::magic_number)
            );

            // validate version
            uint32_t version = 0;
            fc::raw::unpack( ds, version );
            EOS_ASSERT( version >= fork_database::min_supported_version && version <= fork_database::max_supported_version,
                        fork_database_exception,
                       "Unsupported version of fork database file '${filename}'. "
                       "Fork database version is ${version} while code supports version(s) [${min},${max}]",
                       ("filename", fork_db_dat)
                       ("version", version)
                       ("min", fork_database::min_supported_version)
                       ("max", fork_database::max_supported_version)
            );

            block_header_state_legacy bhs;
            fc::raw::unpack( ds, bhs );
            reset_impl( bhs );

            unsigned_int size; fc::raw::unpack( ds, size );
            for( uint32_t i = 0, n = size.value; i < n; ++i ) {
               block_state_legacy s;
               fc::raw::unpack( ds, s );
               // do not populate transaction_metadatas, they will be created as needed in apply_block with appropriate key recovery
               s.header_exts = s.block->validate_and_extract_header_extensions();
               add_impl( std::make_shared<block_state_legacy>( std::move( s ) ), false, true, validator );
            }
            block_id_type head_id;
            fc::raw::unpack( ds, head_id );

            if( root->id == head_id ) {
               head = root;
            } else {
               head = get_block_impl( head_id );
               EOS_ASSERT( head, fork_database_exception,
                           "could not find head while reconstructing fork database from file; '${filename}' is likely corrupted",
                           ("filename", fork_db_dat) );
            }

            auto candidate = index.get<by_lib_block_num>().begin();
            if( candidate == index.get<by_lib_block_num>().end() || !(*candidate)->is_valid() ) {
               EOS_ASSERT( head->id == root->id, fork_database_exception,
                           "head not set to root despite no better option available; '${filename}' is likely corrupted",
                           ("filename", fork_db_dat) );
            } else {
               EOS_ASSERT( !first_preferred( **candidate, *head ), fork_database_exception,
                           "head not set to best available option available; '${filename}' is likely corrupted",
                           ("filename", fork_db_dat) );
            }
         } FC_CAPTURE_AND_RETHROW( (fork_db_dat) )

         std::filesystem::remove( fork_db_dat );
      }
   }

   void fork_database::close() {
      std::lock_guard g( my->mtx );
      my->close_impl();
   }

   void fork_database_impl::close_impl() {
      auto fork_db_dat = datadir / config::forkdb_filename;

      if( !root ) {
         if( index.size() > 0 ) {
            elog( "fork_database is in a bad state when closing; not writing out '${filename}'",
                  ("filename", fork_db_dat) );
         }
         return;
      }

      std::ofstream out( fork_db_dat.generic_string().c_str(), std::ios::out | std::ios::binary | std::ofstream::trunc );
      fc::raw::pack( out, fork_database::magic_number );
      fc::raw::pack( out, fork_database::max_supported_version ); // write out current version which is always max_supported_version
      fc::raw::pack( out, *static_cast<block_header_state_legacy*>(&*root) );
      uint32_t num_blocks_in_fork_db = index.size();
      fc::raw::pack( out, unsigned_int{num_blocks_in_fork_db} );

      const auto& indx = index.get<by_lib_block_num>();

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
         fc::raw::pack( out, head->id );
      } else {
         elog( "head not set in fork database; '${filename}' will be corrupted",
               ("filename", fork_db_dat) );
      }

      index.clear();
   }

   fork_database::~fork_database() {
      my->close_impl();
   }

   void fork_database::reset( const block_header_state_legacy& root_bhs ) {
      std::lock_guard g( my->mtx );
      my->reset_impl(root_bhs);
   }

   void fork_database_impl::reset_impl( const block_header_state_legacy& root_bhs ) {
      index.clear();
      root = std::make_shared<block_state_legacy>();
      static_cast<block_header_state_legacy&>(*root) = root_bhs;
      root->validated = true;
      head = root;
   }

   void fork_database::rollback_head_to_root() {
      std::lock_guard g( my->mtx );
      my->rollback_head_to_root_impl();
   }

   void fork_database_impl::rollback_head_to_root_impl() {
      auto& by_id_idx = index.get<by_block_id>();
      auto itr = by_id_idx.begin();
      while (itr != by_id_idx.end()) {
         by_id_idx.modify( itr, [&]( block_state_legacy_ptr& bsp ) {
            bsp->validated = false;
         } );
         ++itr;
      }
      head = root;
   }

   void fork_database::advance_root( const block_id_type& id ) {
      std::lock_guard g( my->mtx );
      my->advance_root_impl( id );
   }

   void fork_database_impl::advance_root_impl( const block_id_type& id ) {
      EOS_ASSERT( root, fork_database_exception, "root not yet set" );

      auto new_root = get_block_impl( id );
      EOS_ASSERT( new_root, fork_database_exception,
                  "cannot advance root to a block that does not exist in the fork database" );
      EOS_ASSERT( new_root->is_valid(), fork_database_exception,
                  "cannot advance root to a block that has not yet been validated" );


      deque<block_id_type> blocks_to_remove;
      for( auto b = new_root; b; ) {
         blocks_to_remove.emplace_back( b->header.previous );
         b = get_block_impl( blocks_to_remove.back() );
         EOS_ASSERT( b || blocks_to_remove.back() == root->id, fork_database_exception, "invariant violation: orphaned branch was present in forked database" );
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

   block_header_state_legacy_ptr fork_database::get_block_header( const block_id_type& id )const {
      std::shared_lock g( my->mtx );
      return my->get_block_header_impl( id );
   }

   block_header_state_legacy_ptr fork_database_impl::get_block_header_impl( const block_id_type& id )const {
      if( root->id == id ) {
         return root;
      }

      auto itr = index.find( id );
      if( itr != index.end() )
         return *itr;

      return block_header_state_legacy_ptr();
   }

   void fork_database_impl::add_impl( const block_state_legacy_ptr& n,
                                      bool ignore_duplicate, bool validate,
                                      const std::function<void( block_timestamp_type,
                                                                const flat_set<digest_type>&,
                                                                const vector<digest_type>& )>& validator )
   {
      EOS_ASSERT( root, fork_database_exception, "root not yet set" );
      EOS_ASSERT( n, fork_database_exception, "attempt to add null block state" );

      auto prev_bh = get_block_header_impl( n->header.previous );

      EOS_ASSERT( prev_bh, unlinkable_block_exception,
                  "unlinkable block", ("id", n->id)("previous", n->header.previous) );

      if( validate ) {
         try {
            const auto& exts = n->header_exts;

            if( exts.count(protocol_feature_activation::extension_id()) > 0 ) {
               const auto& new_protocol_features = std::get<protocol_feature_activation>(exts.lower_bound(protocol_feature_activation::extension_id())->second).protocol_features;
               validator( n->header.timestamp, prev_bh->activated_protocol_features->protocol_features, new_protocol_features );
            }
         } EOS_RETHROW_EXCEPTIONS( fork_database_exception, "serialized fork database is incompatible with configured protocol features"  )
      }

      auto inserted = index.insert(n);
      if( !inserted.second ) {
         if( ignore_duplicate ) return;
         EOS_THROW( fork_database_exception, "duplicate block added", ("id", n->id) );
      }

      auto candidate = index.get<by_lib_block_num>().begin();
      if( (*candidate)->is_valid() ) {
         head = *candidate;
      }
   }

   void fork_database::add( const block_state_legacy_ptr& n, bool ignore_duplicate ) {
      std::lock_guard g( my->mtx );
      my->add_impl( n, ignore_duplicate, false,
                    []( block_timestamp_type timestamp,
                        const flat_set<digest_type>& cur_features,
                        const vector<digest_type>& new_features )
                    {}
      );
   }

   block_state_legacy_ptr fork_database::root()const {
      std::shared_lock g( my->mtx );
      return my->root;
   }

   block_state_legacy_ptr fork_database::head()const {
      std::shared_lock g( my->mtx );
      return my->head;
   }

   block_state_legacy_ptr fork_database::pending_head()const {
      std::shared_lock g( my->mtx );
      const auto& indx = my->index.get<by_lib_block_num>();

      auto itr = indx.lower_bound( false );
      if( itr != indx.end() && !(*itr)->is_valid() ) {
         if( first_preferred( **itr, *my->head ) )
            return *itr;
      }

      return my->head;
   }

   branch_type fork_database::fetch_branch( const block_id_type& h, uint32_t trim_after_block_num )const {
      std::shared_lock g( my->mtx );
      return my->fetch_branch_impl( h, trim_after_block_num );
   }

   branch_type fork_database_impl::fetch_branch_impl( const block_id_type& h, uint32_t trim_after_block_num )const {
      branch_type result;
      for( auto s = get_block_impl(h); s; s = get_block_impl( s->header.previous ) ) {
         if( s->block_num <= trim_after_block_num )
             result.push_back( s );
      }

      return result;
   }

   block_state_legacy_ptr fork_database::search_on_branch( const block_id_type& h, uint32_t block_num )const {
      std::shared_lock g( my->mtx );
      return my->search_on_branch_impl( h, block_num );
   }

   block_state_legacy_ptr fork_database_impl::search_on_branch_impl( const block_id_type& h, uint32_t block_num )const {
      for( auto s = get_block_impl(h); s; s = get_block_impl( s->header.previous ) ) {
         if( s->block_num == block_num )
             return s;
      }

      return {};
   }

   /**
    *  Given two head blocks, return two branches of the fork graph that
    *  end with a common ancestor (same prior block)
    */
   pair< branch_type, branch_type >  fork_database::fetch_branch_from( const block_id_type& first,
                                                                       const block_id_type& second )const {
      std::shared_lock g( my->mtx );
      return my->fetch_branch_from_impl( first, second );
   }

   pair< branch_type, branch_type >  fork_database_impl::fetch_branch_from_impl( const block_id_type& first,
                                                                                 const block_id_type& second )const {
      pair<branch_type,branch_type> result;
      auto first_branch = (first == root->id) ? root : get_block_impl(first);
      auto second_branch = (second == root->id) ? root : get_block_impl(second);

      EOS_ASSERT(first_branch, fork_db_block_not_found, "block ${id} does not exist", ("id", first));
      EOS_ASSERT(second_branch, fork_db_block_not_found, "block ${id} does not exist", ("id", second));

      while( first_branch->block_num > second_branch->block_num )
      {
         result.first.push_back(first_branch);
         const auto& prev = first_branch->header.previous;
         first_branch = (prev == root->id) ? root : get_block_impl( prev );
         EOS_ASSERT( first_branch, fork_db_block_not_found,
                     "block ${id} does not exist",
                     ("id", prev)
         );
      }

      while( second_branch->block_num > first_branch->block_num )
      {
         result.second.push_back( second_branch );
         const auto& prev = second_branch->header.previous;
         second_branch = (prev == root->id) ? root : get_block_impl( prev );
         EOS_ASSERT( second_branch, fork_db_block_not_found,
                     "block ${id} does not exist",
                     ("id", prev)
         );
      }

      if (first_branch->id == second_branch->id) return result;

      while( first_branch->header.previous != second_branch->header.previous )
      {
         result.first.push_back(first_branch);
         result.second.push_back(second_branch);
         const auto &first_prev = first_branch->header.previous;
         first_branch = get_block_impl( first_prev );
         const auto &second_prev = second_branch->header.previous;
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
   void fork_database::remove( const block_id_type& id ) {
      std::lock_guard g( my->mtx );
      return my->remove_impl( id );
   }

   void fork_database_impl::remove_impl( const block_id_type& id ) {
      deque<block_id_type> remove_queue{id};
      const auto& previdx = index.get<by_prev>();
      const auto& head_id = head->id;

      for( uint32_t i = 0; i < remove_queue.size(); ++i ) {
         EOS_ASSERT( remove_queue[i] != head_id, fork_database_exception,
                     "removing the block and its descendants would remove the current head block" );

         auto previtr = previdx.lower_bound( remove_queue[i] );
         while( previtr != previdx.end() && (*previtr)->header.previous == remove_queue[i] ) {
            remove_queue.emplace_back( (*previtr)->id );
            ++previtr;
         }
      }

      for( const auto& block_id : remove_queue ) {
         index.erase( block_id );
      }
   }

   void fork_database::mark_valid( const block_state_legacy_ptr& h ) {
      std::lock_guard g( my->mtx );
      my->mark_valid_impl( h );
   }

   void fork_database_impl::mark_valid_impl( const block_state_legacy_ptr& h ) {
      if( h->validated ) return;

      auto& by_id_idx = index.get<by_block_id>();

      auto itr = by_id_idx.find( h->id );
      EOS_ASSERT( itr != by_id_idx.end(), fork_database_exception,
                  "block state not in fork database; cannot mark as valid",
                  ("id", h->id) );

      by_id_idx.modify( itr, []( block_state_legacy_ptr& bsp ) {
         bsp->validated = true;
      } );

      auto candidate = index.get<by_lib_block_num>().begin();
      if( first_preferred( **candidate, *head ) ) {
         head = *candidate;
      }
   }

   block_state_legacy_ptr fork_database::get_block(const block_id_type& id)const {
      std::shared_lock g( my->mtx );
      return my->get_block_impl(id);
   }

   block_state_legacy_ptr fork_database_impl::get_block_impl(const block_id_type& id)const {
      auto itr = index.find( id );
      if( itr != index.end() )
         return *itr;
      return block_state_legacy_ptr();
   }

} } /// eosio::chain
