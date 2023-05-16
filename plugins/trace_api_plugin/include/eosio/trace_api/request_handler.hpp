#pragma once

#include <fc/variant.hpp>
#include <eosio/trace_api/metadata_log.hpp>
#include <eosio/trace_api/data_log.hpp>
#include <eosio/trace_api/common.hpp>

namespace eosio::trace_api {
   using data_handler_function = std::function<std::tuple<fc::variant, std::optional<fc::variant>>( const std::variant<action_trace_v0, action_trace_v1> & action_trace_t)>;

   namespace detail {
      class response_formatter {
      public:
         static fc::variant process_block( const data_log_entry& trace, bool irreversible, const data_handler_function& data_handler );
      };
   }

   template<typename LogfileProvider, typename DataHandlerProvider>
   class request_handler {
   public:
      request_handler(LogfileProvider&& logfile_provider, DataHandlerProvider&& data_handler_provider, log_handler log)
      :logfile_provider(std::move(logfile_provider))
      ,data_handler_provider(std::move(data_handler_provider))
      ,_log(log)
      {
         _log("Constructed request_handler");
      }

      /**
       * Fetch the trace for a given block height and convert it to a fc::variant for conversion to a final format
       * (eg JSON)
       *
       * @param block_height - the height of the block whose trace is requested
       * @return a properly formatted variant representing the trace for the given block height if it exists, an
       * empty variant otherwise.
       * @throws bad_data_exception when there are issues with the underlying data preventing processing.
       */
      fc::variant get_block_trace( uint32_t block_height ) {
         auto data = logfile_provider.get_block(block_height);
         if (!data) {
            _log("No block found at block height " + std::to_string(block_height) );
            return {};
         }

         auto data_handler = [this](const auto& action) -> std::tuple<fc::variant, std::optional<fc::variant>> {
            return std::visit([&](const auto& action_trace_t) {
               return data_handler_provider.serialize_to_variant(action_trace_t);
            }, action);
         };

         return detail::response_formatter::process_block(std::get<0>(*data), std::get<1>(*data), data_handler);
      }

      /**
       * Fetch the trace for a given transaction id and convert it to a fc::variant for conversion to a final format
       * (eg JSON)
       *
       * @param trxid - the transaction id whose trace is requested
       * @param block_height - the height of the block whose trace contains requested transaction trace
       * @return a properly formatted variant representing the trace for the given transaction id if it exists, an
       * empty variant otherwise.
       * @throws bad_data_exception when there are issues with the underlying data preventing processing.
       */
      fc::variant get_transaction_trace(chain::transaction_id_type trxid, uint32_t block_height){
         _log("get_transaction_trace called" );
         fc::variant result = {};
         // extract the transaction trace from the block trace
         auto resp = get_block_trace(block_height);
         if (!resp.is_null()) {
            auto& b_mvo = resp.get_object();
            if (b_mvo.contains("transactions")) {
               auto& transactions = b_mvo["transactions"];
               std::string input_id = trxid.str();
               for (uint32_t i = 0; i < transactions.size(); ++i) {
                  if (transactions[i].is_null()) continue;
                  auto& t_mvo = transactions[i].get_object();
                  if (t_mvo.contains("id")) {
                     const auto& t_id = t_mvo["id"].get_string();
                     if (t_id == input_id) {
                        result = transactions[i];
                        break;
                     }
                  }
               }
               if( result.is_null() )
                  _log("Exhausted all " + std::to_string(transactions.size()) + " transactions in block " + b_mvo["number"].as_string() + " without finding trxid " + trxid.str());
            }
         }
         return result;
      }

   private:
      LogfileProvider logfile_provider;
      DataHandlerProvider data_handler_provider;
      log_handler _log;
   };


}
