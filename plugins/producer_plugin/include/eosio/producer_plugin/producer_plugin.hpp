#pragma once

#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/signature_provider_plugin/signature_provider_plugin.hpp>

#include <appbase/application.hpp>

namespace eosio {

using boost::signals2::signal;

class producer_plugin : public appbase::plugin<producer_plugin> {
public:
   APPBASE_PLUGIN_REQUIRES((chain_plugin)(signature_provider_plugin))

   struct runtime_options {
      std::optional<int32_t>   max_transaction_time;
      std::optional<int32_t>   max_irreversible_block_age;
      std::optional<int32_t>   produce_time_offset_us;
      std::optional<int32_t>   last_block_time_offset_us;
      std::optional<int32_t>   max_scheduled_transaction_time_per_block_ms;
      std::optional<int32_t>   subjective_cpu_leeway_us;
      std::optional<double>    incoming_defer_ratio;
      std::optional<uint32_t>  greylist_limit;
   };

   struct whitelist_blacklist {
      std::optional< flat_set<account_name> > actor_whitelist;
      std::optional< flat_set<account_name> > actor_blacklist;
      std::optional< flat_set<account_name> > contract_whitelist;
      std::optional< flat_set<account_name> > contract_blacklist;
      std::optional< flat_set< std::pair<account_name, action_name> > > action_blacklist;
      std::optional< flat_set<public_key_type> > key_blacklist;
   };

   struct greylist_params {
      std::vector<account_name> accounts;
   };

   struct integrity_hash_information {
      chain::block_id_type head_block_id;
      chain::digest_type   integrity_hash;
   };

   struct snapshot_information {
      chain::block_id_type head_block_id;
      uint32_t             head_block_num;
      fc::time_point       head_block_time;
      uint32_t             version;
      std::string          snapshot_name;
   };

   struct scheduled_protocol_feature_activations {
      std::vector<chain::digest_type> protocol_features_to_activate;
   };

   struct get_supported_protocol_features_params {
      bool exclude_disabled = false;
      bool exclude_unactivatable = false;
   };

   struct get_account_ram_corrections_params {
      std::optional<account_name>  lower_bound;
      std::optional<account_name>  upper_bound;
      uint32_t                     limit = 10;
      bool                         reverse = false;
   };

   struct get_account_ram_corrections_result {
      std::vector<fc::variant>     rows;
      std::optional<account_name>  more;
   };

   template<typename T>
   using next_function = std::function<void(const std::variant<fc::exception_ptr, T>&)>;

   producer_plugin();
   virtual ~producer_plugin();

   virtual void set_program_options(
      boost::program_options::options_description &command_line_options,
      boost::program_options::options_description &config_file_options
      ) override;

   bool                   is_producer_key(const chain::public_key_type& key) const;
   chain::signature_type  sign_compact(const chain::public_key_type& key, const fc::sha256& digest) const;
   int64_t get_subjective_bill( const account_name& first_auth, const fc::time_point& now ) const;

   virtual void plugin_initialize(const boost::program_options::variables_map& options);
   virtual void plugin_startup();
   virtual void plugin_shutdown();
   void handle_sighup() override;

   void pause();
   void resume();
   bool paused() const;
   void update_runtime_options(const runtime_options& options);
   runtime_options get_runtime_options() const;

   void add_greylist_accounts(const greylist_params& params);
   void remove_greylist_accounts(const greylist_params& params);
   greylist_params get_greylist() const;

   whitelist_blacklist get_whitelist_blacklist() const;
   void set_whitelist_blacklist(const whitelist_blacklist& params);

   integrity_hash_information get_integrity_hash() const;
   void create_snapshot(next_function<snapshot_information> next);

   scheduled_protocol_feature_activations get_scheduled_protocol_feature_activations() const;
   void schedule_protocol_feature_activations(const scheduled_protocol_feature_activations& schedule);

   fc::variants get_supported_protocol_features( const get_supported_protocol_features_params& params ) const;

   get_account_ram_corrections_result  get_account_ram_corrections( const get_account_ram_corrections_params& params ) const;

   struct get_unapplied_transactions_params {
      string      lower_bound;  /// transaction id
      std::optional<uint32_t>    limit = 100;
      std::optional<uint32_t>    time_limit_ms; // defaults to 10ms
   };

   struct unapplied_trx {
      transaction_id_type       trx_id;
      fc::time_point_sec        expiration;
      string                    trx_type; // eosio::chain::trx_enum_type values or "read_only"
      account_name              first_auth;
      account_name              first_receiver;
      action_name               first_action;
      uint16_t                  total_actions = 0;
      uint32_t                  billed_cpu_time_us = 0;
      size_t                    size = 0;
   };

   struct get_unapplied_transactions_result {
      size_t                     size = 0;
      size_t                     incoming_size = 0;
      std::vector<unapplied_trx> trxs;
      string                     more; ///< fill lower_bound with trx id to fetch next set of transactions
   };

   get_unapplied_transactions_result get_unapplied_transactions( const get_unapplied_transactions_params& params, const fc::time_point& deadline ) const;


   void log_failed_transaction(const transaction_id_type& trx_id, const chain::packed_transaction_ptr& packed_trx_ptr, const char* reason) const;

   // thread-safe, called when a new block is received
   void received_block(uint32_t block_num);

 private:
   std::shared_ptr<class producer_plugin_impl> my;
};

} //eosio

FC_REFLECT(eosio::producer_plugin::runtime_options, (max_transaction_time)(max_irreversible_block_age)(produce_time_offset_us)(last_block_time_offset_us)(max_scheduled_transaction_time_per_block_ms)(subjective_cpu_leeway_us)(incoming_defer_ratio)(greylist_limit));
FC_REFLECT(eosio::producer_plugin::greylist_params, (accounts));
FC_REFLECT(eosio::producer_plugin::whitelist_blacklist, (actor_whitelist)(actor_blacklist)(contract_whitelist)(contract_blacklist)(action_blacklist)(key_blacklist) )
FC_REFLECT(eosio::producer_plugin::integrity_hash_information, (head_block_id)(integrity_hash))
FC_REFLECT(eosio::producer_plugin::snapshot_information, (head_block_id)(head_block_num)(head_block_time)(version)(snapshot_name))
FC_REFLECT(eosio::producer_plugin::scheduled_protocol_feature_activations, (protocol_features_to_activate))
FC_REFLECT(eosio::producer_plugin::get_supported_protocol_features_params, (exclude_disabled)(exclude_unactivatable))
FC_REFLECT(eosio::producer_plugin::get_account_ram_corrections_params, (lower_bound)(upper_bound)(limit)(reverse))
FC_REFLECT(eosio::producer_plugin::get_account_ram_corrections_result, (rows)(more))
FC_REFLECT(eosio::producer_plugin::get_unapplied_transactions_params, (lower_bound)(limit)(time_limit_ms))
FC_REFLECT(eosio::producer_plugin::unapplied_trx, (trx_id)(expiration)(trx_type)(first_auth)(first_receiver)(first_action)(total_actions)(billed_cpu_time_us)(size))
FC_REFLECT(eosio::producer_plugin::get_unapplied_transactions_result, (size)(incoming_size)(trxs)(more))
