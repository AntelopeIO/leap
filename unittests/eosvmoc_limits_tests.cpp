#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED

#include <eosio/testing/tester.hpp>
#include <test_contracts.hpp>
#include <boost/test/unit_test.hpp>

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using mvo = fc::mutable_variant_object;

BOOST_AUTO_TEST_SUITE(eosvmoc_limits_tests)

// common routine to verify wasm_execution_error is raised when a resource
// limit specified in eosvmoc_config is reached
void limit_violated_test(const eosvmoc::config& eosvmoc_config) {
   fc::temp_directory tempdir;

   constexpr bool use_genesis = true;
   validating_tester chain(
      tempdir,
      [&](controller::config& cfg) {
         cfg.eosvmoc_config = eosvmoc_config;
      },
      use_genesis
   );

   chain.create_accounts({"eosio.token"_n});
   chain.set_code("eosio.token"_n, test_contracts::eosio_token_wasm());
   chain.set_abi("eosio.token"_n, test_contracts::eosio_token_abi());

   if (chain.control->is_eos_vm_oc_enabled()) {
      BOOST_CHECK_EXCEPTION(
         chain.push_action( "eosio.token"_n, "create"_n, "eosio.token"_n, mvo()
            ( "issuer", "eosio.token" )
            ( "maximum_supply", "1000000.00 TOK" )),
         eosio::chain::wasm_execution_error,
         [](const eosio::chain::wasm_execution_error& e) {
            return expect_assert_message(e, "failed to compile wasm");
         }
      );
   } else {
      chain.push_action( "eosio.token"_n, "create"_n, "eosio.token"_n, mvo()
         ( "issuer", "eosio.token" )
         ( "maximum_supply", "1000000.00 TOK" )
      );
   }
}

// common routine to verify no wasm_execution_error is raised
// because limits specified in eosvmoc_config are not reached
void limit_not_violated_test(const eosvmoc::config& eosvmoc_config) {
   fc::temp_directory tempdir;

   constexpr bool use_genesis = true;
   validating_tester chain(
      tempdir,
      [&](controller::config& cfg) {
         cfg.eosvmoc_config = eosvmoc_config;
      },
      use_genesis
   );

   chain.create_accounts({"eosio.token"_n});
   chain.set_code("eosio.token"_n, test_contracts::eosio_token_wasm());
   chain.set_abi("eosio.token"_n, test_contracts::eosio_token_abi());

   chain.push_action( "eosio.token"_n, "create"_n, "eosio.token"_n, mvo()
      ( "issuer", "eosio.token" )
      ( "maximum_supply", "1000000.00 TOK" )
   );
}

// test libtester does not enforce limits
BOOST_AUTO_TEST_CASE( default_limits ) { try {
   eosvmoc::config eosvmoc_config;
   limit_not_violated_test(eosvmoc_config);
} FC_LOG_AND_RETHROW() }

// test VM limits are checked
BOOST_AUTO_TEST_CASE( vm_limits ) { try {
   eosvmoc::config eosvmoc_config;

   // disable non-tested limits
   eosvmoc_config.cpu_limits.rlim_cur       = RLIM_INFINITY;
   eosvmoc_config.stack_size_limit          = std::numeric_limits<uint64_t>::max();
   eosvmoc_config.generated_code_size_limit = std::numeric_limits<size_t>::max();

   // set vm_limit to a small value such that it is exceeded
   eosvmoc_config.vm_limits.rlim_cur = 64u*1024u*1024u;
   limit_violated_test(eosvmoc_config);

   // set vm_limit to a large value such that it is not exceeded
   eosvmoc_config.vm_limits.rlim_cur = 128u*1024u*1024u;
   limit_not_violated_test(eosvmoc_config);
} FC_LOG_AND_RETHROW() }

// test stack size limit is checked
BOOST_AUTO_TEST_CASE( stack_limit ) { try {
   eosvmoc::config eosvmoc_config;

   // disable non-tested limits
   eosvmoc_config.cpu_limits.rlim_cur       = RLIM_INFINITY;
   eosvmoc_config.vm_limits.rlim_cur        = RLIM_INFINITY;
   eosvmoc_config.generated_code_size_limit = std::numeric_limits<size_t>::max();

   // The stack size of the compiled WASM in the test is 104.
   // Set stack_size_limit one less than the actual needed stack size
   eosvmoc_config.stack_size_limit = 103;
   limit_violated_test(eosvmoc_config);

   // set stack_size_limit to the actual needed stack size
   eosvmoc_config.stack_size_limit = 104;
   limit_not_violated_test(eosvmoc_config);
} FC_LOG_AND_RETHROW() }

// test generated code size limit is checked
BOOST_AUTO_TEST_CASE( generated_code_size_limit ) { try {
   eosvmoc::config eosvmoc_config;

   // disable non-tested limits
   eosvmoc_config.cpu_limits.rlim_cur = RLIM_INFINITY;
   eosvmoc_config.vm_limits.rlim_cur  = RLIM_INFINITY;
   eosvmoc_config.stack_size_limit    = std::numeric_limits<uint64_t>::max();

   // The generated code size of the compiled WASM in the test is 36856.
   // Set generated_code_size_limit to the actual generated code size
   eosvmoc_config.generated_code_size_limit = 36856;
   limit_violated_test(eosvmoc_config);

   // Set generated_code_size_limit to one above the actual generated code size
   eosvmoc_config.generated_code_size_limit = 36857;
   limit_not_violated_test(eosvmoc_config);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()

#endif
