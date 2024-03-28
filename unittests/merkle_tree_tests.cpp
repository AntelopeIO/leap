#include <eosio/chain/incremental_merkle.hpp>
#include <eosio/chain/incremental_merkle_legacy.hpp>
#include <boost/test/unit_test.hpp>
#include <fc/crypto/sha256.hpp>

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

BOOST_AUTO_TEST_SUITE_END()
