#pragma once

#include <appbase/channel.hpp>
#include <appbase/method.hpp>

#include <eosio/chain/block.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/transaction_metadata.hpp>
#include <eosio/chain/trace.hpp>

namespace eosio { namespace chain { namespace plugin_interface {
   using namespace eosio::chain;
   using namespace appbase;

   //
   // prometheus metrics
   //

   enum class metric_type {
      gauge = 1,
      counter = 2
   };

   struct runtime_metric {
      metric_type type = metric_type::gauge;
      std::string family;
      std::string label;
      int64_t value = 0;
   };

   using metrics_listener = std::function<void(std::vector<runtime_metric>)>;

   struct plugin_metrics {

      virtual ~plugin_metrics() = default;
      virtual vector<runtime_metric> metrics()=0;

      bool should_post() {
         return (_listener && (fc::time_point::now() > (_last_post + _min_post_interval_us)));
      }

      bool post_metrics() {
         if (should_post()){
            _listener(metrics());
            _last_post = fc::time_point::now();
            return true;
         }

         return false;
      }

      void register_listener(metrics_listener listener) {
         _listener = std::move(listener);
      }

      explicit plugin_metrics(fc::microseconds min_post_interval_us=fc::milliseconds(250))
      : _min_post_interval_us(min_post_interval_us)
      , _listener(nullptr) {}

   private:
      fc::microseconds _min_post_interval_us;
      metrics_listener _listener;
      fc::time_point _last_post;
   };

   //
   // channel & method interfaces
   //

   template<typename T>
   using next_function = std::function<void(const std::variant<fc::exception_ptr, T>&)>;

   struct chain_plugin_interface;

   namespace channels {
      using pre_accepted_block     = channel_decl<struct pre_accepted_block_tag,    signed_block_ptr>;
      using rejected_block         = channel_decl<struct rejected_block_tag,        signed_block_ptr>;
      using accepted_block_header  = channel_decl<struct accepted_block_header_tag, block_state_ptr>;
      using accepted_block         = channel_decl<struct accepted_block_tag,        block_state_ptr>;
      using irreversible_block     = channel_decl<struct irreversible_block_tag,    block_state_ptr>;
      using accepted_transaction   = channel_decl<struct accepted_transaction_tag,  transaction_metadata_ptr>;
      using applied_transaction    = channel_decl<struct applied_transaction_tag,   transaction_trace_ptr>;
   }

   namespace methods {
      using get_block_by_number    = method_decl<chain_plugin_interface, signed_block_ptr(uint32_t block_num)>;
      using get_block_by_id        = method_decl<chain_plugin_interface, signed_block_ptr(const block_id_type& block_id)>;
      using get_head_block_id      = method_decl<chain_plugin_interface, block_id_type ()>;
      using get_lib_block_id       = method_decl<chain_plugin_interface, block_id_type ()>;

      using get_last_irreversible_block_number = method_decl<chain_plugin_interface, uint32_t ()>;
   }

   namespace incoming {
      namespace methods {
         // synchronously push a block/trx to a single provider, block_state_ptr may be null
         using block_sync            = method_decl<chain_plugin_interface, bool(const signed_block_ptr&, const std::optional<block_id_type>&, const block_state_ptr&), first_provider_policy>;
         using transaction_async     = method_decl<chain_plugin_interface, void(const packed_transaction_ptr&, bool, transaction_metadata::trx_type, bool, next_function<transaction_trace_ptr>), first_provider_policy>;
      }
   }

   namespace compat {
      namespace channels {
         using transaction_ack       = channel_decl<struct accepted_transaction_tag, std::pair<fc::exception_ptr, packed_transaction_ptr>>;
      }
   }

} } }
