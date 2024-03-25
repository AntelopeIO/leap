#include <eosio/chain/incremental_merkle.hpp>
#include <eosio/chain/incremental_merkle_legacy.hpp>
#include <eosio/chain/merkle.hpp>
#include <boost/test/unit_test.hpp>
#include <fc/crypto/sha256.hpp>

using namespace eosio::chain;
using eosio::chain::detail::make_legacy_digest_pair;

BOOST_AUTO_TEST_SUITE(merkle_tree_tests)

BOOST_AUTO_TEST_CASE(basic_append_and_root_check_canonical) {
   incremental_merkle_tree_legacy tree;
   BOOST_CHECK_EQUAL(tree.get_root(), fc::sha256());

   auto node1 = fc::sha256::hash("Node1");
   tree.append(node1);
   BOOST_CHECK_EQUAL(tree.get_root().str(), node1.str());
   BOOST_CHECK_EQUAL(calculate_merkle_legacy({node1}).str(), node1.str());
}

BOOST_AUTO_TEST_CASE(multiple_appends_canonical) {
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
   BOOST_CHECK_EQUAL(calculate_merkle({node1}).str(), node1.str());
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

   tree.append(node1);
   BOOST_CHECK_EQUAL(tree.get_root().str(), node1.str());
   BOOST_CHECK_EQUAL(calculate_merkle({node1}).str(), node1.str());

   tree.append(node2);
   BOOST_CHECK_EQUAL(tree.get_root().str(), fc::sha256::hash(std::make_pair(node1, node2)).str());
   BOOST_CHECK_EQUAL(calculate_merkle({node1, node2}).str(), fc::sha256::hash(std::make_pair(node1, node2)).str());

   tree.append(node3);
   BOOST_CHECK_EQUAL(tree.get_root().str(), fc::sha256::hash(std::make_pair(
                        fc::sha256::hash(std::make_pair(node1, node2)),
                        fc::sha256::hash(std::make_pair(node3, node3)))).str());
   BOOST_CHECK_EQUAL(calculate_merkle({node1, node2, node3}).str(), fc::sha256::hash(std::make_pair(
                        fc::sha256::hash(std::make_pair(node1, node2)),
                        fc::sha256::hash(std::make_pair(node3, node3)))).str());

   tree.append(node4);
   auto calculated_root = fc::sha256::hash(std::make_pair(
      fc::sha256::hash(std::make_pair(node1, node2)),
      fc::sha256::hash(std::make_pair(node3, node4))));
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());
   BOOST_CHECK_EQUAL(calculate_merkle({node1, node2, node3, node4}).str(), calculated_root.str());

   tree.append(node5);
   calculated_root = fc::sha256::hash(
      std::make_pair(
         fc::sha256::hash(std::make_pair(
            fc::sha256::hash(std::make_pair(node1, node2)),
            fc::sha256::hash(std::make_pair(node3, node4))
         )),
         fc::sha256::hash(std::make_pair(
            fc::sha256::hash(std::make_pair(node5, node5)),
            fc::sha256::hash(std::make_pair(node5, node5))
         ))
      )
   );
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());
   BOOST_CHECK_EQUAL(calculate_merkle({node1, node2, node3, node4, node5}).str(), calculated_root.str());

   tree.append(node6);
   calculated_root = fc::sha256::hash(
      std::make_pair(
         fc::sha256::hash(std::make_pair(
            fc::sha256::hash(std::make_pair(node1, node2)),
            fc::sha256::hash(std::make_pair(node3, node4))
         )),
         fc::sha256::hash(std::make_pair(
            fc::sha256::hash(std::make_pair(node5, node6)),
            fc::sha256::hash(std::make_pair(node5, node6))
         ))
      )
   );
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());
   BOOST_CHECK_EQUAL(calculate_merkle({node1, node2, node3, node4, node5, node6}).str(), calculated_root.str());

   tree.append(node7);
   calculated_root = fc::sha256::hash(
      std::make_pair(
         fc::sha256::hash(std::make_pair(
            fc::sha256::hash(std::make_pair(node1, node2)),
            fc::sha256::hash(std::make_pair(node3, node4))
         )),
         fc::sha256::hash(std::make_pair(
            fc::sha256::hash(std::make_pair(node5, node6)),
            fc::sha256::hash(std::make_pair(node7, node7))
         ))
      )
   );
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());
   BOOST_CHECK_EQUAL(calculate_merkle({node1, node2, node3, node4, node5, node6, node7}).str(), calculated_root.str());

   tree.append(node8);
   calculated_root = fc::sha256::hash(
      std::make_pair(
         fc::sha256::hash(std::make_pair(
            fc::sha256::hash(std::make_pair(node1, node2)),
            fc::sha256::hash(std::make_pair(node3, node4))
         )),
         fc::sha256::hash(std::make_pair(
            fc::sha256::hash(std::make_pair(node5, node6)),
            fc::sha256::hash(std::make_pair(node7, node8))
         ))
      )
   );
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());
   BOOST_CHECK_EQUAL(calculate_merkle({node1, node2, node3, node4, node5, node6, node7, node8}).str(), calculated_root.str());

   tree.append(node9);
   calculated_root = fc::sha256::hash(std::make_pair(
      fc::sha256::hash(
         std::make_pair(
            fc::sha256::hash(std::make_pair(
               fc::sha256::hash(std::make_pair(node1, node2)),
               fc::sha256::hash(std::make_pair(node3, node4))
            )),
            fc::sha256::hash(std::make_pair(
               fc::sha256::hash(std::make_pair(node5, node6)),
               fc::sha256::hash(std::make_pair(node7, node8))
            ))
         )
      ),
      fc::sha256::hash(
         std::make_pair(
            fc::sha256::hash(std::make_pair(
               fc::sha256::hash(std::make_pair(node9, node9)),
               fc::sha256::hash(std::make_pair(node9, node9))
            )),
            fc::sha256::hash(std::make_pair(
               fc::sha256::hash(std::make_pair(node9, node9)),
               fc::sha256::hash(std::make_pair(node9, node9))
            ))
         )
      )   ));
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());
   BOOST_CHECK_EQUAL(calculate_merkle({node1, node2, node3, node4, node5, node6, node7, node8, node9}).str(), calculated_root.str());
}

BOOST_AUTO_TEST_SUITE_END()
