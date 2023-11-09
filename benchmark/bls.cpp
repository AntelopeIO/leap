#include <benchmark.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/platform_timer.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/apply_context.hpp>
#include <eosio/chain/webassembly/interface.hpp>
#include <fc/variant_object.hpp>
#include <fc/io/json.hpp>
#include <test_contracts.hpp>
#include <fc/log/logger.hpp>
#include <bls12-381/bls12-381.hpp>
#include <random>

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace bls12_381;

namespace eosio::chain {
// placed in eosio::chain name space so that interface_in_benchmark
// can access transaction_context's private member max_transaction_time_subjective.
// interface_in_benchmark is a friend of transaction_context.

// To benchmark host functions directly without going through CDT
// wrappers, we need to contruct a eosio::chain::webassembly::interface
// object, as host functions are implemented in eosio::chain::webassembly::interface
struct interface_in_benchmark {
   interface_in_benchmark() {
      // prevent logging from messing up benchmark results display
      fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::off);

      // create a chain
      fc::temp_directory tempdir;
      auto conf_genesis = tester::default_config( tempdir );
      auto& cfg = conf_genesis.second.initial_configuration;
      // configure large cpu usgaes so expensive BLS functions like pairing
      // can run a reasonable number of times within a trasaction time
      cfg.max_block_cpu_usage        = 500'000;
      cfg.max_transaction_cpu_usage  = 480'000;
      cfg.min_transaction_cpu_usage  = 1;
      chain = std::make_unique<tester>(conf_genesis.first, conf_genesis.second);
      chain->execute_setup_policy( setup_policy::full );

      // get hold controller
      control = chain->control.get();

      // create account and deploy contract for a temp transaction
      chain->create_accounts( {"payloadless"_n} );
      chain->set_code( "payloadless"_n, test_contracts::payloadless_wasm() );
      chain->set_abi( "payloadless"_n, test_contracts::payloadless_abi() );

      // construct a signed transaction
      fc::variant pretty_trx = fc::mutable_variant_object()
         ("actions", fc::variants({
            fc::mutable_variant_object()
               ("account", name("payloadless"_n))
               ("name", "doit")
               ("authorization", fc::variants({
                  fc::mutable_variant_object()
                     ("actor", name("payloadless"_n))
                     ("permission", name(config::active_name))
               }))
               ("data", fc::mutable_variant_object()
               )
            })
        );
      trx = std::make_unique<signed_transaction>();
      abi_serializer::from_variant(pretty_trx, *trx, chain->get_resolver(), abi_serializer::create_yield_function( chain->abi_serializer_max_time ));
      chain->set_transaction_headers(*trx);
      trx->sign( chain->get_private_key( "payloadless"_n, "active" ), control->get_chain_id() );

      // construct a packed transaction
      ptrx = std::make_unique<packed_transaction>(*trx, eosio::chain::packed_transaction::compression_type::zlib);

      // build transaction context from the packed transaction
      timer = std::make_unique<platform_timer>();
      trx_timer = std::make_unique<transaction_checktime_timer>(*timer);
      trx_ctx = std::make_unique<transaction_context>(*control, *ptrx, ptrx->id(), std::move(*trx_timer));
      trx_ctx->max_transaction_time_subjective = fc::microseconds::maximum();
      trx_ctx->init_for_input_trx( ptrx->get_unprunable_size(), ptrx->get_prunable_size() );
      trx_ctx->exec();

      // build apply context from the control and transaction context
      apply_ctx = std::make_unique<apply_context>(*control, *trx_ctx, 1);

      // finally build the interface
      interface = std::make_unique<webassembly::interface>(*apply_ctx);
   }

   std::unique_ptr<tester>                      chain;
   controller*                                  control;
   std::unique_ptr<signed_transaction>          trx;
   std::unique_ptr<packed_transaction>          ptrx;
   std::unique_ptr<platform_timer>              timer;
   std::unique_ptr<transaction_checktime_timer> trx_timer;
   std::unique_ptr<transaction_context>         trx_ctx;
   std::unique_ptr<apply_context>               apply_ctx;
   std::unique_ptr<webassembly::interface>      interface;
};
} // namespace eosio::chain


namespace benchmark {

// utilility to create a random scalar
std::array<uint64_t, 4> random_scalar()
{
   std::random_device rd;
   std::mt19937_64 gen(rd());
   std::uniform_int_distribution<uint64_t> dis;

   return {
      dis(gen) % bls12_381::fp::Q[0],
      dis(gen) % bls12_381::fp::Q[1],
      dis(gen) % bls12_381::fp::Q[2],
      dis(gen) % bls12_381::fp::Q[3]
   };
}

// some functions like pairing/exp are expensive. set a hard limit to
// the number of runs such that the transanction does not reach deadline
constexpr uint32_t num_runs_limit = 200;

// utilility to create a random g1
bls12_381::g1 random_g1()
{
   std::array<uint64_t, 4> k = random_scalar();
   return bls12_381::g1::one().mulScalar(k);
}

// utilility to create a random g2
bls12_381::g2 random_g2()
{
   std::array<uint64_t, 4> k = random_scalar();
   return bls12_381::g2::one().mulScalar(k);
}

void benchmark_bls_g1_add() {
   // prepare g1 operand in Jacobian LE format
   g1 p = random_g1();
   std::vector<char> buf(144);
   p.toJacobianBytesLE(std::span<uint8_t, 144>((uint8_t*)buf.data(), 144), true);
   eosio::chain::span<const char> op1(buf.data(), buf.size());

   // prepare result operand
   std::vector<char> result_buf(144);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_g1_add to be benchmarked
   interface_in_benchmark interface;
   auto g1_add_func = [&]() {
      interface.interface->bls_g1_add(op1, op1, result);
   };

   benchmarking("bls_g1_add", g1_add_func);
}

void benchmark_bls_g2_add() {
   // prepare g2 operand in Jacobian LE format
   g2 p = random_g2();
   std::vector<char> buf(288);
   p.toJacobianBytesLE(std::span<uint8_t, 288>((uint8_t*)buf.data(), 288), true);
   eosio::chain::span<const char> op(buf.data(), buf.size());

   // prepare result operand
   std::vector<char> result_buf(288);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_g2_add to be benchmarked
   interface_in_benchmark interface;
   auto benchmarked_func = [&]() {
      interface.interface->bls_g2_add(op, op, result);
   };

   benchmarking("bls_g2_add", benchmarked_func);
}

void benchmark_bls_g1_mul() {
   // prepare g1 operand
   g1 p = random_g1();
   std::vector<char> buf(144);
   p.toJacobianBytesLE(std::span<uint8_t, 144>((uint8_t*)buf.data(), 144), true);
   eosio::chain::span<const char> point(buf.data(), buf.size());

   // prepare scalar operand
   std::array<uint64_t, 4> s = random_scalar();
   std::vector<char> scalar_buf(32);
   scalar::toBytesLE(s, std::span<uint8_t, 32>((uint8_t*)scalar_buf.data(), 32));
   eosio::chain::span<const char> scalar(scalar_buf.data(), scalar_buf.size());

   // prepare result operand
   std::vector<char> result_buf(144);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_g1_mul to be benchmarked
   interface_in_benchmark interface;
   auto benchmarked_func = [&]() {
      interface.interface->bls_g1_mul(point, scalar, result);
   };

   benchmarking("bls_g1_mul", benchmarked_func);
}

void benchmark_bls_g2_mul() {
   g2 p = random_g2();
   std::vector<char> buf(288);
   p.toJacobianBytesLE(std::span<uint8_t, 288>((uint8_t*)buf.data(), 288), true);
   eosio::chain::span<const char> point(buf.data(), buf.size());

   // prepare scalar operand
   std::array<uint64_t, 4> s = random_scalar();
   std::vector<char> scalar_buf(32);
   scalar::toBytesLE(s, std::span<uint8_t, 32>((uint8_t*)scalar_buf.data(), 32));
   eosio::chain::span<const char> scalar(scalar_buf.data(), scalar_buf.size());

   // prepare result operand
   std::vector<char> result_buf(288);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_g2_mul to be benchmarked
   interface_in_benchmark interface;
   auto benchmarked_func = [&]() {
      interface.interface->bls_g2_mul(point, scalar, result);
   };

   benchmarking("bls_g2_mul", benchmarked_func);
}

void benchmark_bls_g1_exp(std::string test_name, uint32_t num_points) {
   // prepare g1 points operand
   std::vector<char> g1_buf(144*num_points);
   for (auto i=0u; i < num_points; ++i) {
      g1 p = random_g1();
      p.toJacobianBytesLE(std::span<uint8_t, 144>((uint8_t*)g1_buf.data() + i * 144, 144), true);
   }
   chain::span<const char> g1_points(g1_buf.data(), g1_buf.size());

   // prepare scalars operand
   std::vector<char> scalars_buf(32*num_points);
   for (auto i=0u; i < num_points; ++i) {
      std::array<uint64_t, 4> s = random_scalar();
      scalar::toBytesLE(s, std::span<uint8_t, 32>((uint8_t*)scalars_buf.data() + i*32, 32));
   }
   chain::span<const char> scalars(scalars_buf.data(), scalars_buf.size());

   // prepare result operand
   std::vector<char> result_buf(144);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_g1_exp to be benchmarked
   interface_in_benchmark interface;
   auto benchmarked_func = [&]() {
      interface.interface->bls_g1_exp(g1_points, scalars, num_points, result);
   };

   benchmarking(test_name, benchmarked_func, num_runs_limit);
}

void benchmark_bls_g1_exp_one_point() {
   benchmark_bls_g1_exp("bls_g1_exp 1 point", 1);
}

void benchmark_bls_g1_exp_three_point() {
   benchmark_bls_g1_exp("bls_g1_exp 3 points", 3);
}

void benchmark_bls_g2_exp(std::string test_name, uint32_t num_points) {
   // prepare g2 points operand
   std::vector<char> g2_buf(288*num_points);
   for (auto i=0u; i < num_points; ++i) {
      g2 p = random_g2();
      p.toJacobianBytesLE(std::span<uint8_t, 288>((uint8_t*)g2_buf.data() + i * 288, 288), true);
   }
   eosio::chain::span<const char> g2_points(g2_buf.data(), g2_buf.size());

   // prepare scalars operand
   std::vector<char> scalars_buf(32*num_points);
   for (auto i=0u; i < num_points; ++i) {
      std::array<uint64_t, 4> s = random_scalar();
      scalar::toBytesLE(s, std::span<uint8_t, 32>((uint8_t*)scalars_buf.data() + i*32, 32));
   }
   eosio::chain::span<const char> scalars(scalars_buf.data(), scalars_buf.size());

   // prepare result operand
   std::vector<char> result_buf(288);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_g2_exp to be benchmarked
   interface_in_benchmark interface;
   auto benchmarked_func = [&]() {
      interface.interface->bls_g2_exp(g2_points, scalars, num_points, result);
   };

   benchmarking(test_name, benchmarked_func, num_runs_limit);
}

void benchmark_bls_g2_exp_one_point() {
   benchmark_bls_g2_exp("bls_g2_exp 1 point", 1);
}

void benchmark_bls_g2_exp_three_point() {
   benchmark_bls_g2_exp("bls_g2_exp 3 points", 3);
}

void benchmark_bls_pairing(std::string test_name, uint32_t num_pairs) {
   // prepare g1 operand
   std::vector<char> g1_buf(144*num_pairs);
   //g1_buf.reserve(144*num_pairs);
   for (auto i=0u; i < num_pairs; ++i) {
      g1 p = random_g1();
      p.toJacobianBytesLE(std::span<uint8_t, 144>((uint8_t*)g1_buf.data() + i * 144, 144), true);
   }
   eosio::chain::span<const char> g1_points(g1_buf.data(), g1_buf.size());

   // prepare g2 operand
   std::vector<char> g2_buf(288*num_pairs);
   for (auto i=0u; i < num_pairs; ++i) {
      g2 p2 = random_g2();
      p2.toJacobianBytesLE(std::span<uint8_t, (288)>((uint8_t*)g2_buf.data() + i * 288, (288)), true);
   }
   eosio::chain::span<const char> g2_points(g2_buf.data(), g2_buf.size());

   // prepare result operand
   std::vector<char> result_buf(576);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_pairing to be benchmarked
   interface_in_benchmark interface;
   auto benchmarked_func = [&]() {
      interface.interface->bls_pairing(g1_points, g2_points, num_pairs, result);
   };

   benchmarking(test_name, benchmarked_func, num_runs_limit);
}

void benchmark_bls_pairing_one_pair() {
   benchmark_bls_pairing("bls_pairing 1 pair", 1);
}

void benchmark_bls_pairing_three_pair() {
   benchmark_bls_pairing("bls_pairing 3 pairs", 3);
}
void bls_benchmarking() {
   benchmark_bls_g1_add();
   benchmark_bls_g2_add();
   benchmark_bls_g1_mul();
   benchmark_bls_g2_mul();
   benchmark_bls_pairing_one_pair();
   benchmark_bls_pairing_three_pair();
   benchmark_bls_g1_exp_one_point();
   benchmark_bls_g1_exp_three_point();
   benchmark_bls_g2_exp_one_point();
   benchmark_bls_g2_exp_three_point();
}
} // namespace benchmark
