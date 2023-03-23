#pragma once
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/block_log.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain/genesis_state.hpp>
#include <chainbase/pinnable_mapped_file.hpp>
#include <boost/signals2/signal.hpp>

#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/account_object.hpp>
#include <eosio/chain/snapshot.hpp>
#include <eosio/chain/protocol_feature_manager.hpp>
#include <eosio/chain/webassembly/eos-vm-oc/config.hpp>

namespace chainbase {
   class database;
}
namespace boost { namespace asio {
   class thread_pool;
}}

namespace eosio { namespace vm { class wasm_allocator; }}

namespace eosio { namespace chain {

   class authorization_manager;

   namespace resource_limits {
      class resource_limits_manager;
   };

   struct controller_impl;
   using chainbase::database;
   using chainbase::pinnable_mapped_file;
   using boost::signals2::signal;

   class dynamic_global_property_object;
   class global_property_object;
   class permission_object;
   class account_object;
   class deep_mind_handler;
   using resource_limits::resource_limits_manager;
   using apply_handler = std::function<void(apply_context&)>;
   using forked_branch_callback = std::function<void(const branch_type&)>;
   // lookup transaction_metadata via supplied function to avoid re-creation
   using trx_meta_cache_lookup = std::function<transaction_metadata_ptr( const transaction_id_type&)>;

   class fork_database;

   enum class db_read_mode {
      HEAD,
      IRREVERSIBLE
   };

   enum class validation_mode {
      FULL,
      LIGHT
   };

   class controller {
      public:
         struct config {
            flat_set<account_name>   sender_bypass_whiteblacklist;
            flat_set<account_name>   actor_whitelist;
            flat_set<account_name>   actor_blacklist;
            flat_set<account_name>   contract_whitelist;
            flat_set<account_name>   contract_blacklist;
            flat_set< pair<account_name, action_name> > action_blacklist;
            flat_set<public_key_type> key_blacklist;
            path                     blocks_dir             =  chain::config::default_blocks_dir_name;
            block_log_config         blog;
            path                     state_dir              =  chain::config::default_state_dir_name;
            uint64_t                 state_size             =  chain::config::default_state_size;
            uint64_t                 state_guard_size       =  chain::config::default_state_guard_size;
            uint32_t                 sig_cpu_bill_pct       =  chain::config::default_sig_cpu_bill_pct;
            uint16_t                 thread_pool_size       =  chain::config::default_controller_thread_pool_size;
            uint32_t   max_nonprivileged_inline_action_size =  chain::config::default_max_nonprivileged_inline_action_size;
            bool                     read_only              =  false;
            bool                     force_all_checks       =  false;
            bool                     disable_replay_opts    =  false;
            bool                     contracts_console      =  false;
            bool                     allow_ram_billing_in_notify = false;
            uint32_t                 maximum_variable_signature_length = chain::config::default_max_variable_signature_length;
            bool                     disable_all_subjective_mitigations = false; //< for developer & testing purposes, can be configured using `disable-all-subjective-mitigations` when `EOSIO_DEVELOPER` build option is provided
            uint32_t                 terminate_at_block     = 0;
            bool                     integrity_hash_on_start= false;
            bool                     integrity_hash_on_stop = false;

            wasm_interface::vm_type  wasm_runtime = chain::config::default_wasm_runtime;
            eosvmoc::config          eosvmoc_config;
            bool                     eosvmoc_tierup         = false;

            db_read_mode             read_mode              = db_read_mode::HEAD;
            validation_mode          block_validation_mode  = validation_mode::FULL;

            pinnable_mapped_file::map_mode db_map_mode      = pinnable_mapped_file::map_mode::mapped;

            flat_set<account_name>   resource_greylist;
            flat_set<account_name>   trusted_producers;
            uint32_t                 greylist_limit         = chain::config::maximum_elastic_resource_multiplier;

            flat_set<account_name>   profile_accounts;
         };

         enum class block_status {
            irreversible = 0, ///< this block has already been applied before by this node and is considered irreversible
            validated   = 1, ///< this is a complete block signed by a valid producer and has been previously applied by this node and therefore validated but it is not yet irreversible
            complete   = 2, ///< this is a complete block signed by a valid producer but is not yet irreversible nor has it yet been applied by this node
            incomplete  = 3, ///< this is an incomplete block being produced by a producer
            ephemeral = 4  ///< this is an incomplete block created for speculative execution of trxs, will always be aborted
         };

         controller( const config& cfg, const chain_id_type& chain_id );
         controller( const config& cfg, protocol_feature_set&& pfs, const chain_id_type& chain_id );
         ~controller();

         void add_indices();
         void startup( std::function<void()> shutdown, std::function<bool()> check_shutdown, const snapshot_reader_ptr& snapshot);
         void startup( std::function<void()> shutdown, std::function<bool()> check_shutdown, const genesis_state& genesis);
         void startup( std::function<void()> shutdown, std::function<bool()> check_shutdown);

         void preactivate_feature( const digest_type& feature_digest, bool is_trx_transient );

         vector<digest_type> get_preactivated_protocol_features()const;

         void validate_protocol_features( const vector<digest_type>& features_to_activate )const;

         /**
          * Starts a new pending block session upon which new transactions can be pushed.
          */
         void start_block( block_timestamp_type time,
                           uint16_t confirm_block_count,
                           const vector<digest_type>& new_protocol_feature_activations,
                           block_status bs,
                           const fc::time_point& deadline = fc::time_point::maximum() );

         /**
          * @return transactions applied in aborted block
          */
         deque<transaction_metadata_ptr> abort_block();

       /**
        *
        */
         transaction_trace_ptr push_transaction( const transaction_metadata_ptr& trx,
                                                 fc::time_point deadline, fc::microseconds max_transaction_time,
                                                 uint32_t billed_cpu_time_us, bool explicit_billed_cpu_time,
                                                 int64_t subjective_cpu_bill_us );

         /**
          * Attempt to execute a specific transaction in our deferred trx database
          *
          */
         transaction_trace_ptr push_scheduled_transaction( const transaction_id_type& scheduled,
                                                           fc::time_point block_deadline, fc::microseconds max_transaction_time,
                                                           uint32_t billed_cpu_time_us, bool explicit_billed_cpu_time );

         struct block_report {
            size_t             total_net_usage = 0;
            size_t             total_cpu_usage_us = 0;
            fc::microseconds   total_elapsed_time{};
            fc::microseconds   total_time{};
         };

         block_state_ptr finalize_block( block_report& br, const signer_callback_type& signer_callback );
         void sign_block( const signer_callback_type& signer_callback );
         void commit_block();

         // thread-safe
         std::future<block_state_ptr> create_block_state_future( const block_id_type& id, const signed_block_ptr& b );
         // thread-safe
         block_state_ptr create_block_state( const block_id_type& id, const signed_block_ptr& b ) const;

         /**
          * @param br returns statistics for block
          * @param bsp block to push
          * @param cb calls cb with forked applied transactions for each forked block
          * @param trx_lookup user provided lookup function for externally cached transaction_metadata
          */
         void push_block( block_report& br,
                          const block_state_ptr& bsp,
                          const forked_branch_callback& cb,
                          const trx_meta_cache_lookup& trx_lookup );

         boost::asio::io_context& get_thread_pool();

         const chainbase::database& db()const;

         const fork_database& fork_db()const;

         const account_object&                 get_account( account_name n )const;
         const global_property_object&         get_global_properties()const;
         const dynamic_global_property_object& get_dynamic_global_properties()const;
         const resource_limits_manager&        get_resource_limits_manager()const;
         resource_limits_manager&              get_mutable_resource_limits_manager();
         const authorization_manager&          get_authorization_manager()const;
         authorization_manager&                get_mutable_authorization_manager();
         const protocol_feature_manager&       get_protocol_feature_manager()const;
         uint32_t                              get_max_nonprivileged_inline_action_size()const;

         const flat_set<account_name>&   get_actor_whitelist() const;
         const flat_set<account_name>&   get_actor_blacklist() const;
         const flat_set<account_name>&   get_contract_whitelist() const;
         const flat_set<account_name>&   get_contract_blacklist() const;
         const flat_set< pair<account_name, action_name> >& get_action_blacklist() const;
         const flat_set<public_key_type>& get_key_blacklist() const;

         void   set_actor_whitelist( const flat_set<account_name>& );
         void   set_actor_blacklist( const flat_set<account_name>& );
         void   set_contract_whitelist( const flat_set<account_name>& );
         void   set_contract_blacklist( const flat_set<account_name>& );
         void   set_action_blacklist( const flat_set< pair<account_name, action_name> >& );
         void   set_key_blacklist( const flat_set<public_key_type>& );

         uint32_t             head_block_num()const;
         time_point           head_block_time()const;
         block_id_type        head_block_id()const;
         account_name         head_block_producer()const;
         const block_header&  head_block_header()const;
         block_state_ptr      head_block_state()const;

         uint32_t             fork_db_head_block_num()const;
         block_id_type        fork_db_head_block_id()const;

         time_point                     pending_block_time()const;
         account_name                   pending_block_producer()const;
         const block_signing_authority& pending_block_signing_authority()const;
         std::optional<block_id_type>   pending_producer_block_id()const;
         uint32_t                       pending_block_num()const;

         const producer_authority_schedule&         active_producers()const;
         const producer_authority_schedule&         pending_producers()const;
         std::optional<producer_authority_schedule> proposed_producers()const;

         uint32_t last_irreversible_block_num() const;
         block_id_type last_irreversible_block_id() const;
         time_point last_irreversible_block_time() const;

         // thread-safe
         signed_block_ptr fetch_block_by_number( uint32_t block_num )const;
         // thread-safe
         signed_block_ptr fetch_block_by_id( const block_id_type& id )const;
         // thread-safe
         std::optional<signed_block_header> fetch_block_header_by_number( uint32_t block_num )const;
         // thread-safe
         std::optional<signed_block_header> fetch_block_header_by_id( const block_id_type& id )const;
         // return block_state from forkdb, thread-safe
         block_state_ptr fetch_block_state_by_number( uint32_t block_num )const;
         // return block_state from forkdb, thread-safe
         block_state_ptr fetch_block_state_by_id( block_id_type id )const;
         // thread-safe
         block_id_type get_block_id_for_num( uint32_t block_num )const;

         sha256 calculate_integrity_hash();
         void write_snapshot( const snapshot_writer_ptr& snapshot );

         bool sender_avoids_whitelist_blacklist_enforcement( account_name sender )const;
         void check_actor_list( const flat_set<account_name>& actors )const;
         void check_contract_list( account_name code )const;
         void check_action_list( account_name code, action_name action )const;
         void check_key_list( const public_key_type& key )const;
         bool is_building_block()const;
         bool is_speculative_block()const;

         bool is_ram_billing_in_notify_allowed()const;

         //This is only an accessor to the user configured subjective limit: i.e. it does not do a
         // check similar to is_ram_billing_in_notify_allowed() to check if controller is currently
         // producing a block
         uint32_t configured_subjective_signature_length_limit()const;

         void add_resource_greylist(const account_name &name);
         void remove_resource_greylist(const account_name &name);
         bool is_resource_greylisted(const account_name &name) const;
         const flat_set<account_name> &get_resource_greylist() const;

         void validate_expiration( const transaction& t )const;
         void validate_tapos( const transaction& t )const;
         void validate_db_available_size() const;

         bool is_protocol_feature_activated( const digest_type& feature_digest )const;
         bool is_builtin_activated( builtin_protocol_feature_t f )const;

         bool is_known_unexpired_transaction( const transaction_id_type& id) const;

         int64_t set_proposed_producers( vector<producer_authority> producers );

         bool light_validation_allowed() const;
         bool skip_auth_check()const;
         bool skip_trx_checks()const;
         bool skip_db_sessions()const;
         bool skip_db_sessions( block_status bs )const;
         bool is_trusted_producer( const account_name& producer) const;

         bool contracts_console()const;

         bool is_profiling(account_name name) const;

         chain_id_type get_chain_id()const;

         db_read_mode get_read_mode()const;
         validation_mode get_validation_mode()const;
         uint32_t get_terminate_at_block()const;

         void set_subjective_cpu_leeway(fc::microseconds leeway);
         std::optional<fc::microseconds> get_subjective_cpu_leeway() const;
         void set_greylist_limit( uint32_t limit );
         uint32_t get_greylist_limit()const;

         void add_to_ram_correction( account_name account, uint64_t ram_bytes );
         bool all_subjective_mitigations_disabled()const;

         deep_mind_handler* get_deep_mind_logger(bool is_trx_transient) const;
         void enable_deep_mind( deep_mind_handler* logger );
         uint32_t earliest_available_block_num() const;

#if defined(EOSIO_EOS_VM_RUNTIME_ENABLED) || defined(EOSIO_EOS_VM_JIT_RUNTIME_ENABLED)
         vm::wasm_allocator&  get_wasm_allocator();
         bool is_eos_vm_oc_enabled() const;
#endif

         static std::optional<uint64_t> convert_exception_to_error_code( const fc::exception& e );

         signal<void(uint32_t)>                        block_start; // block_num
         signal<void(const signed_block_ptr&)>         pre_accepted_block;
         signal<void(const block_state_ptr&)>          accepted_block_header;
         signal<void(const block_state_ptr&)>          accepted_block;
         signal<void(const block_state_ptr&)>          irreversible_block;
         signal<void(const transaction_metadata_ptr&)> accepted_transaction;
         signal<void(std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&>)> applied_transaction;
         signal<void(const int&)>                      bad_alloc;

         /*
         signal<void()>                                  pre_apply_block;
         signal<void()>                                  post_apply_block;
         signal<void()>                                  abort_apply_block;
         signal<void(const transaction_metadata_ptr&)>   pre_apply_transaction;
         signal<void(const transaction_trace_ptr&)>      post_apply_transaction;
         signal<void(const transaction_trace_ptr&)>  pre_apply_action;
         signal<void(const transaction_trace_ptr&)>  post_apply_action;
         */

         const apply_handler* find_apply_handler( account_name contract, scope_name scope, action_name act )const;
         wasm_interface& get_wasm_interface();


         std::optional<abi_serializer> get_abi_serializer( account_name n, const abi_serializer::yield_function_t& yield )const {
            if( n.good() ) {
               try {
                  const auto& a = get_account( n );
                  if( abi_def abi; abi_serializer::to_abi( a.abi, abi ))
                     return abi_serializer( std::move(abi), yield );
               } FC_CAPTURE_AND_LOG((n))
            }
            return std::optional<abi_serializer>();
         }

         template<typename T>
         fc::variant to_variant_with_abi( const T& obj, const abi_serializer::yield_function_t& yield )const {
            fc::variant pretty_output;
            abi_serializer::to_variant( obj, pretty_output,
                                        [&]( account_name n ){ return get_abi_serializer( n, yield ); }, yield );
            return pretty_output;
         }

      static chain_id_type extract_chain_id(snapshot_reader& snapshot);

      static std::optional<chain_id_type> extract_chain_id_from_db( const path& state_dir );

      void replace_producer_keys( const public_key_type& key );
      void replace_account_keys( name account, name permission, const public_key_type& key );

      void set_db_read_only_mode();
      void unset_db_read_only_mode();
      void init_thread_local_data();
      bool is_on_main_thread() const;

      private:
         friend class apply_context;
         friend class transaction_context;

         chainbase::database& mutable_db()const;

         std::unique_ptr<controller_impl> my;

   };

} }  /// eosio::chain
