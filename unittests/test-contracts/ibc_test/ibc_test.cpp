/*
  TODO: Replace with IBC testing contract (bridge functionality)

*/

#include <eosio/eosio.hpp>
using namespace eosio;

CONTRACT ibc_test : public contract {
   public:
      using contract::contract;

      ACTION hi( name nm );

      using hi_action = action_wrapper<"hi"_n, &ibc_test::hi>;


      /*
        TODO:

        set_finalizer_policy(finalizer_policy policy)

        checkproofa(heavyproof proof) //must also store the block merkle root + the height if the heavyproof is at the highest height the contract has successfully verified so far  
        checkproofb(lightproof proof) //must verify a proof of inclusion between the block to prove and the highest height for which a (heavy) finality proof as been successfully verified 
      */
      
};

ACTION ibc_test::hi( name nm ) {
   /* fill in action body */
   print_f("Name : %\n",nm);
}
