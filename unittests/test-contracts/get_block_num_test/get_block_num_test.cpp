#include "get_block_num_test.hpp"

using namespace eosio;
[[eosio::action]]
void get_block_num_test::testblock(uint32_t expected_result) {
   uint32_t retBlock = eosio::internal_use_do_not_use::get_block_num();
   check( retBlock == expected_result , "result does not match");
}
