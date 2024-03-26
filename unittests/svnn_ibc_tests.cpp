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

#include <eosio/chain/exceptions.hpp>
using mvo = mutable_variant_object;


BOOST_AUTO_TEST_SUITE(svnn_ibc)

// Extending the default chain tester
// ( libraries/testing/include/eosio/testing/tester.hpp )
class ibc_tester : public tester {
public:
   const account_name   _bridge = "bridge"_n;

   // This is mostly for creating accounts and loading contracts.
   void setup(){
      // load bridge contract
      create_account( _bridge );
      set_code( _bridge, eosio::testing::test_contracts::svnn_ibc_wasm());
      set_abi( _bridge, eosio::testing::test_contracts::svnn_ibc_abi());
   }

   //set a finalizer policy
   void set_policy(){
      auto cr = push_action( _bridge, "setfpolicy"_n, _bridge, mutable_variant_object()
         ("from_block_num", 1)
         ("policy", mutable_variant_object() 
            ("generation", 1)
            ("fthreshold", 3)
            ("last_block_num", 0)
            ("finalizers", fc::variants({
               mutable_variant_object() 
                  ("description","finalizer1")
                  ("fweight", 1)
                  ("public_key", "b12eba13063c6cdc7bbe40e7de62a1c0f861a9ad55e924cdd5049be9b58e205053968179cede5be79afdcbbb90322406aefb7a5ce64edc2a4482d8656daed1eeacfb4286f661c0f9117dcd83fad451d301b2310946e5cd58808f7b441b280a02")
               ,
               mutable_variant_object() 
                  ("description","finalizer2")
                  ("fweight", 1)
                  ("public_key", "0728121cffe7b8ddac41817c3a6faca76ae9de762d9c26602f936ac3e283da756002d3671a2858f54c355f67b31b430b23b957dba426d757eb422db617be4cc13daf41691aa059b0f198fa290014d3c3e4fa1def2abc6a3328adfa7705c75508")
               ,
               mutable_variant_object() 
                  ("description","finalizer3")
                  ("fweight", 1)
                  ("public_key", "e06c31c83f70b4fe9507877563bfff49235774d94c98dbf9673d61d082ef589f7dd4865281f37d60d1bb433514d4ef0b787424fb5e53472b1d45d28d90614fad29a4e5e0fe70ea387f7845e22c843f6061f9be20a7af21d8b72d02f4ca494a0a")
               ,
               mutable_variant_object() 
                  ("description","finalizer4")
                  ("fweight", 1)
                  ("public_key", "08c9bd408bac02747e493d918e4b3e6bd1a2ffaf9bfca4f2e79dd22e12556bf46e911f25613c24d9f6403996c5246c19ef94aff48094868425eda1e46bcd059c59f3b060521be797f5cc2e6debe2180efa12c0814618a38836a64c3d7440740f")
            }))
         )
      );
   }

   //verify a proof
   void check_proof(){
      auto cr = push_action( _bridge, "checkproof"_n, _bridge, mutable_variant_object()
         ("proof", mutable_variant_object() 
            ("finality_proof", mutable_variant_object() 
               ("qc_block", mutable_variant_object()
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("finalizer_policy_generation", 1)
                  ("witness_hash", "888ceeb757ea240d1c1ae2f4f717e67b73dcd592b2ba097f63b4c3e3ca4350e1")
                  ("finality_mroot", "1d2ab7379301370d3fa1b27a9f4ac077f6ea445a1aa3dbf7e18e9cc2c25b140c")
               )
               ("qc", mutable_variant_object()
                  ("signature", "")
                  ("finalizers", fc::variants({}))
               )
            )
            ("target_block_proof_of_inclusion", mutable_variant_object() 
               ("target_node_index", 7)
               ("last_node_index", 7)
               ("target", fc::variants({"block_data", mutable_variant_object() 
                  ("finality_data", mutable_variant_object() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("finalizer_policy_generation", 1)
                     ("witness_hash", "dff620c1c4d31cade95ed609269a86d4ecb2357f9302d17675c0665c75786508")
                     ("finality_mroot", "1397eb7c86719f160188fa740fc3610ccb5a6681ad56807dc99a17fe73a7b7fd")
                  )
                  ("dynamic_data", mutable_variant_object() 
                     ("block_num", 28)
                     ("action_proofs", fc::variants())
                     ("action_mroot", "4e890ef0e014f93bd1b31fabf1041ecc9fb1c44e957c2f7b1682333ee426677a")
                  )
               }))
               ("merkle_branches", fc::variants({
                  mutable_variant_object() 
                     ("direction", 1)
                     ("hash", "4e17da018040c80339f2714828d1927d5b616f9af7aa4768c1876df6f05e5602")
                  ,
                  mutable_variant_object() 
                     ("direction", 1)
                     ("hash", "7ee0e16f1941fb5a98d80d20ca92e0c689e9284285d5f90ecd4f8f1ea2ffb53c")
                  ,
                  mutable_variant_object() 
                     ("direction", 1)
                     ("hash", "401526ba03ec4a955c83cda131dacd3e89becaad2cf04107170e436dd90a553f")
               }))
            )
         )
      );
   }
};

BOOST_AUTO_TEST_CASE( first_test ) try {
   
   ibc_tester chain_a;

   chain_a.setup();

   chain_a.set_policy();
   chain_a.check_proof();

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()