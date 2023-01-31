#include <eosio/chain_api_plugin/chain_api_plugin.hpp>
#include <eosio/chain/exceptions.hpp>

#include <fc/io/json.hpp>

namespace eosio {

static appbase::abstract_plugin& _chain_api_plugin = app().register_plugin<chain_api_plugin>();

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

struct async_result_visitor : public fc::visitor<fc::variant> {
   template<typename T>
   fc::variant operator()(const T& v) const {
      return fc::variant(v);
   }
};

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
   [api_handle](string, string body, url_response_callback cb) mutable { \
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

#define CALL_ASYNC_WITH_400(api_name, api_handle, api_namespace, call_name, call_result, http_response_code, params_type) \
{std::string("/v1/" #api_name "/" #call_name), \
   [api_handle](string, string body, url_response_callback cb) mutable { \
      auto deadline = api_handle.start(); \
      try { \
         auto params = parse_params<api_namespace::call_name ## _params, params_type>(body);\
         FC_CHECK_DEADLINE(deadline);\
         api_handle.call_name( std::move(params), \
            [cb, body](const std::variant<fc::exception_ptr, call_result>& result){\
               if (std::holds_alternative<fc::exception_ptr>(result)) {\
                  try {\
                     std::get<fc::exception_ptr>(result)->dynamic_rethrow_exception();\
                  } catch (...) {\
                     http_plugin::handle_exception(#api_name, #call_name, body, cb);\
                  }\
               } else {\
                  cb(http_response_code, fc::time_point::maximum(), std::visit(async_result_visitor(), result));\
               }\
            });\
      } catch (...) { \
         http_plugin::handle_exception(#api_name, #call_name, body, cb); \
      } \
   }\
}

#define CHAIN_RO_CALL(call_name, http_response_code, params_type) CALL_WITH_400(chain, ro_api, chain_apis::read_only, call_name, http_response_code, params_type)
#define CHAIN_RW_CALL(call_name, http_response_code, params_type) CALL_WITH_400(chain, rw_api, chain_apis::read_write, call_name, http_response_code, params_type)
#define CHAIN_RO_CALL_ASYNC(call_name, call_result, http_response_code, params_type) CALL_ASYNC_WITH_400(chain, ro_api, chain_apis::read_only, call_name, call_result, http_response_code, params_type)
#define CHAIN_RW_CALL_ASYNC(call_name, call_result, http_response_code, params_type) CALL_ASYNC_WITH_400(chain, rw_api, chain_apis::read_write, call_name, call_result, http_response_code, params_type)

#define CHAIN_RO_CALL_WITH_400(call_name, http_response_code, params_type) CALL_WITH_400(chain, ro_api, chain_apis::read_only, call_name, http_response_code, params_type)

void chain_api_plugin::plugin_startup() {
   ilog( "starting chain_api_plugin" );
   my.reset(new chain_api_plugin_impl(app().get_plugin<chain_plugin>().chain()));
   auto& chain = app().get_plugin<chain_plugin>();
   auto& http = app().get_plugin<http_plugin>();
   fc::microseconds max_response_time = http.get_max_response_time();

   auto ro_api = chain.get_read_only_api(max_response_time);
   auto rw_api = chain.get_read_write_api(max_response_time);

   auto& _http_plugin = app().get_plugin<http_plugin>();
   ro_api.set_shorten_abi_errors( !http_plugin::verbose_errors() );

   _http_plugin.add_api( {
      CHAIN_RO_CALL(get_info, 200, http_params_types::no_params)}, appbase::priority::medium_high);
   _http_plugin.add_api({
      CHAIN_RO_CALL(get_activated_protocol_features, 200, http_params_types::possible_no_params),
      CHAIN_RO_CALL(get_block, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_block_info, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_block_header_state, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_account, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_code, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_code_hash, 200, http_params_types::params_required),
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
      CHAIN_RO_CALL(abi_json_to_bin, 200, http_params_types::params_required),
      CHAIN_RO_CALL(abi_bin_to_json, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_required_keys, 200, http_params_types::params_required),
      CHAIN_RO_CALL(get_transaction_id, 200, http_params_types::params_required),
      CHAIN_RO_CALL_ASYNC(compute_transaction, chain_apis::read_only::compute_transaction_results, 200, http_params_types::params_required),
      CHAIN_RW_CALL_ASYNC(push_block, chain_apis::read_write::push_block_results, 202, http_params_types::params_required),
      CHAIN_RW_CALL_ASYNC(push_transaction, chain_apis::read_write::push_transaction_results, 202, http_params_types::params_required),
      CHAIN_RW_CALL_ASYNC(push_transactions, chain_apis::read_write::push_transactions_results, 202, http_params_types::params_required),
      CHAIN_RW_CALL_ASYNC(send_transaction, chain_apis::read_write::send_transaction_results, 202, http_params_types::params_required),
      CHAIN_RW_CALL_ASYNC(send_transaction2, chain_apis::read_write::send_transaction_results, 202, http_params_types::params_required),
      CHAIN_RO_CALL(get_consensus_parameters, 200, http_params_types::no_params)
   });

   if (chain.account_queries_enabled()) {
      _http_plugin.add_async_api({
         CHAIN_RO_CALL_WITH_400(get_accounts_by_authorizers, 200, http_params_types::params_required),
      });
   }

   if (chain.transaction_finality_status_enabled()) {
      _http_plugin.add_api({
         CHAIN_RO_CALL_WITH_400(get_transaction_status, 200, http_params_types::params_required),
      });
   }
}

void chain_api_plugin::plugin_shutdown() {}

}
