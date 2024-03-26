#include "create_slim_account_test.hpp"

using namespace eosio;
[[eosio::action]]
void create_slim_account_test::testcreate(eosio::name creator, eosio::name account, authority active_auth ) {
   auto packed_authority = pack(active_auth);
   eosio::internal_use_do_not_use::create_slim_account(creator.value , account.value , packed_authority.data(), packed_authority.size());
}
