#include <eosio/hotstuff/hotstuff.hpp>

namespace eosio::hotstuff {

    using namespace eosio::chain;

    struct safety_state {

      void set_v_height(const fc::crypto::blslib::bls_public_key& finalizer_key, const view_number v_height) {
         _states[finalizer_key].first = v_height;
      }

      void set_b_lock(const fc::crypto::blslib::bls_public_key& finalizer_key, const fc::sha256& b_lock) {
         _states[finalizer_key].second = b_lock;
      }

      std::pair<view_number, fc::sha256> get_safety_state(const fc::crypto::blslib::bls_public_key& finalizer_key) const {
         auto s = _states.find(finalizer_key);
         if (s != _states.end()) return s->second;
         else return {};
      }

      view_number get_v_height(const fc::crypto::blslib::bls_public_key& finalizer_key) const {
         auto s = _states.find(finalizer_key);
         if (s != _states.end()) return s->second.first;
         else return {};
      };

      fc::sha256 get_b_lock(const fc::crypto::blslib::bls_public_key& finalizer_key) const {
         auto s_itr = _states.find(finalizer_key);
         if (s_itr != _states.end()) return s_itr->second.second;
         else return {};
      };

      //todo : implement safety state default / sorting

      std::pair<view_number, fc::sha256> get_safety_state() const {
         auto s = _states.begin();
         if (s != _states.end()) return s->second;
         else return {};
      }

      view_number get_v_height() const {
         auto s = _states.begin();
         if (s != _states.end()) return s->second.first;
         else return {};
      };

      fc::sha256 get_b_lock() const {
         auto s_itr = _states.begin();
         if (s_itr != _states.end()) return s_itr->second.second;
         else return {};
      };

      std::map<fc::crypto::blslib::bls_public_key, std::pair<view_number, fc::sha256>> _states;
    };
}

FC_REFLECT(eosio::hotstuff::safety_state, (_states))
