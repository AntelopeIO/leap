#include <eosio/chain/block_header.hpp>

#include <eosio/hotstuff/qc_chain.hpp>

namespace eosio::hotstuff {

    using namespace eosio::chain;

    struct safety_state {

      void set_v_height(const name finalizer, const eosio::chain::view_number v_height){
         _states[finalizer.to_uint64_t()].first = v_height;
      }  

      void set_b_lock(const name finalizer, fc::sha256 b_lock){
         _states[finalizer.to_uint64_t()].second = b_lock;
      }  

      std::pair<eosio::chain::view_number, fc::sha256> get_safety_state(const name finalizer) const{
         auto s = _states.find(finalizer.to_uint64_t());
         if (s != _states.end()) return s->second;
         else return std::make_pair(eosio::chain::view_number(),fc::sha256());
      }  

      eosio::chain::view_number get_v_height(const name finalizer) const{
         auto s = _states.find(finalizer.to_uint64_t());
         if (s != _states.end()) return s->second.first;
         else return eosio::chain::view_number();
      };

      fc::sha256 get_b_lock(const name finalizer) const{
         auto s_itr = _states.find(finalizer.to_uint64_t());
         if (s_itr != _states.end()) return s_itr->second.second;
         else return fc::sha256();
      };

      //todo : implement safety state default / sorting

      std::pair<eosio::chain::view_number, fc::sha256> get_safety_state() const{
         auto s = _states.begin();
         if (s != _states.end()) return s->second;
         else return std::make_pair(eosio::chain::view_number(),fc::sha256());
      }  

      eosio::chain::view_number get_v_height() const{
         auto s = _states.begin();
         if (s != _states.end()) return s->second.first;
         else return eosio::chain::view_number();
      };

      fc::sha256 get_b_lock() const{
         auto s_itr = _states.begin();
         if (s_itr != _states.end()) return s_itr->second.second;
         else return fc::sha256();
      };

      std::unordered_map<uint64_t, std::pair<eosio::chain::view_number, fc::sha256>> _states;

    };

}

FC_REFLECT(eosio::hotstuff::safety_state, (_states))