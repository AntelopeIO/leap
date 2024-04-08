#include <benchmark.hpp>
#include <eosio/chain/incremental_merkle.hpp>
#include <eosio/chain/incremental_merkle_legacy.hpp>
#include <random>

namespace eosio::benchmark {

using namespace eosio::chain;

std::vector<digest_type> create_test_digests(size_t n) {
   std::vector<digest_type> v;
   v.reserve(n);
   for (size_t i=0; i<n; ++i)
      v.push_back(fc::sha256::hash(std::string{"Node"} + std::to_string(i)));
   return v;
}

void benchmark_calc_merkle(uint32_t size_boost) {
   using namespace std::string_literals;
   const size_t num_digests = size_boost * 1000ull; // don't use exact powers of 2 as it is a special case

   const std::vector<digest_type> digests = create_test_digests(num_digests);
   const deque<digest_type> deq { digests.begin(), digests.end() };

   auto num_str = std::to_string(size_boost);
   while(num_str.size() < 4)
      num_str.insert(0, 1, ' ');
   auto msg_header = "Calc, "s + num_str + ",000 digests,  "s;
   uint32_t num_runs = std::min(get_num_runs(), std::max(1u, get_num_runs() / size_boost));
   benchmarking(msg_header + "legacy: ", [&]() { calculate_merkle_legacy(deq); }, num_runs);
   benchmarking(msg_header + "savanna:", [&]() { calculate_merkle(digests.begin(), digests.end()); }, num_runs);
}

void benchmark_incr_merkle(uint32_t size_boost) {
   using namespace std::string_literals;
   const size_t num_digests = size_boost * 1000ull; // don't use exact powers of 2 as it is a special case

   const std::vector<digest_type> digests = create_test_digests(num_digests);

   auto num_str = std::to_string(size_boost);
   while(num_str.size() < 4)
      num_str.insert(0, 1, ' ');
   auto msg_header = "Incr, "s + num_str + ",000 digests,  "s;
   uint32_t num_runs = std::min(get_num_runs(), std::max(1u, get_num_runs() / size_boost));

   auto incr = [&](const auto& incr_tree) {
      auto work_tree = incr_tree;
      for (const auto& d : digests)
         work_tree.append(d);
      return work_tree.get_root();
   };

   benchmarking(msg_header + "legacy: ", [&]() { incr(incremental_merkle_tree_legacy()); }, num_runs);
   benchmarking(msg_header + "savanna:", [&]() { incr(incremental_merkle_tree()); }, num_runs);
}

// register benchmarking functions
void merkle_benchmarking() {
   benchmark_calc_merkle(1000); // calculate_merkle of very large sequence (1,000,000 digests)
   benchmark_calc_merkle(50);   // calculate_merkle of large sequence (50,000 digests)
   benchmark_calc_merkle(1);    // calculate_merkle of small sequence (1000 digests)
   std::cout << "\n";

   benchmark_incr_merkle(100);  // incremental_merkle of very large sequence (100,000 digests)
   benchmark_incr_merkle(25);   // incremental_merkle of large sequence (25,000 digests)
   benchmark_incr_merkle(1);    // incremental_merkle of small sequence (1000 digests)
}

}