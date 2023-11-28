#include <eosio/chain/incremental_merkle.hpp>
#include <boost/test/unit_test.hpp>
#include <fc/crypto/sha256.hpp>

using namespace eosio::chain;

BOOST_AUTO_TEST_SUITE(merkle_tree_tests)

BOOST_AUTO_TEST_CASE(basic_append_and_root_check_canonical) {
   incremental_canonical_merkle_tree tree;
   BOOST_CHECK_EQUAL(tree.get_root(), fc::sha256());

   auto node1 = fc::sha256::hash("Node1");
   tree.append(node1);
   BOOST_CHECK_EQUAL(tree.get_root(), node1);
}

BOOST_AUTO_TEST_CASE(multiple_appends_canonical) {
   incremental_canonical_merkle_tree tree;
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

   tree.append(node2);
   BOOST_CHECK_EQUAL(tree.get_root().str(), fc::sha256::hash(make_canonical_pair(node1, node2)).str());

   tree.append(node3);
   BOOST_CHECK_EQUAL(tree.get_root().str(), fc::sha256::hash(make_canonical_pair(
                        fc::sha256::hash(make_canonical_pair(node1, node2)),
                        fc::sha256::hash(make_canonical_pair(node3, node3)))).str());

   tree.append(node4);
   auto calculated_root = fc::sha256::hash(make_canonical_pair(
      fc::sha256::hash(make_canonical_pair(node1, node2)),
      fc::sha256::hash(make_canonical_pair(node3, node4))));
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());

   tree.append(node5);
   calculated_root = fc::sha256::hash(
      make_canonical_pair(
         fc::sha256::hash(make_canonical_pair(
            fc::sha256::hash(make_canonical_pair(node1, node2)),
            fc::sha256::hash(make_canonical_pair(node3, node4))
         )),
         fc::sha256::hash(make_canonical_pair(
            fc::sha256::hash(make_canonical_pair(node5, node5)),
            fc::sha256::hash(make_canonical_pair(node5, node5))
         ))
      )
   );
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());

   tree.append(node6);
   calculated_root = fc::sha256::hash(
      make_canonical_pair(
         fc::sha256::hash(make_canonical_pair(
            fc::sha256::hash(make_canonical_pair(node1, node2)),
            fc::sha256::hash(make_canonical_pair(node3, node4))
         )),
         fc::sha256::hash(make_canonical_pair(
            fc::sha256::hash(make_canonical_pair(node5, node6)),
            fc::sha256::hash(make_canonical_pair(node5, node6))
         ))
      )
   );
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());

   tree.append(node7);
   calculated_root = fc::sha256::hash(
      make_canonical_pair(
         fc::sha256::hash(make_canonical_pair(
            fc::sha256::hash(make_canonical_pair(node1, node2)),
            fc::sha256::hash(make_canonical_pair(node3, node4))
         )),
         fc::sha256::hash(make_canonical_pair(
            fc::sha256::hash(make_canonical_pair(node5, node6)),
            fc::sha256::hash(make_canonical_pair(node7, node7))
         ))
      )
   );
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());

   tree.append(node8);
   calculated_root = fc::sha256::hash(
      make_canonical_pair(
         fc::sha256::hash(make_canonical_pair(
            fc::sha256::hash(make_canonical_pair(node1, node2)),
            fc::sha256::hash(make_canonical_pair(node3, node4))
         )),
         fc::sha256::hash(make_canonical_pair(
            fc::sha256::hash(make_canonical_pair(node5, node6)),
            fc::sha256::hash(make_canonical_pair(node7, node8))
         ))
      )
   );
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());

   tree.append(node9);
   calculated_root = fc::sha256::hash(make_canonical_pair(
      fc::sha256::hash(
         make_canonical_pair(
            fc::sha256::hash(make_canonical_pair(
               fc::sha256::hash(make_canonical_pair(node1, node2)),
               fc::sha256::hash(make_canonical_pair(node3, node4))
            )),
            fc::sha256::hash(make_canonical_pair(
               fc::sha256::hash(make_canonical_pair(node5, node6)),
               fc::sha256::hash(make_canonical_pair(node7, node8))
            ))
         )
      ),
      fc::sha256::hash(
         make_canonical_pair(
            fc::sha256::hash(make_canonical_pair(
               fc::sha256::hash(make_canonical_pair(node9, node9)),
               fc::sha256::hash(make_canonical_pair(node9, node9))
            )),
            fc::sha256::hash(make_canonical_pair(
               fc::sha256::hash(make_canonical_pair(node9, node9)),
               fc::sha256::hash(make_canonical_pair(node9, node9))
            ))
         )
      )   ));
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());
}

BOOST_AUTO_TEST_CASE(basic_append_and_root_check) {
   incremental_merkle_tree tree;
   BOOST_CHECK_EQUAL(tree.get_root(), fc::sha256());

   auto node1 = fc::sha256::hash("Node1");
   tree.append(node1);
   BOOST_CHECK_EQUAL(tree.get_root(), node1);
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

   tree.append(node2);
   BOOST_CHECK_EQUAL(tree.get_root().str(), fc::sha256::hash(std::make_pair(node1, node2)).str());

   tree.append(node3);
   BOOST_CHECK_EQUAL(tree.get_root().str(), fc::sha256::hash(std::make_pair(
                        fc::sha256::hash(std::make_pair(node1, node2)),
                        fc::sha256::hash(std::make_pair(node3, node3)))).str());

   tree.append(node4);
   auto calculated_root = fc::sha256::hash(std::make_pair(
      fc::sha256::hash(std::make_pair(node1, node2)),
      fc::sha256::hash(std::make_pair(node3, node4))));
   BOOST_CHECK_EQUAL(tree.get_root().str(), calculated_root.str());

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
}

BOOST_AUTO_TEST_SUITE_END()
