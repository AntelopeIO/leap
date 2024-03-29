#include <eosio/chain/incremental_merkle.hpp>
#include <eosio/chain/incremental_merkle_legacy.hpp>
#include <boost/test/unit_test.hpp>
#include <fc/crypto/sha256.hpp>
#include <chrono>

using namespace eosio::chain;
using eosio::chain::detail::make_legacy_digest_pair;

std::vector<digest_type> create_test_digests(size_t n) {
   std::vector<digest_type> v;
   v.reserve(n);
   for (size_t i=0; i<n; ++i)
      v.push_back(fc::sha256::hash(std::string{"Node"} + std::to_string(i)));
   return v;
}

constexpr auto hash = eosio::chain::detail::hash_combine;

BOOST_AUTO_TEST_SUITE(merkle_tree_tests)

BOOST_AUTO_TEST_CASE(basic_append_and_root_check_legacy) {
   incremental_merkle_tree_legacy tree;
   BOOST_CHECK_EQUAL(tree.get_root(), fc::sha256());

   auto node1 = fc::sha256::hash("Node1");
   tree.append(node1);
   BOOST_CHECK_EQUAL(tree.get_root().str(), node1.str());
   BOOST_CHECK_EQUAL(calculate_merkle_legacy({node1}).str(), node1.str());
}

BOOST_AUTO_TEST_CASE(multiple_appends_legacy) {
   incremental_merkle_tree_legacy tree;
   auto node1 = fc::sha256::hash("Node1");
   auto node2 = fc::sha256::hash("Node2");
   auto node3 = fc::sha256::hash("Node3");
   auto node4 = fc::sha256::hash("Node4");
   auto node5 = fc::sha256::hash("Node5");
   auto node6 = fc::sha256::hash("Node6");
   auto node7 = fc::sha256::hash("Node7");
   auto node8 = fc::sha256::hash("Node8");
   auto node9 = fc::sha256::hash("Node9");

   tree.append(node1);
   BOOST_CHECK_EQUAL(tree.get_root().str(), node1.str());
   BOOST_CHECK_EQUAL(calculate_merkle_legacy({node1}).str(), node1.str());

   tree.append(node2);
   BOOST_CHECK_EQUAL(tree.get_root().str(), fc::sha256::hash(make_legacy_digest_pair(node1, node2)).str());
   BOOST_CHECK_EQUAL(calculate_merkle_legacy({node1, node2}).str(), fc::sha256::hash(make_legacy_digest_pair(node1, node2)).str());

   tree.append(node3);
   BOOST_CHECK_EQUAL(tree.get_root().str(), fc::sha256::hash(make_legacy_digest_pair(
                        fc::sha256::hash(make_legacy_digest_pair(node1, node2)),
                        fc::sha256::hash(make_legacy_digest_pair(node3, node3)))).str());
   BOOST_CHECK_EQUAL(calculate_merkle_legacy({node1, node2, node3}).str(), fc::sha256::hash(make_legacy_digest_pair(
                        fc::sha256::hash(make_legacy_digest_pair(node1, node2)),
                        fc::sha256::hash(make_legacy_digest_pair(node3, node3)))).str());

   tree.append(node4);
   auto calculated_root = fc::sha256::hash(make_legacy_digest_pair(
      fc::sha256::hash(make_legacy_digest_pair(node1, node2)),
      fc::sha256::hash(make_legacy_digest_pair(node3, node4))));
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());
   BOOST_CHECK_EQUAL(calculate_merkle_legacy({node1, node2, node3, node4}).str(), calculated_root.str());

   tree.append(node5);
   calculated_root = fc::sha256::hash(
      make_legacy_digest_pair(
         fc::sha256::hash(make_legacy_digest_pair(
            fc::sha256::hash(make_legacy_digest_pair(node1, node2)),
            fc::sha256::hash(make_legacy_digest_pair(node3, node4))
         )),
         fc::sha256::hash(make_legacy_digest_pair(
            fc::sha256::hash(make_legacy_digest_pair(node5, node5)),
            fc::sha256::hash(make_legacy_digest_pair(node5, node5))
         ))
      )
   );
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());
   BOOST_CHECK_EQUAL(calculate_merkle_legacy({node1, node2, node3, node4, node5}).str(), calculated_root.str());

   tree.append(node6);
   calculated_root = fc::sha256::hash(
      make_legacy_digest_pair(
         fc::sha256::hash(make_legacy_digest_pair(
            fc::sha256::hash(make_legacy_digest_pair(node1, node2)),
            fc::sha256::hash(make_legacy_digest_pair(node3, node4))
         )),
         fc::sha256::hash(make_legacy_digest_pair(
            fc::sha256::hash(make_legacy_digest_pair(node5, node6)),
            fc::sha256::hash(make_legacy_digest_pair(node5, node6))
         ))
      )
   );
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());
   BOOST_CHECK_EQUAL(calculate_merkle_legacy({node1, node2, node3, node4, node5, node6}).str(), calculated_root.str());

   tree.append(node7);
   calculated_root = fc::sha256::hash(
      make_legacy_digest_pair(
         fc::sha256::hash(make_legacy_digest_pair(
            fc::sha256::hash(make_legacy_digest_pair(node1, node2)),
            fc::sha256::hash(make_legacy_digest_pair(node3, node4))
         )),
         fc::sha256::hash(make_legacy_digest_pair(
            fc::sha256::hash(make_legacy_digest_pair(node5, node6)),
            fc::sha256::hash(make_legacy_digest_pair(node7, node7))
         ))
      )
   );
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());
   BOOST_CHECK_EQUAL(calculate_merkle_legacy({node1, node2, node3, node4, node5, node6, node7}).str(), calculated_root.str());

   tree.append(node8);
   calculated_root = fc::sha256::hash(
      make_legacy_digest_pair(
         fc::sha256::hash(make_legacy_digest_pair(
            fc::sha256::hash(make_legacy_digest_pair(node1, node2)),
            fc::sha256::hash(make_legacy_digest_pair(node3, node4))
         )),
         fc::sha256::hash(make_legacy_digest_pair(
            fc::sha256::hash(make_legacy_digest_pair(node5, node6)),
            fc::sha256::hash(make_legacy_digest_pair(node7, node8))
         ))
      )
   );
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());
   BOOST_CHECK_EQUAL(calculate_merkle_legacy({node1, node2, node3, node4, node5, node6, node7, node8}).str(), calculated_root.str());

   tree.append(node9);
   calculated_root = fc::sha256::hash(make_legacy_digest_pair(
      fc::sha256::hash(
         make_legacy_digest_pair(
            fc::sha256::hash(make_legacy_digest_pair(
               fc::sha256::hash(make_legacy_digest_pair(node1, node2)),
               fc::sha256::hash(make_legacy_digest_pair(node3, node4))
            )),
            fc::sha256::hash(make_legacy_digest_pair(
               fc::sha256::hash(make_legacy_digest_pair(node5, node6)),
               fc::sha256::hash(make_legacy_digest_pair(node7, node8))
            ))
         )
      ),
      fc::sha256::hash(
         make_legacy_digest_pair(
            fc::sha256::hash(make_legacy_digest_pair(
               fc::sha256::hash(make_legacy_digest_pair(node9, node9)),
               fc::sha256::hash(make_legacy_digest_pair(node9, node9))
            )),
            fc::sha256::hash(make_legacy_digest_pair(
               fc::sha256::hash(make_legacy_digest_pair(node9, node9)),
               fc::sha256::hash(make_legacy_digest_pair(node9, node9))
            ))
         )
      )   ));
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());
   BOOST_CHECK_EQUAL(calculate_merkle_legacy({node1, node2, node3, node4, node5, node6, node7, node8, node9}).str(), calculated_root.str());
}

BOOST_AUTO_TEST_CASE(basic_append_and_root_check) {
   incremental_merkle_tree tree;
   BOOST_CHECK_EQUAL(tree.get_root(), fc::sha256());

   auto node1 = fc::sha256::hash("Node1");
   tree.append(node1);
   BOOST_CHECK_EQUAL(tree.get_root(), node1);
   BOOST_CHECK_EQUAL(calculate_merkle({node1}), node1);
}

BOOST_AUTO_TEST_CASE(multiple_appends) {
   incremental_merkle_tree tree;
   auto node1 = fc::sha256::hash("Node1");
   auto node2 = fc::sha256::hash("Node2");
   auto node3 = fc::sha256::hash("Node3");
   auto node4 = fc::sha256::hash("Node4");
   auto node5 = fc::sha256::hash("Node5");
   auto node6 = fc::sha256::hash("Node6");
   auto node7 = fc::sha256::hash("Node7");
   auto node8 = fc::sha256::hash("Node8");
   auto node9 = fc::sha256::hash("Node9");

   std::vector<digest_type> digests { node1, node2, node3, node4, node5, node6, node7, node8, node9 };
   auto first = digests.cbegin();

   tree.append(node1);
   BOOST_CHECK_EQUAL(tree.get_root(), node1);
   BOOST_CHECK_EQUAL(calculate_merkle({node1}), node1);

   tree.append(node2);
   BOOST_CHECK_EQUAL(tree.get_root(), hash(node1, node2));
   BOOST_CHECK_EQUAL(calculate_merkle({first, first + 2}), hash(node1, node2));

   tree.append(node3);
   auto calculated_root = hash(hash(node1, node2), node3);
   BOOST_CHECK_EQUAL(tree.get_root(), calculated_root);
   BOOST_CHECK_EQUAL(calculate_merkle({first, first + 3}), calculated_root);

   tree.append(node4);
   auto first_four_tree = hash(hash(node1, node2), hash(node3, node4));
   calculated_root = first_four_tree;
   BOOST_CHECK_EQUAL(tree.get_root(), calculated_root);
   BOOST_CHECK_EQUAL(calculate_merkle({first, first + 4}), calculated_root);

   tree.append(node5);
   calculated_root = hash(first_four_tree, node5);
   BOOST_CHECK_EQUAL(tree.get_root(), calculated_root);
   BOOST_CHECK_EQUAL(calculate_merkle({first, first + 5}), calculated_root);

   tree.append(node6);
   calculated_root = hash(first_four_tree, hash(node5, node6));
   BOOST_CHECK_EQUAL(tree.get_root(), calculated_root);
   BOOST_CHECK_EQUAL(calculate_merkle({first, first + 6}), calculated_root);

   tree.append(node7);
   calculated_root = hash(first_four_tree, hash(hash(node5, node6), node7));
   BOOST_CHECK_EQUAL(tree.get_root(), calculated_root);
   BOOST_CHECK_EQUAL(calculate_merkle({first, first + 7}), calculated_root);

   tree.append(node8);
   auto next_four_tree = hash(hash(node5, node6), hash(node7, node8));
   calculated_root = hash(first_four_tree, next_four_tree);
   BOOST_CHECK_EQUAL(tree.get_root(), calculated_root);
   BOOST_CHECK_EQUAL(calculate_merkle({first, first + 8}), calculated_root);

   tree.append(node9);
   calculated_root = hash(hash(first_four_tree, next_four_tree), node9);
   BOOST_CHECK_EQUAL(tree.get_root(), calculated_root);
   BOOST_CHECK_EQUAL(calculate_merkle({first, first + 9}), calculated_root);
}

BOOST_AUTO_TEST_CASE(consistency_over_large_range) {
   constexpr size_t num_digests = 1001ull;

   std::vector<digest_type> digests = create_test_digests(num_digests);
   for (size_t i=1; i<num_digests; ++i) {
      incremental_merkle_tree tree;
      for (size_t j=0; j<i; ++j)
         tree.append(digests[j]);
      BOOST_CHECK_EQUAL(calculate_merkle({digests.begin(), digests.begin() + i}), tree.get_root());
   }
}

class stopwatch {
public:
   stopwatch(std::string msg) : _msg(std::move(msg)) {  _start = clock::now(); }

   ~stopwatch() { std::cout << _msg << get_time_us()/1000000 << " s\n"; }

   double get_time_us() const {
      using duration_t = std::chrono::duration<double, std::micro>;
      return std::chrono::duration_cast<duration_t>(clock::now() - _start).count();
   }

   using clock = std::chrono::high_resolution_clock;
   using point = std::chrono::time_point<clock>;

   std::string _msg;
   point       _start;
};

BOOST_AUTO_TEST_CASE(perf_test_one_large) {
   auto perf_test = [](const std::string& type, auto&& incr_tree, auto&& calc_fn) {
      using namespace std::string_literals;
      constexpr size_t num_digests = 1000ull * 1000ull; // don't use exact powers of 2 as it is a special case

      std::vector<digest_type> digests = create_test_digests(num_digests);
      deque<digest_type> deq { digests.begin(), digests.end() };

      auto incr_root = [&]() {
         stopwatch s("time for "s + type + " incremental_merkle: ");
         for (const auto& d : digests)
            incr_tree.append(d);
         return incr_tree.get_root();
      }();

      auto calc_root = [&]() {
         stopwatch s("time for "s + type + " calculate_merkle: ");
         return calc_fn(deq);
      }();

      return std::make_pair(incr_root, calc_root);
   };

   {
      auto [incr_root, calc_root] = perf_test("new", incremental_merkle_tree(), calculate_merkle);
      BOOST_CHECK_EQUAL(incr_root, calc_root);
   }

   {
      auto [incr_root, calc_root] = perf_test("legacy", incremental_merkle_tree_legacy(), calculate_merkle_legacy);
      BOOST_CHECK_EQUAL(incr_root, calc_root);
   }
}


BOOST_AUTO_TEST_CASE(perf_test_many_small) {

   auto perf_test = [](const std::string& type, auto&& incr_tree, auto&& calc_fn) {
      using namespace std::string_literals;
      constexpr size_t num_digests = 10000; // don't use exact powers of 2 as it is a special case
      constexpr size_t num_runs    = 100;

      std::vector<digest_type> digests = create_test_digests(num_digests);
      deque<digest_type> deq { digests.begin(), digests.end() };

      deque<digest_type> results(num_runs);

      auto incr = [&]() {
         auto work_tree = incr_tree;
         for (const auto& d : digests)
            work_tree.append(d);
         return work_tree.get_root();
      };

      auto calc = [&]() { return calc_fn(deq); };

      auto incr_root = [&]() {
         stopwatch s("time for "s + type + " incremental_merkle: ");
         for (auto& r : results)
            r = incr();
         return calc_fn(results);
      }();

      auto calc_root = [&]() {
         stopwatch s("time for "s + type + " calculate_merkle: ");
         for (auto& r : results)
            r = calc();
         return calc_fn(results);
      }();

      return std::make_pair(incr_root, calc_root);
   };

   {
      auto [incr_root, calc_root] = perf_test("new", incremental_merkle_tree(), calculate_merkle);
      BOOST_CHECK_EQUAL(incr_root, calc_root);
   }

   {
      auto [incr_root, calc_root] = perf_test("legacy", incremental_merkle_tree_legacy(), calculate_merkle_legacy);
      BOOST_CHECK_EQUAL(incr_root, calc_root);
   }
}

BOOST_AUTO_TEST_SUITE_END()
