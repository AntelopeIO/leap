#include <eosio/chain_api_plugin/chain_api_plugin.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/http_plugin/macros.hpp>
#include <fc/io/json.hpp>

namespace eosio {

   static auto _chain_api_plugin = application::register_plugin<chain_api_plugin>();

using namespace eosio;

class chain_api_plugin_impl {
public:
   chain_api_plugin_impl(controller& db)
      : db(db) {}

   controller& db;
};


chain_api_plugin::chain_api_plugin(){}
chain_api_plugin::~chain_api_plugin(){}

void chain_api_plugin::set_program_options(options_description&, options_description&) {}
void chain_api_plugin::plugin_initialize(const variables_map&) {}

// Only want a simple 'Invalid transaction id' if unable to parse the body
template<>
chain_apis::read_only::get_transaction_status_params
parse_params<chain_apis::read_only::get_transaction_status_params, http_params_types::params_required>(const std::string& body) {
   if (body.empty()) {
      EOS_THROW(chain::invalid_http_request, "A Request body is required");
   }

   try {
      auto v = fc::json::from_string( body ).as<chain_apis::read_only::get_transaction_status_params>();
      if( v.id == transaction_id_type() ) throw false;
      return v;
   } catch( ... ) {
      EOS_THROW(chain::invalid_http_request, "Invalid transaction id");
   }
}

#define CALL_WITH_400(api_name, api_handle, api_namespace, call_name, http_response_code, params_type) \
{std::string("/v1/" #api_name "/" #call_name), \
   [api_handle](string&&, string&& body, url_response_callback&& cb) mutable { \
          auto deadline = api_handle.start(); \
          try { \
             auto params = parse_params<api_namespace::call_name ## _params, params_type>(body);\
             FC_CHECK_DEADLINE(deadline);\
             fc::variant result( api_handle.call_name( std::move(params), deadline ) ); \
             cb(http_response_code, deadline, std::move(result)); \
          } catch (...) { \
             http_plugin::handle_exception(#api_name, #call_name, body, cb); \
          } \
       }}

#define CHAIN_RO_CALL(call_name, http_response_code, params_type) CALL_WITH_400(chain, ro_api, chain_apis::read_only, call_name, http_response_code, params_type)
#define CHAIN_RW_CALL(call_name, http_response_code, params_type) CALL_WITH_400(chain, rw_api, chain_apis::read_write, call_name, http_response_code, params_type)
#define CHAIN_RO_CALL_ASYNC(call_name, call_result, http_response_code, params_type) CALL_ASYNC_WITH_400(chain, ro_api, chain_apis::read_only, call_name, call_result, http_response_code, params_type)
#define CHAIN_RW_CALL_ASYNC(call_name, call_result, http_response_code, params_type) CALL_ASYNC_WITH_400(chain, rw_api, chain_apis::read_write, call_name, call_result, http_response_code, params_type)

#define CHAIN_RO_CALL_WITH_400(call_name, http_response_code, params_type) CALL_WITH_400(chain, ro_api, chain_apis::read_only, call_name, http_response_code, params_type)

void chain_api_plugin::plugin_startup() {
   ilog( "starting chain_api_plugin" );
   my.reset(new chain_api_plugin_impl(app().get_plugin<chain_plugin>().chain()));
   auto& chain = app().get_plugin<chain_plugin>();
   auto& _http_plugin = app().get_plugin<http_plugin>();
   fc::microseconds max_response_time = _http_plugin.get_max_response_time();

   auto ro_api = chain.get_read_only_api(max_response_time);
   auto rw_api = chain.get_read_write_api(max_response_time);

   ro_api.set_shorten_abi_errors( !http_plugin::verbose_errors() );

   _http_plugin.add_api( {
      CHAIN_RO_CALL(get_info, 200, http_params_types::no_params)}, appbase::exec_queue::read_only, appbase::priority::medium_high);
   _http_plugin.add_api({
      CHAIN_RO_CALL(get_activated_protocol_features, 200, http_params_types::possible_no_params),
      CHAIN_RO_CALL(get_block_info, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_block_header_state, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_account, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_code, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_code_hash, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_consensus_parameters, 200, http_params_types::no_params),
      CHAIN_RO_CALL(get_abi, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_raw_code_and_abi, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_raw_abi, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_table_rows, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_table_by_scope, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_currency_balance, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_currency_stats, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_producers, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_producer_schedule, 200, http_params_types::no_params),
      CHAIN_RO_CALL(get_scheduled_transactions, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_required_keys, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_transaction_id, 200, http_params_types::params_required),
      // transaction related APIs will be posted to read_write queue after keys are recovered, they are safe to run in parallel until they post to the read_write queue
      CHAIN_RO_CALL_ASYNC(send_read_only_transaction, chain_apis::read_only::send_read_only_transaction_results, 200, http_params_types::params_required),
      CHAIN_RO_CALL_ASYNC(compute_transaction, chain_apis::read_only::compute_transaction_results, 200, http_params_types::params_required),
      CHAIN_RW_CALL_ASYNC(push_transaction, chain_apis::read_write::push_transaction_results, 202, http_params_types::params_required),
      CHAIN_RW_CALL_ASYNC(push_transactions, chain_apis::read_write::push_transactions_results, 202, http_params_types::params_required),
      CHAIN_RW_CALL_ASYNC(send_transaction, chain_apis::read_write::send_transaction_results, 202, http_params_types::params_required),
      CHAIN_RW_CALL_ASYNC(send_transaction2, chain_apis::read_write::send_transaction_results, 202, http_params_types::params_required)
   }, appbase::exec_queue::read_only);

   // Not safe to run in parallel with read-only transactions
   _http_plugin.add_api({
      CHAIN_RW_CALL_ASYNC(push_block, chain_apis::read_write::push_block_results, 202, http_params_types::params_required)
   }, appbase::exec_queue::read_write, appbase::priority::medium_low );

   if (chain.account_queries_enabled()) {
      _http_plugin.add_async_api({
         CHAIN_RO_CALL_WITH_400(get_accounts_by_authorizers, 200, http_params_types::params_required),
      });
   }

   _http_plugin.add_async_api({
      CHAIN_RO_CALL_WITH_400(get_raw_block, 200, http_params_types::params_required),
      CHAIN_RO_CALL_WITH_400(get_block_header, 200, http_params_types::params_required)
   });

   if (chain.transaction_finality_status_enabled()) {
      _http_plugin.add_api({
         CHAIN_RO_CALL_WITH_400(get_transaction_status, 200, http_params_types::params_required),
      }, appbase::exec_queue::read_only);
   }

   _http_plugin.add_api({
      { std::string("/v1/chain/get_block"),
        [ro_api, &_http_plugin, max_time=std::min(chain.get_abi_serializer_max_time(),max_response_time)]
              ( string&&, string&& body, url_response_callback&& cb ) mutable {
           auto deadline = ro_api.start();
           try {
              auto start = fc::time_point::now();
              auto params = parse_params<chain_apis::read_only::get_raw_block_params, http_params_types::params_required>(body);
              FC_CHECK_DEADLINE( deadline );
              chain::signed_block_ptr block = ro_api.get_raw_block( params, deadline );

              auto abi_cache = ro_api.get_block_serializers( block, max_time );
              FC_CHECK_DEADLINE( deadline );

              auto post_time = fc::time_point::now();
              auto remaining_time = max_time - (post_time - start);
              _http_plugin.post_http_thread_pool(
                    [ro_api, cb, deadline, post_time, remaining_time, abi_cache{std::move(abi_cache)}, block{std::move( block )}]() mutable {
                 try {
                    auto new_deadline = deadline + (fc::time_point::now() - post_time);

                    fc::variant result = ro_api.convert_block( block, std::move(abi_cache), remaining_time );

                    cb( 200, new_deadline, std::move( result ) );
                 } catch( ... ) {
                    http_plugin::handle_exception( "chain", "get_block", "", cb );
                 }
              } );
           } catch( ... ) {
              http_plugin::handle_exception("chain", "get_block", body, cb);
           }
        }
      }
   }, appbase::exec_queue::read_only);
}

void chain_api_plugin::plugin_shutdown() {}

}
