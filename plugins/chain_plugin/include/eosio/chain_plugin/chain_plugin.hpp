#pragma once
#include <eosio/chain/application.hpp>
#include <eosio/chain/asset.hpp>
#include <eosio/chain/authority.hpp>
#include <eosio/chain/account_object.hpp>
#include <eosio/chain/block.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/contract_table_objects.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/fixed_bytes.hpp>

#include <boost/container/flat_set.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <eosio/chain_plugin/account_query_db.hpp>
#include <eosio/chain_plugin/trx_retry_db.hpp>
#include <eosio/chain_plugin/trx_finality_status_processing.hpp>

#include <fc/static_variant.hpp>
#include <fc/time.hpp>

namespace fc { class variant; }

namespace eosio {
   namespace chain { class abi_resolver; }

   using chain::controller;
   using std::unique_ptr;
   using std::pair;
   using namespace appbase;
   using chain::name;
   using chain::uint128_t;
   using chain::public_key_type;
   using chain::transaction;
   using chain::transaction_id_type;
   using boost::container::flat_set;
   using chain::asset;
   using chain::symbol;
   using chain::authority;
   using chain::account_name;
   using chain::action_name;
   using chain::abi_def;
   using chain::abi_serializer;
   using chain::abi_serializer_cache_builder;
   using chain::abi_resolver;
   using chain::packed_transaction;

   enum class throw_on_yield { no, yes };
   inline auto make_resolver(const controller& control, fc::microseconds abi_serializer_max_time, throw_on_yield yield_throw ) {
      return [&control, abi_serializer_max_time, yield_throw](const account_name& name) -> std::optional<abi_serializer> {
         if (name.good()) {
            const auto* accnt = control.db().template find<chain::account_object, chain::by_name>( name );
            if( accnt != nullptr ) {
               try {
                  if( abi_def abi; abi_serializer::to_abi( accnt->abi, abi ) ) {
                     return abi_serializer( std::move( abi ), abi_serializer::create_yield_function( abi_serializer_max_time ) );
                  }
               } catch( ... ) {
                  if( yield_throw == throw_on_yield::yes )
                     throw;
               }
            }
         }
         return {};
      };
   }

   template<class T>
   inline abi_resolver get_serializers_cache(const controller& db, const T& obj, const fc::microseconds& max_time) {
      return abi_resolver(abi_serializer_cache_builder(make_resolver(db, max_time, throw_on_yield::no)).add_serializers(obj).get());
   }

namespace chain_apis {
struct empty{};

struct linked_action {
   name                account;
   std::optional<name> action;
};

struct permission {
   name                                       perm_name;
   name                                       parent;
   authority                                  required_auth;
   std::optional<std::vector<linked_action>>  linked_actions;
};


// see specializations for uint64_t and double in source file
template<typename Type>
Type convert_to_type(const string& str, const string& desc) {
   try {
      return fc::variant(str).as<Type>();
   } FC_RETHROW_EXCEPTIONS(warn, "Could not convert ${desc} string '${str}' to key type.", ("desc", desc)("str",str) )
}

uint64_t convert_to_type(const eosio::name &n, const string &desc);

template<>
uint64_t convert_to_type(const string& str, const string& desc);

template<>
double convert_to_type(const string& str, const string& desc);

template<typename Type>
string convert_to_string(const Type& source, const string& key_type, const string& encode_type, const string& desc);

template<>
string convert_to_string(const chain::key256_t& source, const string& key_type, const string& encode_type, const string& desc);

template<>
string convert_to_string(const float128_t& source, const string& key_type, const string& encode_type, const string& desc);

class read_write;
   
class api_base {
public:
   static constexpr uint32_t max_return_items = 1000;
   static void handle_db_exhaustion();
   static void handle_bad_alloc();

protected:
   struct send_transaction_params_t {
      bool return_failure_trace = true;
      bool retry_trx = false; ///< request transaction retry on validated transaction
      std::optional<uint16_t> retry_trx_num_blocks{}; ///< if retry_trx, report trace at specified blocks from executed or lib if not specified
      chain::transaction_metadata::trx_type trx_type;
      fc::variant transaction;
   };

   template<class API, class Result>
   static void send_transaction_gen(API& api, send_transaction_params_t params, chain::plugin_interface::next_function<Result> next);
};
   
class read_only : public api_base {
   const controller& db;
   const std::optional<account_query_db>& aqdb;
   const fc::microseconds abi_serializer_max_time;
   const fc::microseconds http_max_response_time;
   bool  shorten_abi_errors = true;
   const trx_finality_status_processing* trx_finality_status_proc;
   friend class api_base;
   
public:
   static const string KEYi64;

   read_only(const controller& db, const std::optional<account_query_db>& aqdb,
             const fc::microseconds& abi_serializer_max_time, const fc::microseconds& http_max_response_time,
             const trx_finality_status_processing* trx_finality_status_proc)
      : db(db)
      , aqdb(aqdb)
      , abi_serializer_max_time(abi_serializer_max_time)
      , http_max_response_time(http_max_response_time)
      , trx_finality_status_proc(trx_finality_status_proc) {
   }

   void validate() const {}

   // return deadline for call
   fc::time_point start() const {
      validate();
      return fc::time_point::now().safe_add(http_max_response_time);
   }

   void set_shorten_abi_errors( bool f ) { shorten_abi_errors = f; }

   using get_info_params = empty;

   struct get_info_results {
      string                               server_version;
      chain::chain_id_type                 chain_id;
      uint32_t                             head_block_num = 0;
      uint32_t                             last_irreversible_block_num = 0;
      chain::block_id_type                 last_irreversible_block_id;
      chain::block_id_type                 head_block_id;
      fc::time_point                       head_block_time;
      account_name                         head_block_producer;

      uint64_t                             virtual_block_cpu_limit = 0;
      uint64_t                             virtual_block_net_limit = 0;

      uint64_t                             block_cpu_limit = 0;
      uint64_t                             block_net_limit = 0;
      //string                               recent_slots;
      //double                               participation_rate = 0;
      std::optional<string>                server_version_string;
      std::optional<uint32_t>              fork_db_head_block_num;
      std::optional<chain::block_id_type>  fork_db_head_block_id;
      std::optional<string>                server_full_version_string;
      std::optional<uint64_t>              total_cpu_weight;
      std::optional<uint64_t>              total_net_weight;
      std::optional<uint32_t>              earliest_available_block_num;
      std::optional<fc::time_point>        last_irreversible_block_time;
   };
   get_info_results get_info(const get_info_params&, const fc::time_point& deadline) const;

   struct get_transaction_status_params {
      chain::transaction_id_type           id;
   };

   struct get_transaction_status_results {
      string                               state;
      std::optional<uint32_t>              block_number;
      std::optional<chain::block_id_type>  block_id;
      std::optional<fc::time_point>        block_timestamp;
      std::optional<fc::time_point>        expiration;
      uint32_t                             head_number = 0;
      chain::block_id_type                 head_id;
      fc::time_point                       head_timestamp;
      uint32_t                             irreversible_number = 0;
      chain::block_id_type                 irreversible_id;
      fc::time_point                       irreversible_timestamp;
      chain::block_id_type                 earliest_tracked_block_id;
      uint32_t                             earliest_tracked_block_number = 0;
   };
   get_transaction_status_results get_transaction_status(const get_transaction_status_params& params, const fc::time_point& deadline) const;


   struct get_activated_protocol_features_params {
      std::optional<uint32_t>  lower_bound;
      std::optional<uint32_t>  upper_bound;
      uint32_t                 limit = std::numeric_limits<uint32_t>::max(); // ignored
      bool                     search_by_block_num = false;
      bool                     reverse = false;
      std::optional<uint32_t>  time_limit_ms; // ignored
   };

   struct get_activated_protocol_features_results {
      fc::variants             activated_protocol_features;
      std::optional<uint32_t>  more;
   };

   get_activated_protocol_features_results
   get_activated_protocol_features( const get_activated_protocol_features_params& params, const fc::time_point& deadline )const;

   struct producer_info {
      name                       producer_name;
   };

   // account_resource_info holds similar data members as in account_resource_limit, but decoupling making them independently to be refactored in future
   struct account_resource_info {
      int64_t used = 0;
      int64_t available = 0;
      int64_t max = 0;
      std::optional<chain::block_timestamp_type> last_usage_update_time;    // optional for backward nodeos support
      std::optional<int64_t> current_used;  // optional for backward nodeos support
      void set( const eosio::chain::resource_limits::account_resource_limit& arl)
      {
         used = arl.used;
         available = arl.available;
         max = arl.max;
         last_usage_update_time = arl.last_usage_update_time;
         current_used = arl.current_used;
      }
   };

   struct get_account_results {
      name                       account_name;
      uint32_t                   head_block_num = 0;
      fc::time_point             head_block_time;

      bool                       privileged = false;
      fc::time_point             last_code_update;
      fc::time_point             created;

      std::optional<asset>       core_liquid_balance;

      int64_t                    ram_quota  = 0;
      int64_t                    net_weight = 0;
      int64_t                    cpu_weight = 0;

      account_resource_info      net_limit;
      account_resource_info      cpu_limit;
      int64_t                    ram_usage = 0;

      vector<permission>         permissions;

      fc::variant                total_resources;
      fc::variant                self_delegated_bandwidth;
      fc::variant                refund_request;
      fc::variant                voter_info;
      fc::variant                rex_info;

      std::optional<eosio::chain::resource_limits::account_resource_limit> subjective_cpu_bill_limit;
      std::vector<linked_action> eosio_any_linked_actions;
   };

   struct get_account_params {
      name                  account_name;
      std::optional<symbol> expected_core_symbol;
   };
   using get_account_return_t = std::function<chain::t_or_exception<get_account_results>()>;
   get_account_return_t get_account( const get_account_params& params, const fc::time_point& deadline )const;


   struct get_code_results {
      name                   account_name;
      string                 wast;
      string                 wasm;
      fc::sha256             code_hash;
      std::optional<abi_def> abi;
   };

   struct get_code_params {
      name account_name;
      bool code_as_wasm = true;
   };

   struct get_code_hash_results {
      name                   account_name;
      fc::sha256             code_hash;
   };

   struct get_code_hash_params {
      name account_name;
   };

   struct get_abi_results {
      name                   account_name;
      std::optional<abi_def> abi;
   };

   struct get_abi_params {
      name account_name;
   };

   struct get_raw_code_and_abi_results {
      name                   account_name;
      chain::blob            wasm;
      chain::blob            abi;
   };

   struct get_raw_code_and_abi_params {
      name                   account_name;
   };

   struct get_raw_abi_params {
      name                      account_name;
      std::optional<fc::sha256> abi_hash;
   };

   struct get_raw_abi_results {
      name                       account_name;
      fc::sha256                 code_hash;
      fc::sha256                 abi_hash;
      std::optional<chain::blob> abi;
   };


   get_code_results get_code( const get_code_params& params, const fc::time_point& deadline )const;
   get_code_hash_results get_code_hash( const get_code_hash_params& params, const fc::time_point& deadline )const;
   get_abi_results get_abi( const get_abi_params& params, const fc::time_point& deadline )const;
   get_raw_code_and_abi_results get_raw_code_and_abi( const get_raw_code_and_abi_params& params, const fc::time_point& deadline)const;
   get_raw_abi_results get_raw_abi( const get_raw_abi_params& params, const fc::time_point& deadline)const;


   struct get_required_keys_params {
      fc::variant transaction;
      flat_set<public_key_type> available_keys;
   };
   struct get_required_keys_result {
      flat_set<public_key_type> required_keys;
   };

   get_required_keys_result get_required_keys( const get_required_keys_params& params, const fc::time_point& deadline)const;

   using get_transaction_id_params = transaction;
   using get_transaction_id_result = transaction_id_type;

   get_transaction_id_result get_transaction_id( const get_transaction_id_params& params, const fc::time_point& deadline)const;

   struct get_raw_block_params {
      string block_num_or_id;
   };

   chain::signed_block_ptr get_raw_block(const get_raw_block_params& params, const fc::time_point& deadline) const;

   using get_block_params = get_raw_block_params;
   std::function<chain::t_or_exception<fc::variant>()> get_block(const get_block_params& params, const fc::time_point& deadline) const;

   // call from app() thread
   abi_resolver get_block_serializers( const chain::signed_block_ptr& block, const fc::microseconds& max_time ) const;

   // call from any thread
   fc::variant convert_block( const chain::signed_block_ptr& block,
                              abi_resolver& resolver ) const;

   struct get_block_header_params {
      string block_num_or_id;
      bool include_extensions = false; // include block extensions (requires reading entire block off disk)
   };

   struct get_block_header_result {
      chain::block_id_type  id;
      fc::variant           signed_block_header;
      std::optional<chain::extensions_type> block_extensions;
   };

   get_block_header_result get_block_header(const get_block_header_params& params, const fc::time_point& deadline) const;

   struct get_block_info_params {
      uint32_t block_num = 0;
   };

   fc::variant get_block_info(const get_block_info_params& params, const fc::time_point& deadline) const;

   struct get_block_header_state_params {
      string block_num_or_id;
   };

   fc::variant get_block_header_state(const get_block_header_state_params& params, const fc::time_point& deadline) const;

   struct get_table_rows_params {
      bool                 json = false;
      name                 code;
      string               scope;
      name                 table;
      string               table_key;
      string               lower_bound;
      string               upper_bound;
      uint32_t             limit = 10;
      string               key_type;  // type of key specified by index_position
      string               index_position; // 1 - primary (first), 2 - secondary index (in order defined by multi_index), 3 - third index, etc
      string               encode_type{"dec"}; //dec, hex , default=dec
      std::optional<bool>  reverse;
      std::optional<bool>  show_payer; // show RAM payer
      std::optional<uint32_t> time_limit_ms; // defaults to http-max-response-time-ms
    };

   struct get_table_rows_result {
      vector<fc::variant> rows; ///< one row per item, either encoded as hex String or JSON object
      bool                more = false; ///< true if last element in data is not the end and sizeof data() < limit
      string              next_key; ///< fill lower_bound with this value to fetch more rows
   };

   using get_table_rows_return_t = std::function<chain::t_or_exception<get_table_rows_result>()>;
   
   get_table_rows_return_t get_table_rows( const get_table_rows_params& params, const fc::time_point& deadline )const;

   struct get_table_by_scope_params {
      name                 code; // mandatory
      name                 table; // optional, act as filter
      string               lower_bound; // lower bound of scope, optional
      string               upper_bound; // upper bound of scope, optional
      uint32_t             limit = 10;
      std::optional<bool>  reverse;
      std::optional<uint32_t> time_limit_ms; // defaults to http-max-response-time-ms
   };
   struct get_table_by_scope_result_row {
      name        code;
      name        scope;
      name        table;
      name        payer;
      uint32_t    count = 0;
   };
   struct get_table_by_scope_result {
      vector<get_table_by_scope_result_row> rows;
      string      more; ///< fill lower_bound with this value to fetch more rows
   };

   get_table_by_scope_result get_table_by_scope( const get_table_by_scope_params& params, const fc::time_point& deadline )const;

   struct get_currency_balance_params {
      name                  code;
      name                  account;
      std::optional<string> symbol;
   };

   vector<asset> get_currency_balance( const get_currency_balance_params& params, const fc::time_point& deadline )const;

   struct get_currency_stats_params {
      name           code;
      string         symbol;
   };


   struct get_currency_stats_result {
      asset          supply;
      asset          max_supply;
      account_name   issuer;
   };

   fc::variant get_currency_stats( const get_currency_stats_params& params, const fc::time_point& deadline )const;

   struct get_producers_params {
      bool        json = false;
      string      lower_bound;
      uint32_t    limit = 50;
      std::optional<uint32_t> time_limit_ms; // defaults to http-max-response-time-ms
   };

   struct get_producers_result {
      vector<fc::variant> rows; ///< one row per item, either encoded as hex string or JSON object
      double              total_producer_vote_weight;
      string              more; ///< fill lower_bound with this value to fetch more rows
   };

   get_producers_result get_producers( const get_producers_params& params, const fc::time_point& deadline )const;

   struct get_producer_schedule_params {
   };

   struct get_producer_schedule_result {
      fc::variant active;
      fc::variant pending;
      fc::variant proposed;
   };

   get_producer_schedule_result get_producer_schedule( const get_producer_schedule_params& params, const fc::time_point& deadline )const;

   struct get_scheduled_transactions_params {
      bool        json = false;
      string      lower_bound;  /// timestamp OR transaction ID
      uint32_t    limit = 50;
      std::optional<uint32_t> time_limit_ms; // defaults to http-max-response-time-ms
   };

   struct get_scheduled_transactions_result {
      fc::variants  transactions;
      string        more; ///< fill lower_bound with this to fetch next set of transactions
   };

   get_scheduled_transactions_result get_scheduled_transactions( const get_scheduled_transactions_params& params, const fc::time_point& deadline ) const;
   struct compute_transaction_results {
       chain::transaction_id_type  transaction_id;
       fc::variant                 processed; // "processed" is expected JSON for trxs in cleos
    };

   struct compute_transaction_params {
      fc::variant transaction;
   };

   void compute_transaction(compute_transaction_params params, chain::plugin_interface::next_function<compute_transaction_results> next );

   struct send_read_only_transaction_results {
      chain::transaction_id_type  transaction_id;
      fc::variant                 processed;
   };
   struct send_read_only_transaction_params {
      fc::variant transaction;
   };
   void send_read_only_transaction(send_read_only_transaction_params params, chain::plugin_interface::next_function<send_read_only_transaction_results> next );

   static void copy_inline_row(const chain::key_value_object& obj, vector<char>& data) {
      data.resize( obj.value.size() );
      memcpy( data.data(), obj.value.data(), obj.value.size() );
   }

   template<typename Function>
   void walk_key_value_table(const name& code, const name& scope, const name& table, Function f) const
   {
      const auto& d = db.db();
      const auto* t_id = d.find<chain::table_id_object, chain::by_code_scope_table>(boost::make_tuple(code, scope, table));
      if (t_id != nullptr) {
         const auto &idx = d.get_index<chain::key_value_index, chain::by_scope_primary>();
         decltype(t_id->id) next_tid(t_id->id._id + 1);
         auto lower = idx.lower_bound(boost::make_tuple(t_id->id));
         auto upper = idx.lower_bound(boost::make_tuple(next_tid));

         for (auto itr = lower; itr != upper; ++itr) {
            if (!f(*itr)) {
               break;
            }
         }
      }
   }

   static uint64_t get_table_index_name(const read_only::get_table_rows_params& p, bool& primary);

   template <typename IndexType, typename SecKeyType, typename ConvFn>
   get_table_rows_return_t
   get_table_rows_by_seckey( const read_only::get_table_rows_params& p,
                             abi_def&& abi,
                             const fc::time_point& deadline,
                             ConvFn conv ) const {

      fc::time_point params_deadline = p.time_limit_ms ? std::min(fc::time_point::now().safe_add(fc::milliseconds(*p.time_limit_ms)), deadline) : deadline;

      struct http_params_t {
         name table;
         bool shorten_abi_errors;
         bool json;
         bool show_payer;
         bool more;
         std::string next_key;
         vector<std::pair<vector<char>, name>> rows;
      };
      
      http_params_t http_params { p.table, shorten_abi_errors, p.json, p.show_payer && *p.show_payer, false  };
         
      const auto& d = db.db();

      name scope{ convert_to_type<uint64_t>(p.scope, "scope") };

      bool primary = false;
      const uint64_t table_with_index = get_table_index_name(p, primary);
      const auto* t_id = d.find<chain::table_id_object, chain::by_code_scope_table>(boost::make_tuple(p.code, scope, p.table));
      const auto* index_t_id = d.find<chain::table_id_object, chain::by_code_scope_table>(boost::make_tuple(p.code, scope, name(table_with_index)));
      if( t_id != nullptr && index_t_id != nullptr ) {
         using secondary_key_type = std::invoke_result_t<decltype(conv), SecKeyType>;
         static_assert( std::is_same<typename IndexType::value_type::secondary_key_type, secondary_key_type>::value, "Return type of conv does not match type of secondary key for IndexType" );

         const auto& secidx = d.get_index<IndexType, chain::by_secondary>();
         auto lower_bound_lookup_tuple = std::make_tuple( index_t_id->id._id,
                                                          eosio::chain::secondary_key_traits<secondary_key_type>::true_lowest(),
                                                          std::numeric_limits<uint64_t>::lowest() );
         auto upper_bound_lookup_tuple = std::make_tuple( index_t_id->id._id,
                                                          eosio::chain::secondary_key_traits<secondary_key_type>::true_highest(),
                                                          std::numeric_limits<uint64_t>::max() );

         if( p.lower_bound.size() ) {
            if( p.key_type == "name" ) {
               if constexpr (std::is_same_v<uint64_t, SecKeyType>) {
                  SecKeyType lv = convert_to_type(name{p.lower_bound}, "lower_bound name");
                  std::get<1>(lower_bound_lookup_tuple) = conv(lv);
               } else {
                  EOS_ASSERT(false, chain::contract_table_query_exception, "Invalid key type of eosio::name ${nm} for lower bound", ("nm", p.lower_bound));
               }
            } else {
               SecKeyType lv = convert_to_type<SecKeyType>( p.lower_bound, "lower_bound" );
               std::get<1>(lower_bound_lookup_tuple) = conv( lv );
            }
         }

         if( p.upper_bound.size() ) {
            if( p.key_type == "name" ) {
               if constexpr (std::is_same_v<uint64_t, SecKeyType>) {
                  SecKeyType uv = convert_to_type(name{p.upper_bound}, "upper_bound name");
                  std::get<1>(upper_bound_lookup_tuple) = conv(uv);
               } else {
                  EOS_ASSERT(false, chain::contract_table_query_exception, "Invalid key type of eosio::name ${nm} for upper bound", ("nm", p.upper_bound));
               }
            } else {
               SecKeyType uv = convert_to_type<SecKeyType>( p.upper_bound, "upper_bound" );
               std::get<1>(upper_bound_lookup_tuple) = conv( uv );
            }
         }

         if( upper_bound_lookup_tuple < lower_bound_lookup_tuple )
            return []() ->  chain::t_or_exception<read_only::get_table_rows_result> {
               return read_only::get_table_rows_result();
            };

         auto walk_table_row_range = [&]( auto itr, auto end_itr ) {
            vector<char> data;
            uint32_t limit = p.limit;
            if (deadline != fc::time_point::maximum() && limit > max_return_items)
               limit = max_return_items;
            for( unsigned int count = 0; count < limit && itr != end_itr; ++count, ++itr ) {
               const auto* itr2 = d.find<chain::key_value_object, chain::by_scope_primary>( boost::make_tuple(t_id->id, itr->primary_key) );
               if( itr2 == nullptr ) continue;
               copy_inline_row(*itr2, data);
               http_params.rows.emplace_back(std::move(data), itr->payer);
               if (fc::time_point::now() >= params_deadline)
                  break;
            }
            if( itr != end_itr ) {
               http_params.more = true;
               http_params.next_key = convert_to_string(itr->secondary_key, p.key_type, p.encode_type, "next_key - next lower bound");
            }
         };

         auto lower = secidx.lower_bound( lower_bound_lookup_tuple );
         auto upper = secidx.upper_bound( upper_bound_lookup_tuple );
         if( p.reverse && *p.reverse ) {
            walk_table_row_range( boost::make_reverse_iterator(upper), boost::make_reverse_iterator(lower) );
         } else {
            walk_table_row_range( lower, upper );
         }
      }

      // not enforcing the deadline for that second processing part (the serialization), as it is not taking place
      // on the main thread, but in the http thread pool.
      return [p = std::move(http_params), abi=std::move(abi), abi_serializer_max_time=abi_serializer_max_time]() mutable ->
         chain::t_or_exception<read_only::get_table_rows_result> {
         read_only::get_table_rows_result result;
         abi_serializer abis;
         abis.set_abi(std::move(abi), abi_serializer::create_yield_function(abi_serializer_max_time));
         auto table_type = abis.get_table_type(p.table);
         
         for (auto& row : p.rows) {
            fc::variant data_var;
            if( p.json ) {
               data_var = abis.binary_to_variant(table_type, row.first,
                                                 abi_serializer::create_yield_function(abi_serializer_max_time),
                                                 p.shorten_abi_errors );
            } else {
               data_var = fc::variant(row.first);
            }

            if (p.show_payer) {
               result.rows.emplace_back(fc::mutable_variant_object("data", std::move(data_var))("payer", row.second));
            } else {
               result.rows.emplace_back(std::move(data_var));
            }            
         }
         result.more = p.more;
         result.next_key = p.next_key;
         return result;
      };
   }

   template <typename IndexType>
   get_table_rows_return_t
   get_table_rows_ex( const read_only::get_table_rows_params& p,
                      abi_def&& abi,
                      const fc::time_point& deadline ) const {

      fc::time_point params_deadline = p.time_limit_ms ? std::min(fc::time_point::now().safe_add(fc::milliseconds(*p.time_limit_ms)), deadline) : deadline;

      struct http_params_t {
         name table;
         bool shorten_abi_errors;
         bool json;
         bool show_payer;
         bool more;
         std::string next_key;
         vector<std::pair<vector<char>, name>> rows;
      };
      
      http_params_t http_params { p.table, shorten_abi_errors, p.json, p.show_payer && *p.show_payer, false  };
         
      const auto& d = db.db();

      uint64_t scope = convert_to_type<uint64_t>(p.scope, "scope");

      const auto* t_id = d.find<chain::table_id_object, chain::by_code_scope_table>(boost::make_tuple(p.code, name(scope), p.table));
      if( t_id != nullptr ) {
         const auto& idx = d.get_index<IndexType, chain::by_scope_primary>();
         auto lower_bound_lookup_tuple = std::make_tuple( t_id->id, std::numeric_limits<uint64_t>::lowest() );
         auto upper_bound_lookup_tuple = std::make_tuple( t_id->id, std::numeric_limits<uint64_t>::max() );

         if( p.lower_bound.size() ) {
            if( p.key_type == "name" ) {
               name s(p.lower_bound);
               std::get<1>(lower_bound_lookup_tuple) = s.to_uint64_t();
            } else {
               auto lv = convert_to_type<typename IndexType::value_type::key_type>( p.lower_bound, "lower_bound" );
               std::get<1>(lower_bound_lookup_tuple) = lv;
            }
         }

         if( p.upper_bound.size() ) {
            if( p.key_type == "name" ) {
               name s(p.upper_bound);
               std::get<1>(upper_bound_lookup_tuple) = s.to_uint64_t();
            } else {
               auto uv = convert_to_type<typename IndexType::value_type::key_type>( p.upper_bound, "upper_bound" );
               std::get<1>(upper_bound_lookup_tuple) = uv;
            }
         }

         if( upper_bound_lookup_tuple < lower_bound_lookup_tuple  )
            return []() ->  chain::t_or_exception<read_only::get_table_rows_result> {
               return read_only::get_table_rows_result();
            };

         auto walk_table_row_range = [&]( auto itr, auto end_itr ) {
            vector<char> data;
            uint32_t limit = p.limit;
            if (deadline != fc::time_point::maximum() && limit > max_return_items)
               limit = max_return_items;
            for( unsigned int count = 0; count < limit && itr != end_itr; ++count, ++itr ) {
               copy_inline_row(*itr, data);
               http_params.rows.emplace_back(std::move(data), itr->payer);
               if (fc::time_point::now() >= params_deadline)
                  break;
            }
            if( itr != end_itr ) {
               http_params.more = true;
               http_params.next_key = convert_to_string(itr->primary_key, p.key_type, p.encode_type, "next_key - next lower bound");
            }
         };

         auto lower = idx.lower_bound( lower_bound_lookup_tuple );
         auto upper = idx.upper_bound( upper_bound_lookup_tuple );
         if( p.reverse && *p.reverse ) {
            walk_table_row_range( boost::make_reverse_iterator(upper), boost::make_reverse_iterator(lower) );
         } else {
            walk_table_row_range( lower, upper );
         }
      }
      
      // not enforcing the deadline for that second processing part (the serialization), as it is not taking place
      // on the main thread, but in the http thread pool.
      return [p = std::move(http_params), abi=std::move(abi), abi_serializer_max_time=abi_serializer_max_time]() mutable ->
         chain::t_or_exception<read_only::get_table_rows_result> {
         read_only::get_table_rows_result result;
         abi_serializer abis;
         abis.set_abi(std::move(abi), abi_serializer::create_yield_function(abi_serializer_max_time));
         auto table_type = abis.get_table_type(p.table);
         
         for (auto& row : p.rows) {
            fc::variant data_var;
            if( p.json ) {
               data_var = abis.binary_to_variant(table_type, row.first,
                                                 abi_serializer::create_yield_function(abi_serializer_max_time),
                                                 p.shorten_abi_errors );
            } else {
               data_var = fc::variant(row.first);
            }

            if (p.show_payer) {
               result.rows.emplace_back(fc::mutable_variant_object("data", std::move(data_var))("payer", row.second));
            } else {
               result.rows.emplace_back(std::move(data_var));
            }            
         }
         result.more = p.more;
         result.next_key = p.next_key;
         return result;
      };
   }

   using get_accounts_by_authorizers_result = account_query_db::get_accounts_by_authorizers_result;
   using get_accounts_by_authorizers_params = account_query_db::get_accounts_by_authorizers_params;
   get_accounts_by_authorizers_result get_accounts_by_authorizers( const get_accounts_by_authorizers_params& args, const fc::time_point& deadline) const;

   chain::symbol extract_core_symbol()const;

   using get_consensus_parameters_params = empty;
   struct get_consensus_parameters_results {
     chain::chain_config               chain_config;
     std::optional<chain::wasm_config> wasm_config;
   };
   get_consensus_parameters_results get_consensus_parameters(const get_consensus_parameters_params&, const fc::time_point& deadline) const;
};

class read_write : public api_base {
   controller& db;
   std::optional<trx_retry_db>& trx_retry;
   const fc::microseconds abi_serializer_max_time;
   const fc::microseconds http_max_response_time;
   const bool api_accept_transactions;
   friend class api_base;
   
public:
   read_write(controller& db, std::optional<trx_retry_db>& trx_retry,
              const fc::microseconds& abi_serializer_max_time, const fc::microseconds& http_max_response_time,
              bool api_accept_transactions);
   void validate() const;

   // return deadline for call
   fc::time_point start() const {
      validate();
      return http_max_response_time == fc::microseconds::maximum() ? fc::time_point::maximum()
                                                                   : fc::time_point::now() + http_max_response_time;
   }

   using push_block_params = chain::signed_block;
   using push_block_results = empty;
   void push_block(push_block_params&& params, chain::plugin_interface::next_function<push_block_results> next);

   using push_transaction_params = fc::variant_object;
   struct push_transaction_results {
      chain::transaction_id_type  transaction_id;
      fc::variant                 processed; // "processed" is expected JSON for trxs in cleos
   };
   void push_transaction(const push_transaction_params& params, chain::plugin_interface::next_function<push_transaction_results> next);


   using push_transactions_params  = vector<push_transaction_params>;
   using push_transactions_results = vector<push_transaction_results>;
   void push_transactions(const push_transactions_params& params, chain::plugin_interface::next_function<push_transactions_results> next);

   using send_transaction_params = push_transaction_params;
   using send_transaction_results = push_transaction_results;
   void send_transaction(send_transaction_params params, chain::plugin_interface::next_function<send_transaction_results> next);

   struct send_transaction2_params {
      bool return_failure_trace = true;
      bool retry_trx = false; ///< request transaction retry on validated transaction
      std::optional<uint16_t> retry_trx_num_blocks{}; ///< if retry_trx, report trace at specified blocks from executed or lib if not specified
      fc::variant transaction;
   };
   void send_transaction2(send_transaction2_params params, chain::plugin_interface::next_function<send_transaction_results> next);

};

 //support for --key_types [sha256,ripemd160] and --encoding [dec/hex]
 constexpr const char i64[]       = "i64";
 constexpr const char i128[]      = "i128";
 constexpr const char i256[]      = "i256";
 constexpr const char float64[]   = "float64";
 constexpr const char float128[]  = "float128";
 constexpr const char sha256[]    = "sha256";
 constexpr const char ripemd160[] = "ripemd160";
 constexpr const char dec[]       = "dec";
 constexpr const char hex[]       = "hex";


 template<const char*key_type , const char *encoding=chain_apis::dec>
 struct keytype_converter ;

 template<>
 struct keytype_converter<chain_apis::sha256, chain_apis::hex> {
     using input_type = chain::checksum256_type;
     using index_type = chain::index256_index;
     static auto function() {
        return [](const input_type& v) {
            // The input is in big endian, i.e. f58262c8005bb64b8f99ec6083faf050c502d099d9929ae37ffed2fe1bb954fb
            // fixed_bytes will convert the input to array of 2 uint128_t in little endian, i.e. 50f0fa8360ec998f4bb65b00c86282f5 fb54b91bfed2fe7fe39a92d999d002c5
            // which is the format used by secondary index
            uint8_t buffer[32];
            memcpy(buffer, v.data(), 32);
            fixed_bytes<32> fb(buffer);
            return chain::key256_t(fb.get_array());
        };
     }
 };

 //key160 support with padding zeros in the end of key256
 template<>
 struct keytype_converter<chain_apis::ripemd160, chain_apis::hex> {
     using input_type = chain::checksum160_type;
     using index_type = chain::index256_index;
     static auto function() {
        return [](const input_type& v) {
            // The input is in big endian, i.e. 83a83a3876c64c33f66f33c54f1869edef5b5d4a000000000000000000000000
            // fixed_bytes will convert the input to array of 2 uint128_t in little endian, i.e. ed69184fc5336ff6334cc676383aa883 0000000000000000000000004a5d5bef
            // which is the format used by secondary index
            uint8_t buffer[20];
            memcpy(buffer, v.data(), 20);
            fixed_bytes<20> fb(buffer);
            return chain::key256_t(fb.get_array());
        };
     }
 };

 template<>
 struct keytype_converter<chain_apis::i256> {
     using input_type = boost::multiprecision::uint256_t;
     using index_type = chain::index256_index;
     static auto function() {
        return [](const input_type v) {
            // The input is in little endian of uint256_t, i.e. fb54b91bfed2fe7fe39a92d999d002c550f0fa8360ec998f4bb65b00c86282f5
            // the following will convert the input to array of 2 uint128_t in little endian, i.e. 50f0fa8360ec998f4bb65b00c86282f5 fb54b91bfed2fe7fe39a92d999d002c5
            // which is the format used by secondary index
            chain::key256_t k;
            uint8_t buffer[32] = {};
            boost::multiprecision::export_bits(v, buffer, 8, false);
            memcpy(&k[0], buffer + 16, 16);
            memcpy(&k[1], buffer, 16);
            return k;
        };
     }
 };

 template<typename I>
 std::string itoh(I n, size_t hlen = sizeof(I)<<1) {
     static const char* digits = "0123456789abcdef";
     std::string r(hlen, '0');
     for(size_t i = 0, j = (hlen - 1) * 4 ; i < hlen; ++i, j -= 4)
         r[i] = digits[(n>>j) & 0x0f];
     return r;
 }

} // namespace chain_apis

class chain_plugin : public plugin<chain_plugin> {
public:
   APPBASE_PLUGIN_REQUIRES()

   chain_plugin();
   virtual ~chain_plugin();

   virtual void set_program_options(options_description& cli, options_description& cfg) override;

   void plugin_initialize(const variables_map& options);
   void plugin_startup();
   void plugin_shutdown();
   void handle_sighup() override;

   chain_apis::read_write get_read_write_api(const fc::microseconds& http_max_response_time);
   chain_apis::read_only get_read_only_api(const fc::microseconds& http_max_response_time) const;

   bool accept_block( const chain::signed_block_ptr& block, const chain::block_id_type& id, const chain::block_state_legacy_ptr& bsp );
   void accept_transaction(const chain::packed_transaction_ptr& trx, chain::plugin_interface::next_function<chain::transaction_trace_ptr> next);

   // Only call this after plugin_initialize()!
   controller& chain();
   // Only call this after plugin_initialize()!
   const controller& chain() const;

   chain::chain_id_type get_chain_id() const;
   fc::microseconds get_abi_serializer_max_time() const;
   bool api_accept_transactions() const;
   // set true by other plugins if any plugin allows transactions
   bool accept_transactions() const;
   void enable_accept_transactions();

   static void handle_guard_exception(const chain::guard_exception& e);

   bool account_queries_enabled() const;
   bool transaction_finality_status_enabled() const;

   // return variant of trace for logging, trace is modified to minimize log output
   fc::variant get_log_trx_trace(const chain::transaction_trace_ptr& trx_trace) const;
   // return variant of trx for logging, trace is modified to minimize log output
   fc::variant get_log_trx(const transaction& trx) const;

   const controller::config& chain_config() const;
private:

   unique_ptr<class chain_plugin_impl> my;
};

} // namespace eosio

FC_REFLECT( eosio::chain_apis::linked_action, (account)(action) )
FC_REFLECT( eosio::chain_apis::permission, (perm_name)(parent)(required_auth)(linked_actions) )
FC_REFLECT(eosio::chain_apis::empty, )
FC_REFLECT(eosio::chain_apis::read_only::get_info_results,
           (server_version)(chain_id)(head_block_num)(last_irreversible_block_num)(last_irreversible_block_id)
           (head_block_id)(head_block_time)(head_block_producer)
           (virtual_block_cpu_limit)(virtual_block_net_limit)(block_cpu_limit)(block_net_limit)
           (server_version_string)(fork_db_head_block_num)(fork_db_head_block_id)(server_full_version_string)
           (total_cpu_weight)(total_net_weight)(earliest_available_block_num)(last_irreversible_block_time))
FC_REFLECT(eosio::chain_apis::read_only::get_transaction_status_params, (id) )
FC_REFLECT(eosio::chain_apis::read_only::get_transaction_status_results, (state)(block_number)(block_id)(block_timestamp)(expiration)(head_number)(head_id)
           (head_timestamp)(irreversible_number)(irreversible_id)(irreversible_timestamp)(earliest_tracked_block_id)(earliest_tracked_block_number) )
FC_REFLECT(eosio::chain_apis::read_only::get_activated_protocol_features_params, (lower_bound)(upper_bound)(limit)(search_by_block_num)(reverse)(time_limit_ms) )
FC_REFLECT(eosio::chain_apis::read_only::get_activated_protocol_features_results, (activated_protocol_features)(more) )
FC_REFLECT(eosio::chain_apis::read_only::get_raw_block_params, (block_num_or_id))
FC_REFLECT(eosio::chain_apis::read_only::get_block_info_params, (block_num))
FC_REFLECT(eosio::chain_apis::read_only::get_block_header_state_params, (block_num_or_id))
FC_REFLECT(eosio::chain_apis::read_only::get_block_header_params, (block_num_or_id)(include_extensions))
FC_REFLECT(eosio::chain_apis::read_only::get_block_header_result, (id)(signed_block_header)(block_extensions))

FC_REFLECT( eosio::chain_apis::read_write::push_transaction_results, (transaction_id)(processed) )
FC_REFLECT( eosio::chain_apis::read_write::send_transaction2_params, (return_failure_trace)(retry_trx)(retry_trx_num_blocks)(transaction) )

FC_REFLECT( eosio::chain_apis::read_only::get_table_rows_params, (json)(code)(scope)(table)(table_key)(lower_bound)(upper_bound)(limit)(key_type)(index_position)(encode_type)(reverse)(show_payer)(time_limit_ms) )
FC_REFLECT( eosio::chain_apis::read_only::get_table_rows_result, (rows)(more)(next_key) );

FC_REFLECT( eosio::chain_apis::read_only::get_table_by_scope_params, (code)(table)(lower_bound)(upper_bound)(limit)(reverse)(time_limit_ms) )
FC_REFLECT( eosio::chain_apis::read_only::get_table_by_scope_result_row, (code)(scope)(table)(payer)(count));
FC_REFLECT( eosio::chain_apis::read_only::get_table_by_scope_result, (rows)(more) );

FC_REFLECT( eosio::chain_apis::read_only::get_currency_balance_params, (code)(account)(symbol));
FC_REFLECT( eosio::chain_apis::read_only::get_currency_stats_params, (code)(symbol));
FC_REFLECT( eosio::chain_apis::read_only::get_currency_stats_result, (supply)(max_supply)(issuer));

FC_REFLECT( eosio::chain_apis::read_only::get_producers_params, (json)(lower_bound)(limit)(time_limit_ms) )
FC_REFLECT( eosio::chain_apis::read_only::get_producers_result, (rows)(total_producer_vote_weight)(more) );

FC_REFLECT_EMPTY( eosio::chain_apis::read_only::get_producer_schedule_params )
FC_REFLECT( eosio::chain_apis::read_only::get_producer_schedule_result, (active)(pending)(proposed) );

FC_REFLECT( eosio::chain_apis::read_only::get_scheduled_transactions_params, (json)(lower_bound)(limit)(time_limit_ms) )
FC_REFLECT( eosio::chain_apis::read_only::get_scheduled_transactions_result, (transactions)(more) );

FC_REFLECT( eosio::chain_apis::read_only::account_resource_info, (used)(available)(max)(last_usage_update_time)(current_used) )
FC_REFLECT( eosio::chain_apis::read_only::get_account_results,
            (account_name)(head_block_num)(head_block_time)(privileged)(last_code_update)(created)
            (core_liquid_balance)(ram_quota)(net_weight)(cpu_weight)(net_limit)(cpu_limit)(ram_usage)(permissions)
            (total_resources)(self_delegated_bandwidth)(refund_request)(voter_info)(rex_info)
            (subjective_cpu_bill_limit) (eosio_any_linked_actions) )
// @swap code_hash
FC_REFLECT( eosio::chain_apis::read_only::get_code_results, (account_name)(code_hash)(wast)(wasm)(abi) )
FC_REFLECT( eosio::chain_apis::read_only::get_code_hash_results, (account_name)(code_hash) )
FC_REFLECT( eosio::chain_apis::read_only::get_abi_results, (account_name)(abi) )
FC_REFLECT( eosio::chain_apis::read_only::get_account_params, (account_name)(expected_core_symbol) )
FC_REFLECT( eosio::chain_apis::read_only::get_code_params, (account_name)(code_as_wasm) )
FC_REFLECT( eosio::chain_apis::read_only::get_code_hash_params, (account_name) )
FC_REFLECT( eosio::chain_apis::read_only::get_abi_params, (account_name) )
FC_REFLECT( eosio::chain_apis::read_only::get_raw_code_and_abi_params, (account_name) )
FC_REFLECT( eosio::chain_apis::read_only::get_raw_code_and_abi_results, (account_name)(wasm)(abi) )
FC_REFLECT( eosio::chain_apis::read_only::get_raw_abi_params, (account_name)(abi_hash) )
FC_REFLECT( eosio::chain_apis::read_only::get_raw_abi_results, (account_name)(code_hash)(abi_hash)(abi) )
FC_REFLECT( eosio::chain_apis::read_only::producer_info, (producer_name) )
FC_REFLECT( eosio::chain_apis::read_only::get_required_keys_params, (transaction)(available_keys) )
FC_REFLECT( eosio::chain_apis::read_only::get_required_keys_result, (required_keys) )
FC_REFLECT( eosio::chain_apis::read_only::compute_transaction_params, (transaction))
FC_REFLECT( eosio::chain_apis::read_only::compute_transaction_results, (transaction_id)(processed) )
FC_REFLECT( eosio::chain_apis::read_only::send_read_only_transaction_params, (transaction))
FC_REFLECT( eosio::chain_apis::read_only::send_read_only_transaction_results, (transaction_id)(processed) )
FC_REFLECT( eosio::chain_apis::read_only::get_consensus_parameters_results, (chain_config)(wasm_config))
