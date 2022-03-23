#include <eosio/chain_plugin/trx_retry_processing.hpp>


using namespace eosio;

namespace eosio::chain_apis {

   trx_retry_processing::trx_retry_processing( uint64_t max_storage )
   {
   }

   chain::block_interface_ptr trx_retry_processing::get_block_processor() const {
#warning TODO need to add real block_interface implementation
      return chain::block_interface_ptr{};
   }

   chain::trx_interface_ptr trx_retry_processing::get_in_block_trx_processor() const {
#warning TODO need to add real trx_interface implementation
      return chain::trx_interface_ptr{};
   }

   chain::trx_interface_ptr trx_retry_processing::get_speculative_trx_processor() const {
      // trx retry does not care about speculative block transactions
      return chain::trx_interface_ptr{new chain::no_op_processor};
   }

   chain::trx_interface_ptr trx_retry_processing::get_local_trx_processor() const {
#warning TODO need to add real trx_interface implementation
      return chain::trx_interface_ptr{};
   }
}
