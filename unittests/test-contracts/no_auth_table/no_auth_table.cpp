#include <eosio/eosio.hpp>

// This creates a simple table without require_auth used.
// It is mainly used to test read-only transactions.

using namespace eosio;
using namespace std;

class [[eosio::contract]] no_auth_table : public contract {
public:
   using contract::contract;

   no_auth_table(name receiver, name code,  datastream<const char*> ds): contract(receiver, code, ds) {}

   [[eosio::action]]
   uint64_t getage(name user) {
      person_index people( get_self(), get_first_receiver().value );
      auto iterator = people.find(user.value);
      check(iterator != people.end(), "Record does not exist");
      return iterator->age;
   }

   [[eosio::action]]
   void insert(name user, uint64_t id, uint64_t age) {
      person_index people( get_self(), get_first_receiver().value );
      auto iterator = people.find(user.value);
      check(iterator == people.end(), "Record already exists");
      people.emplace(user, [&]( auto& row ) {
         row.key = user;
         row.id = id;
         row.age = age;
      });
   }

   [[eosio::action]]
   void modify(name user, uint64_t age) {
      person_index people( get_self(), get_first_receiver().value );
      auto iterator = people.find(user.value);
      check(iterator != people.end(), "Record does not exist");
      people.modify(iterator, user, [&]( auto& row ) {
         row.key = user;
         row.age = age;
      });
   }

   [[eosio::action]]
   void modifybyid(uint64_t id, uint64_t age) {
      person_index people( get_self(), get_first_receiver().value );
      auto secondary_index = people.template get_index<"byid"_n>();
      auto iterator = secondary_index.find(id);
      check(iterator != secondary_index.end(), "Record does not exist");
      secondary_index.modify(iterator, get_self(), [&]( auto& row ) {
         row.id = id;
         row.age = age;
      });
   }
 
   [[eosio::action]]
   void erase(name user) {
      person_index people( get_self(), get_first_receiver().value);

      auto iterator = people.find(user.value);
      check(iterator != people.end(), "Record does not exist");
      people.erase(iterator);
   } 

   [[eosio::action]]
   void erasebyid(uint64_t id) {
      person_index people( get_self(), get_first_receiver().value );
      auto secondary_index = people.template get_index<"byid"_n>();
      auto iterator = secondary_index.find(id);
      check(iterator != secondary_index.end(), "Record does not exist");
      secondary_index.erase(iterator);
   }

private:
   struct [[eosio::table]] person {
      name key;
      uint64_t id;
      uint64_t age;
      uint64_t primary_key() const { return key.value; }
      uint64_t sec64_key() const { return id; }
   };
   using person_index = eosio::multi_index<"people"_n, person,
     indexed_by<"byid"_n, const_mem_fun<person, uint64_t, &person::sec64_key>>
    >;
};
