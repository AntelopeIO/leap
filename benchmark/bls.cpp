#include <benchmark.hpp>
#include <eosio/chain/apply_context.hpp>
#include <eosio/chain/webassembly/interface.hpp>
#include <eosio/testing/tester.hpp>
#include <test_contracts.hpp>
#include <bls12-381/bls12-381.hpp>
#include <random>

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace bls12_381;

// Benchmark BLS host functions without relying on CDT wrappers.
//
// To run a benchmarking session, in the build directory, type
//    benchmark/benchmark -f bls

namespace eosio::benchmark {

// To benchmark host functions directly without CDT wrappers,
// we need to contruct an eosio::chain::webassembly::interface object,
// because host functions are implemented in
// eosio::chain::webassembly::interface class.
struct interface_in_benchmark {
   interface_in_benchmark() {
      // prevent logging from interwined with output benchmark results
      fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::off);

      // create a chain
      fc::temp_directory tempdir;
      auto conf_genesis = tester::default_config( tempdir );
      auto& cfg = conf_genesis.second.initial_configuration;
      // configure large cpu usgaes so expensive BLS functions like pairing
      // can finish within a trasaction time
      cfg.max_block_cpu_usage        = 999'999'999;
      cfg.max_transaction_cpu_usage  = 999'999'990;
      cfg.min_transaction_cpu_usage  = 1;
      chain = std::make_unique<tester>(conf_genesis.first, conf_genesis.second);
      chain->execute_setup_policy( setup_policy::full );

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
      trx->sign( chain->get_private_key( "payloadless"_n, "active" ), chain->control.get()->get_chain_id() );

      // construct a packed transaction
      ptrx = std::make_unique<packed_transaction>(*trx, eosio::chain::packed_transaction::compression_type::zlib);

      // build transaction context from the packed transaction
      timer = std::make_unique<platform_timer>();
      trx_timer = std::make_unique<transaction_checktime_timer>(*timer);
      trx_ctx = std::make_unique<transaction_context>(*chain->control.get(), *ptrx, ptrx->id(), std::move(*trx_timer));
      trx_ctx->max_transaction_time_subjective = fc::microseconds::maximum();
      trx_ctx->init_for_input_trx( ptrx->get_unprunable_size(), ptrx->get_prunable_size() );
      trx_ctx->exec(); // this is required to generate action traces to be used by apply_context constructor

      // build apply context from the control and transaction context
      apply_ctx = std::make_unique<apply_context>(*chain->control.get(), *trx_ctx, 1);

      // finally construct the interface
      interface = std::make_unique<webassembly::interface>(*apply_ctx);
   }

   std::unique_ptr<tester>                      chain;
   std::unique_ptr<signed_transaction>          trx;
   std::unique_ptr<packed_transaction>          ptrx;
   std::unique_ptr<platform_timer>              timer;
   std::unique_ptr<transaction_checktime_timer> trx_timer;
   std::unique_ptr<transaction_context>         trx_ctx;
   std::unique_ptr<apply_context>               apply_ctx;
   std::unique_ptr<webassembly::interface>      interface;
};

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

// utilility to create a random fp
fp random_fe()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    return fp({
        dis(gen) % 0xb9feffffffffaaab,
        dis(gen) % 0x1eabfffeb153ffff,
        dis(gen) % 0x6730d2a0f6b0f624,
        dis(gen) % 0x64774b84f38512bf,
        dis(gen) % 0x4b1ba7b6434bacd7,
        dis(gen) % 0x1a0111ea397fe69a
    });
}

// utilility to create a random fp2
fp2 random_fe2()
{
    return fp2({
        random_fe(),
        random_fe()
    });
}

// utilility to create a random g1
bls12_381::g1 random_g1()
{
   std::array<uint64_t, 4> k = random_scalar();
   return bls12_381::g1::one().scale(k);
}

// utilility to create a random g2
bls12_381::g2 random_g2()
{
   std::array<uint64_t, 4> k = random_scalar();
   return bls12_381::g2::one().scale(k);
}

// bls_g1_add benchmarking
void benchmark_bls_g1_add_impl(const std::string& test_name, bool mont) {
   // prepare g1 operand in Jacobian LE format
   g1 p = random_g1();
   std::vector<char> buf(96);
   p.toAffineBytesLE(std::span<uint8_t, 96>((uint8_t*)buf.data(), 96), mont);
   eosio::chain::span<const char> op1(buf.data(), buf.size());

   // prepare result operand
   std::vector<char> result_buf(96);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_g1_add to be benchmarked
   interface_in_benchmark interface;
   std::function<void()> benchmarked_func {};
   if (mont) {
      benchmarked_func = [&]() {
         interface.interface->bls_g1_add_mont(op1, op1, result);
      };
   } else {
      benchmarked_func = [&]() {
         interface.interface->bls_g1_add(op1, op1, result);
      };
   }

   benchmarking(test_name, benchmarked_func);
}

void benchmark_bls_g1_add() {
   benchmark_bls_g1_add_impl("bls_g1_add", false);
}

void benchmark_bls_g1_add_mont() {
   benchmark_bls_g1_add_impl("bls_g1_add_mont", true);
}

// bls_g2_add benchmarking
void benchmark_bls_g2_add_impl(const std::string& test_name, bool mont) {
   // prepare g2 operand in Jacobian LE format
   g2 p = random_g2();
   std::vector<char> buf(192);
   p.toAffineBytesLE(std::span<uint8_t, 192>((uint8_t*)buf.data(), 192), mont);
   eosio::chain::span<const char> op(buf.data(), buf.size());

   // prepare result operand
   std::vector<char> result_buf(192);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_g2_add to be benchmarked
   interface_in_benchmark interface;
   std::function<void()> benchmarked_func {};
   if (mont) {
      benchmarked_func = [&]() {
         interface.interface->bls_g2_add_mont(op, op, result);
      };
   } else {
      benchmarked_func = [&]() {
         interface.interface->bls_g2_add(op, op, result);
      };
   }

   benchmarking(test_name, benchmarked_func);
}

void benchmark_bls_g2_add() {
   benchmark_bls_g2_add_impl("bls_g2_add", false);
}

void benchmark_bls_g2_add_mont() {
   benchmark_bls_g2_add_impl("bls_g2_add_mont", true);
}

// bls_g1_weighted_sum benchmarking utility
void benchmark_bls_g1_weighted_sum_impl(const std::string& test_name, uint32_t num_points, bool mont) {
   // prepare g1 points operand
   std::vector<char> g1_buf(96*num_points);
   for (auto i=0u; i < num_points; ++i) {
      g1 p = random_g1();
      p.toAffineBytesLE(std::span<uint8_t, 96>((uint8_t*)g1_buf.data() + i * 96, 96), mont);
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
   std::vector<char> result_buf(96);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_g1_weighted_sum to be benchmarked
   interface_in_benchmark interface;
   std::function<void()> benchmarked_func {};
   if (mont) {
      benchmarked_func = [&]() {
         interface.interface->bls_g1_weighted_sum_mont(g1_points, scalars, num_points, result);
      };
   } else {
      benchmarked_func = [&]() {
         interface.interface->bls_g1_weighted_sum(g1_points, scalars, num_points, result);
      };
   }

   benchmarking(test_name, benchmarked_func);
}

// bls_g1_weighted_sum benchmarking with 1 input point
void benchmark_bls_g1_weighted_sum_one_point() {
   benchmark_bls_g1_weighted_sum_impl("bls_g1_weighted_sum 1 point", 1, false);
}

// bls_g1_weighted_sum benchmarking with 3 input points
void benchmark_bls_g1_weighted_sum_three_point() {
   benchmark_bls_g1_weighted_sum_impl("bls_g1_weighted_sum 3 points", 3, false);
}

// bls_g1_weighted_sum benchmarking with 5 input points
void benchmark_bls_g1_weighted_sum_five_point() {
   benchmark_bls_g1_weighted_sum_impl("bls_g1_weighted_sum 5 points", 5, false);
}

// bls_g1_weighted_sum benchmarking with 1 input point
void benchmark_bls_g1_weighted_sum_mont_one_point() {
   benchmark_bls_g1_weighted_sum_impl("bls_g1_weighted_sum_mont 1 point", 1, true);
}

// bls_g1_weighted_sum_mont benchmarking with 3 input points
void benchmark_bls_g1_weighted_sum_mont_three_point() {
   benchmark_bls_g1_weighted_sum_impl("bls_g1_weighted_sum_mont 3 points", 3, true);
}

// bls_g1_weighted_sum_mont benchmarking with 5 input points
void benchmark_bls_g1_weighted_sum_mont_five_point() {
   benchmark_bls_g1_weighted_sum_impl("bls_g1_weighted_sum_mont 5 points", 5, true);
}

// bls_g2_weighted_sum benchmarking utility
void benchmark_bls_g2_weighted_sum_impl(const std::string& test_name, uint32_t num_points, bool mont) {
   // prepare g2 points operand
   std::vector<char> g2_buf(192*num_points);
   for (auto i=0u; i < num_points; ++i) {
      g2 p = random_g2();
      p.toAffineBytesLE(std::span<uint8_t, 192>((uint8_t*)g2_buf.data() + i * 192, 192), mont);
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
   std::vector<char> result_buf(192);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_g2_weighted_sum to be benchmarked
   interface_in_benchmark interface;
   std::function<void()> benchmarked_func {};
   if (mont) {
      benchmarked_func = [&]() {
         interface.interface->bls_g2_weighted_sum_mont(g2_points, scalars, num_points, result);
      };
   } else {
      benchmarked_func = [&]() {
         interface.interface->bls_g2_weighted_sum(g2_points, scalars, num_points, result);
      };
   }

   benchmarking(test_name, benchmarked_func);
}

// bls_g2_weighted_sum benchmarking with 1 input point
void benchmark_bls_g2_weighted_sum_one_point() {
   benchmark_bls_g2_weighted_sum_impl("bls_g2_weighted_sum 1 point", 1, false);
}

// bls_g2_weighted_sum benchmarking with 3 input points
void benchmark_bls_g2_weighted_sum_three_point() {
   benchmark_bls_g2_weighted_sum_impl("bls_g2_weighted_sum 3 points", 3, false);
}

// bls_g2_weighted_sum benchmarking with 5 input points
void benchmark_bls_g2_weighted_sum_five_point() {
   benchmark_bls_g2_weighted_sum_impl("bls_g2_weighted_sum 5 points", 5, false);
}

// bls_g2_weighted_sum_mont benchmarking with 1 input point
void benchmark_bls_g2_weighted_sum_mont_one_point() {
   benchmark_bls_g2_weighted_sum_impl("bls_g2_weighted_sum_mont 1 point", 1, true);
}

// bls_g2_weighted_sum_mont benchmarking with 3 input points
void benchmark_bls_g2_weighted_sum_mont_three_point() {
   benchmark_bls_g2_weighted_sum_impl("bls_g2_weighted_sum_mont 3 points", 3, true);
}

// bls_g2_weighted_sum_mont benchmarking with 5 input points
void benchmark_bls_g2_weighted_sum_mont_five_point() {
   benchmark_bls_g2_weighted_sum_impl("bls_g2_weighted_sum_mont 5 points", 5, true);
}

// bls_pairing benchmarking utility
void benchmark_bls_pairing_impl(const std::string& test_name, uint32_t num_pairs, bool mont) {
   // prepare g1 operand
   std::vector<char> g1_buf(96*num_pairs);
   //g1_buf.reserve(96*num_pairs);
   for (auto i=0u; i < num_pairs; ++i) {
      g1 p = random_g1();
      p.toAffineBytesLE(std::span<uint8_t, 96>((uint8_t*)g1_buf.data() + i * 96, 96), mont);
   }
   eosio::chain::span<const char> g1_points(g1_buf.data(), g1_buf.size());

   // prepare g2 operand
   std::vector<char> g2_buf(192*num_pairs);
   for (auto i=0u; i < num_pairs; ++i) {
      g2 p2 = random_g2();
      p2.toAffineBytesLE(std::span<uint8_t, (192)>((uint8_t*)g2_buf.data() + i * 192, (192)), mont);
   }
   eosio::chain::span<const char> g2_points(g2_buf.data(), g2_buf.size());

   // prepare result operand
   std::vector<char> result_buf(576);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_pairing to be benchmarked
   interface_in_benchmark interface;
   std::function<void()> benchmarked_func {};
   if (mont) {
      benchmarked_func = [&]() {
         interface.interface->bls_pairing_mont(g1_points, g2_points, num_pairs, result);
      };
   } else {
      benchmarked_func = [&]() {
         interface.interface->bls_pairing(g1_points, g2_points, num_pairs, result);
      };
   }

   benchmarking(test_name, benchmarked_func);
}

// bls_pairing benchmarking with 1 input pair
void benchmark_bls_pairing_one_pair() {
   benchmark_bls_pairing_impl("bls_pairing 1 pair", 1, false);
}

// bls_pairing benchmarking with 3 input pairs
void benchmark_bls_pairing_three_pair() {
   benchmark_bls_pairing_impl("bls_pairing 3 pairs", 3, false);
}

// bls_pairing_mont benchmarking with 1 input pair
void benchmark_bls_pairing_mont_one_pair() {
   benchmark_bls_pairing_impl("bls_pairing_mont 1 pair", 1, true);
}

// bls_pairing_mont benchmarking with 3 input pairs
void benchmark_bls_pairing_mont_three_pair() {
   benchmark_bls_pairing_impl("bls_pairing_mont 3 pairs", 3, true);
}

// bls_g1_map benchmarking
void benchmark_bls_g1_map_impl(const std::string& test_name, bool mont) {
   // prepare e operand. Must be fp LE.
   std::vector<char> e_buf(48);
   fp a = random_fe();
   a.toBytesLE(std::span<uint8_t, 48>((uint8_t*)e_buf.data(), 48), mont);
   eosio::chain::span<const char> e(e_buf.data(), e_buf.size());

   // prepare result operand
   std::vector<char> result_buf(96);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_g1_map to be benchmarked
   interface_in_benchmark interface;
   std::function<void()> benchmarked_func {};
   if (mont) {
      benchmarked_func = [&]() {
         interface.interface->bls_g1_map_mont(e, result);
      };
   } else {
      benchmarked_func = [&]() {
         interface.interface->bls_g1_map(e, result);
      };
   }

   benchmarking(test_name, benchmarked_func);
}

void benchmark_bls_g1_map() {
   benchmark_bls_g1_map_impl("bls_g1_map", false);
}

void benchmark_bls_g1_map_mont() {
   benchmark_bls_g1_map_impl("bls_g1_map_mont", true);
}

// bls_g2_map benchmarking
void benchmark_bls_g2_map_impl(const std::string& test_name, bool mont) {
   // prepare e operand. Must be fp2 LE.
   std::vector<char> e_buf(96);
   fp2 a = random_fe2();
   a.toBytesLE(std::span<uint8_t, 96>((uint8_t*)e_buf.data(), 96), mont);
   eosio::chain::span<const char> e(e_buf.data(), e_buf.size());

   // prepare result operand
   std::vector<char> result_buf(192);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_g2_map to be benchmarked
   interface_in_benchmark interface;
   std::function<void()> benchmarked_func {};
   if (mont) {
      benchmarked_func = [&]() {
         interface.interface->bls_g2_map_mont(e, result);
      };
   } else {
      benchmarked_func = [&]() {
         interface.interface->bls_g2_map(e, result);
      };
   }

   benchmarking(test_name, benchmarked_func);
}

void benchmark_bls_g2_map() {
   benchmark_bls_g2_map_impl("bls_g2_map", false);
}

void benchmark_bls_g2_map_mont() {
   benchmark_bls_g2_map_impl("bls_g2_map_mont", true);
}

// bls_fp_mod benchmarking
void benchmark_bls_fp_mod_impl(const std::string& test_name, bool mont) {
   // prepare scalar operand
   std::vector<char> scalar_buf(64);
   // random_scalar returns 32 bytes. need to call it twice
   for (auto i=0u; i < 2; ++i) {
      std::array<uint64_t, 4> s = random_scalar();
      scalar::toBytesLE(s, std::span<uint8_t, 32>((uint8_t*)scalar_buf.data() + i*32, 32));
   }
   chain::span<const char> scalar(scalar_buf.data(), scalar_buf.size());

   // prepare result operand
   std::vector<char> result_buf(48);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_fp_mod to be benchmarked
   interface_in_benchmark interface;
   std::function<void()> benchmarked_func {};
   if (mont) {
      benchmarked_func = [&]() {
         interface.interface->bls_fp_mod_mont(scalar, result);
      };
   } else {
      benchmarked_func = [&]() {
         interface.interface->bls_fp_mod(scalar, result);
      };
   }

   benchmarking(test_name, benchmarked_func);
}

void benchmark_bls_fp_mod() {
   benchmark_bls_fp_mod_impl("bls_fp_mod", false);
}

void benchmark_bls_fp_mod_mont() {
   benchmark_bls_fp_mod_impl("bls_fp_mod_mont", true);
}

void benchmark_bls_fp_mul_impl(const std::string& test_name, bool mont) {
   // prepare op1
   std::vector<char> op1_buf(48);
   fp a = random_fe();
   a.toBytesLE(std::span<uint8_t, 48>((uint8_t*)op1_buf.data(), 48), mont);
   eosio::chain::span<const char> op1(op1_buf.data(), op1_buf.size());

   // prepare op2
   std::vector<char> op2_buf(48);
   fp b = random_fe();
   b.toBytesLE(std::span<uint8_t, 48>((uint8_t*)op2_buf.data(), 48), mont);
   eosio::chain::span<const char> op2(op1_buf.data(), op2_buf.size());

   // prepare result operand
   std::vector<char> result_buf(48);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_pairing to be benchmarked
   interface_in_benchmark interface;
   std::function<void()> benchmarked_func {};
   if (mont) {
      benchmarked_func = [&]() {
         interface.interface->bls_fp_mul_mont(op1, op2, result);
      };
   } else {
      benchmarked_func = [&]() {
         interface.interface->bls_fp_mul(op1, op2, result);
      };
   }

   benchmarking(test_name, benchmarked_func);
}

void benchmark_bls_fp_mul() {
   benchmark_bls_fp_mul_impl("bls_fp_mul", false);
}

void benchmark_bls_fp_mul_mont() {
   benchmark_bls_fp_mul_impl("bls_fp_mul_mont", true);
}

void benchmark_bls_fp_exp_impl(const std::string& test_name, bool mont) {
   // prepare base
   std::vector<char> base_buf(48);
   fp a = random_fe();
   a.toBytesLE(std::span<uint8_t, 48>((uint8_t*)base_buf.data(), 48), mont);
   eosio::chain::span<const char> base(base_buf.data(), base_buf.size());

   // prepare exp operand
   std::vector<char> exp_buf(64);
   // random_scalar returns 32 bytes. need to call it twice
   for (auto i=0u; i < 2; ++i) {
      std::array<uint64_t, 4> s = random_scalar();
      scalar::toBytesLE(s, std::span<uint8_t, 32>((uint8_t*)exp_buf.data() + i*32, 32));
   }
   eosio::chain::span<const char> exp(exp_buf.data(), exp_buf.size());

   // prepare result operand
   std::vector<char> result_buf(48);
   eosio::chain::span<char> result(result_buf.data(), result_buf.size());

   // set up bls_fp_exp to be benchmarked
   interface_in_benchmark interface;
   std::function<void()> benchmarked_func {};
   if (mont) {
      benchmarked_func = [&]() {
         interface.interface->bls_fp_exp_mont(base, exp, result);
      };
   } else {
      benchmarked_func = [&]() {
         interface.interface->bls_fp_exp(base, exp, result);
      };
   }

   benchmarking(test_name, benchmarked_func);
}

void benchmark_bls_fp_exp() {
   benchmark_bls_fp_exp_impl("bls_fp_exp", false);
}

void benchmark_bls_fp_exp_mont() {
   benchmark_bls_fp_exp_impl("bls_fp_exp_mont", true);
}

// register benchmarking functions
void bls_benchmarking() {
   benchmark_bls_g1_add();
   benchmark_bls_g1_add_mont();
   benchmark_bls_g2_add();
   benchmark_bls_g2_add_mont();
   benchmark_bls_pairing_one_pair();
   benchmark_bls_pairing_mont_one_pair();
   benchmark_bls_pairing_three_pair();
   benchmark_bls_pairing_mont_three_pair();
   benchmark_bls_g1_weighted_sum_one_point();
   benchmark_bls_g1_weighted_sum_mont_one_point();
   benchmark_bls_g1_weighted_sum_three_point();
   benchmark_bls_g1_weighted_sum_mont_three_point();
   benchmark_bls_g1_weighted_sum_five_point();
   benchmark_bls_g1_weighted_sum_mont_five_point();
   benchmark_bls_g2_weighted_sum_one_point();
   benchmark_bls_g2_weighted_sum_mont_one_point();
   benchmark_bls_g2_weighted_sum_three_point();
   benchmark_bls_g2_weighted_sum_mont_three_point();
   benchmark_bls_g2_weighted_sum_five_point();
   benchmark_bls_g2_weighted_sum_mont_five_point();
   benchmark_bls_g1_map();
   benchmark_bls_g1_map_mont();
   benchmark_bls_g2_map();
   benchmark_bls_g2_map_mont();
   benchmark_bls_fp_mod();
   benchmark_bls_fp_mod_mont();
   benchmark_bls_fp_mul();
   benchmark_bls_fp_mul_mont();
   benchmark_bls_fp_exp();
   benchmark_bls_fp_exp_mont();
}
} // namespace benchmark
