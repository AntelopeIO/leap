#include <eosio/producer_api_plugin/producer_api_plugin.hpp>
#include <eosio/chain/exceptions.hpp>

#include <fc/time.hpp>
#include <fc/variant.hpp>

#include <chrono>

namespace eosio { namespace detail {
  struct producer_api_plugin_response {
     std::string result;
  };
}}

FC_REFLECT(eosio::detail::producer_api_plugin_response, (result));

namespace eosio {

   static auto _producer_api_plugin = application::register_plugin<producer_api_plugin>();

using namespace eosio;

#define CALL_WITH_400(api_name, category, api_handle, call_name, INVOKE, http_response_code) \
{std::string("/v1/" #api_name "/" #call_name), \
   api_category::category, \
   [&](string&&, string&& body, url_response_callback&& cb) mutable { \
          try { \
             INVOKE \
             cb(http_response_code, fc::variant(result)); \
          } catch (...) { \
             http_plugin::handle_exception(#api_name, #call_name, body, cb); \
          } \
       }}

#define CALL_ASYNC(api_name, category, api_handle, call_name, call_result, INVOKE, http_response_code) \
{std::string("/v1/" #api_name "/" #call_name), \
   api_category::category, \
   [&api_handle](string&&, string&& body, url_response_callback&& cb) mutable { \
      if (body.empty()) body = "{}"; \
      auto next = [cb=std::move(cb), body=std::move(body)](const chain::next_function_variant<call_result>& result){ \
         if (std::holds_alternative<fc::exception_ptr>(result)) {\
            try {\
               std::get<fc::exception_ptr>(result)->dynamic_rethrow_exception();\
            } catch (...) {\
               http_plugin::handle_exception(#api_name, #call_name, body, cb);\
            }\
         } else if (std::holds_alternative<call_result>(result)) { \
            cb(http_response_code, fc::variant(std::get<call_result>(result)));\
         } else { \
            assert(0); \
         } \
      };\
      INVOKE\
   }\
}

#define INVOKE_R_R(api_handle, call_name, in_param) \
     auto params = parse_params<in_param, http_params_types::params_required>(body);\
     auto result = api_handle.call_name(std::move(params));

#define INVOKE_R_R_II(api_handle, call_name, in_param) \
     auto params = parse_params<in_param, http_params_types::possible_no_params>(body);\
     auto result = api_handle.call_name(std::move(params));

#define INVOKE_R_R_D(api_handle, call_name, in_param) \
     auto deadline = http_max_response_time == fc::microseconds::maximum() ? fc::time_point::maximum() \
                                                                           : fc::time_point::now() + http_max_response_time; \
     auto params = parse_params<in_param, http_params_types::possible_no_params>(body);\
     auto result = api_handle.call_name(std::move(params), deadline);

#define INVOKE_R_V(api_handle, call_name) \
     body = parse_params<std::string, http_params_types::no_params>(body); \
     auto result = api_handle.call_name();

#define INVOKE_R_V_ASYNC(api_handle, call_name)\
     api_handle.call_name(next);

#define INVOKE_V_R(api_handle, call_name, in_param) \
     auto params = parse_params<in_param, http_params_types::params_required>(body);\
     api_handle.call_name(std::move(params)); \
     eosio::detail::producer_api_plugin_response result{"ok"};

#define INVOKE_V_V(api_handle, call_name) \
     body = parse_params<std::string, http_params_types::no_params>(body); \
     api_handle.call_name(); \
     eosio::detail::producer_api_plugin_response result{"ok"};


void producer_api_plugin::plugin_startup() {
   ilog("starting producer_api_plugin");
   // lifetime of plugin is lifetime of application
   auto& producer = app().get_plugin<producer_plugin>();
   auto& http = app().get_plugin<http_plugin>();
   fc::microseconds http_max_response_time = http.get_max_response_time();

   app().get_plugin<http_plugin>().add_api({
       CALL_WITH_400(producer, producer_ro, producer, paused,
            INVOKE_R_V(producer, paused), 201),
       CALL_WITH_400(producer, producer_ro, producer, get_runtime_options,
            INVOKE_R_V(producer, get_runtime_options), 201),
       CALL_WITH_400(producer, producer_ro, producer, get_greylist,
            INVOKE_R_V(producer, get_greylist), 201),
       CALL_WITH_400(producer, producer_ro, producer, get_whitelist_blacklist,
            INVOKE_R_V(producer, get_whitelist_blacklist), 201),
       CALL_WITH_400(producer, producer_ro, producer, get_scheduled_protocol_feature_activations,
            INVOKE_R_V(producer, get_scheduled_protocol_feature_activations), 201),
       CALL_WITH_400(producer, producer_ro, producer, get_supported_protocol_features,
            INVOKE_R_R_II(producer, get_supported_protocol_features,
                                 producer_plugin::get_supported_protocol_features_params), 201),
       CALL_WITH_400(producer, producer_ro, producer, get_account_ram_corrections,
            INVOKE_R_R(producer, get_account_ram_corrections, producer_plugin::get_account_ram_corrections_params), 201),
       CALL_WITH_400(producer, producer_ro, producer, get_unapplied_transactions,
                     INVOKE_R_R_D(producer, get_unapplied_transactions, producer_plugin::get_unapplied_transactions_params), 200),
       CALL_WITH_400(producer, producer_ro, producer, get_snapshot_requests,
                     INVOKE_R_V(producer, get_snapshot_requests), 201),
   }, appbase::exec_queue::read_only, appbase::priority::medium_high);

   // Not safe to run in parallel
   app().get_plugin<http_plugin>().add_api({
       CALL_WITH_400(producer, producer_rw, producer, pause,
            INVOKE_V_V(producer, pause), 201),
       CALL_WITH_400(producer, producer_rw, producer, resume,
            INVOKE_V_V(producer, resume), 201),
       CALL_WITH_400(producer, producer_rw, producer, update_runtime_options,
            INVOKE_V_R(producer, update_runtime_options, producer_plugin::runtime_options), 201),
       CALL_WITH_400(producer, producer_rw, producer, add_greylist_accounts,
            INVOKE_V_R(producer, add_greylist_accounts, producer_plugin::greylist_params), 201),
       CALL_WITH_400(producer, producer_rw, producer, remove_greylist_accounts,
            INVOKE_V_R(producer, remove_greylist_accounts, producer_plugin::greylist_params), 201),
       CALL_WITH_400(producer, producer_rw, producer, set_whitelist_blacklist,
            INVOKE_V_R(producer, set_whitelist_blacklist, producer_plugin::whitelist_blacklist), 201),
       CALL_ASYNC(producer, snapshot, producer, create_snapshot, chain::snapshot_scheduler::snapshot_information,
            INVOKE_R_V_ASYNC(producer, create_snapshot), 201),
       CALL_WITH_400(producer, snapshot, producer, schedule_snapshot,
            INVOKE_R_R_II(producer, schedule_snapshot, chain::snapshot_scheduler::snapshot_request_params), 201),
       CALL_WITH_400(producer, snapshot, producer, unschedule_snapshot,
            INVOKE_R_R(producer, unschedule_snapshot, chain::snapshot_scheduler::snapshot_request_id_information), 201),
       CALL_WITH_400(producer, producer_rw, producer, get_integrity_hash,
            INVOKE_R_V(producer, get_integrity_hash), 201),
       CALL_WITH_400(producer, producer_rw, producer, schedule_protocol_feature_activations,
            INVOKE_V_R(producer, schedule_protocol_feature_activations, producer_plugin::scheduled_protocol_feature_activations), 201),
   }, appbase::exec_queue::read_write, appbase::priority::medium_high);
}

void producer_api_plugin::plugin_initialize(const variables_map& options) {
   try {
      const auto& _http_plugin = app().get_plugin<http_plugin>();
      if( !_http_plugin.is_on_loopback(api_category::producer_rw)) {
         wlog( "\n"
               "**********SECURITY WARNING**********\n"
               "*                                  *\n"
               "* --       Producer RW API      -- *\n"
               "* - EXPOSED to the LOCAL NETWORK - *\n"
               "* - USE ONLY ON SECURE NETWORKS! - *\n"
               "*                                  *\n"
               "************************************\n" );

      }
      if( !_http_plugin.is_on_loopback(api_category::snapshot)) {
         wlog( "\n"
               "**********SECURITY WARNING**********\n"
               "*                                  *\n"
               "* --         Snapshot API       -- *\n"
               "* - EXPOSED to the LOCAL NETWORK - *\n"
               "* - USE ONLY ON SECURE NETWORKS! - *\n"
               "*                                  *\n"
               "************************************\n" );

      }
   } FC_LOG_AND_RETHROW()
}


#undef INVOKE_R_R
#undef INVOKE_R_V
#undef INVOKE_V_R
#undef INVOKE_V_V
#undef CALL

}
