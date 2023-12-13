/*

  IF IBC unit test (WIP)

  ====================
  in leap compilation:
  ====================

  cmake .. -DEOSIO_COMPILE_TEST_CONTRACTS=1

  (or compile wasm/abi separately and copy them over to unittests/test-contracts/ibc_test)

  ====================
  running:
  ====================

  ctest -R ibc

*/

// From fork tests
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/testing/tester.hpp>

#include <eosio/chain/fork_database.hpp>

#include <fc/variant_object.hpp>

#include <boost/test/unit_test.hpp>

#include <contracts.hpp>
#include <test_contracts.hpp>

#include "fork_test_utilities.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

// From params_test
#include <eosio/chain/exceptions.hpp>
using mvo = mutable_variant_object;


BOOST_AUTO_TEST_SUITE(ibc_tests)

// These are the producers and finalizers to use across all chains
const std::vector<account_name> test_nodes =
{
   "a"_n, "b"_n, "c"_n, "d"_n, "e"_n,
   "f"_n, "g"_n, "h"_n, "i"_n, "j"_n,
   "k"_n, "l"_n, "m"_n, "n"_n, "o"_n,
   "p"_n, "q"_n, "r"_n, "s"_n, "t"_n,
   "u"_n
};

// Extending the default chain tester
// ( libraries/testing/include/eosio/testing/tester.hpp )
class ibc_tester : public tester {
public:
   const account_name   _bridge = "bridge"_n;
   std::string          _tokenstr;

   // tester() should run the full setup policy, which should load the system contract
   ibc_tester(const std::string tokenstr) : tester(), _tokenstr(tokenstr) {}

   ibc_tester(const std::string tokenstr, setup_policy policy) : tester(policy), _tokenstr(tokenstr) {}

   eosio::chain::asset token_from_string(const std::string& s) {
      return eosio::chain::asset::from_string(s + " " + _tokenstr);
   }

   // This is mostly for creating accounts and loading contracts.
   void setup(){

      // load bridge contract
      create_account( _bridge );
      set_code( _bridge, eosio::testing::test_contracts::ibc_test_wasm());
      set_abi( _bridge, eosio::testing::test_contracts::ibc_test_abi());

      // check that we can call the test contract
      auto cr = push_action( _bridge, "hi"_n, _bridge, mutable_variant_object()
                             ("nm",       "testname"_n )
         );

      // load token contract
      create_account( "eosio.token"_n );
      set_code( "eosio.token"_n, test_contracts::eosio_token_wasm() );
      set_abi( "eosio.token"_n, test_contracts::eosio_token_abi() );

      // issue system token
      cr = push_action( "eosio.token"_n, "create"_n, "eosio.token"_n, mutable_variant_object()
                             ("issuer",       "eosio" )
                             ("maximum_supply", token_from_string("10000000.0000"))
         );

      // token transfer to the system account "eosio"
      cr = push_action( "eosio.token"_n, "issue"_n, config::system_account_name, mutable_variant_object()
                        ("to",       "eosio" )
                        ("quantity", token_from_string("100.0000"))
                        ("memo", "")
         );

      // create and set producers
      create_accounts( test_nodes );
      set_producers( test_nodes );

      // create and set finalizers (this should take care of the finalizer policy step)
      //
      //finalizer_policy policy_a = [...] //some finalizer policy
      //finalizer_policy policy_b = [...] //some different finalizer policy
      set_finalizers( test_nodes );

      // TODO: should ensure a number of blocks is produced until the finalizer policy
      //       is guaranteed to have taken effect

      produce_block();
   }

   // TODO: method to produce a block with a specific QC

   // TODO: heavyproof extract_heavy_proof_data( ...

   // TODO: lightproof extract_light_proof_data( ...

   // TODO: void check_heavy_proof( ...

   // TODO: void check_light_proof( ...
};

BOOST_AUTO_TEST_CASE( first_test ) try {

   ibc_tester chain_a("AAA");
   ibc_tester chain_b("BBB");

   chain_a.setup();

   // TODO: This is lifted from existing testcases. How would it work with IF?
   // run A until the producers are installed and its the start of the first node's round
   BOOST_REQUIRE( produce_until_transition( chain_a, test_nodes.back(), test_nodes.front() ) );

   chain_b.setup();

   // TODO: This is lifted from existing testcases. How would it work with IF?
   // run B until the producers are installed and its the start of the first node's round
   BOOST_REQUIRE( produce_until_transition( chain_b, test_nodes.back(), test_nodes.front() ) );

   // TODO: here, specific blocks, QCs, etc. would be injected in chain_a and chain_b;
   //       probably some support from tester / ibc_tester is required.
   //
   //       of potential interest:  (in tester.hpp)
   //       void                 push_block(signed_block_ptr b);
   //
   //       Either there's support for low-level crafting of the QCs and blocks into
   //         the chain, or higher-level block production takes care of it, that is,
   //         under ideal circumstances, finality is achieved in a block height that is known
   //         by the test after N rounds of production, and then we just fetch the input data
   //         for proofs at the expected blocks, package and submit them to the ibc_test
   //         contract. Depends on what exactly we want to test in each case.
   //
   //chain_a.new_block_without_qc(1) //produce a block at height 1 without a qc
   //chain_a.new_block_with_qc(2, 1) //produce a block at height 2 with a qc on block 1
   //chain_a.new_block_with_qc(3, 2) //etc
   //chain_a.new_block_with_qc(4, 3)
   //chain_a.new_block_with_qc(5, 4)
   //chain_a.new_block_with_qc(6, 5)
   //chain_a.new_block_with_qc(7, 6)
   //chain_a.new_block_with_qc(8, 7)

   // TODO: here, the tester / ibc_tester provides chain data to be sent to the ibc_test
   //       contract (unittests/test-contracts/ibc_test) (controller?)
   //
   //heavyproof h_proof = chain_a.extract_heavy_proof_data(5)
   //lightproof l_proof = chain_a.extract_light_proof_data(3)

   // TODO: chain_b.checkproofa(h_proof) and chain_b.checkproofb(l_proof)
   //       are push_action() calls (that succeed if the contract doesn't throw an error) .
   //
   // chain_b.check_heavy_proof(h_proof);
   // chain_b.check_light_proof(l_proof);


} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
