#pragma once
#include <chainbase/chainbase.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/config.hpp>

#include <type_traits>

namespace eosio { namespace chain {

using shared_public_key_data = std::variant<fc::ecc::public_key_shim, fc::crypto::r1::public_key_shim, shared_string>;

struct shared_public_key {
   explicit shared_public_key( shared_public_key_data&& p ) :
      pubkey(std::move(p)) {}

   public_key_type to_public_key() const {
      fc::crypto::public_key::storage_type public_key_storage;
      std::visit(overloaded {
         [&](const auto& k1r1) {
            public_key_storage = k1r1;
         },
         [&](const shared_string& wa) {
            fc::datastream<const char*> ds(wa.data(), wa.size());
            fc::crypto::webauthn::public_key pub;
            fc::raw::unpack(ds, pub);
            public_key_storage = pub;
         }
      }, pubkey);
      return std::move(public_key_storage);
   }

   std::string to_string(const fc::yield_function_t& yield) const {
      return this->to_public_key().to_string(yield);
   }

   shared_public_key_data pubkey;

   friend bool operator == ( const shared_public_key& lhs, const shared_public_key& rhs ) {
      if(lhs.pubkey.index() != rhs.pubkey.index())
         return false;

      return std::visit(overloaded {
         [&](const fc::ecc::public_key_shim& k1) {
            return k1._data == std::get<fc::ecc::public_key_shim>(rhs.pubkey)._data;
         },
         [&](const fc::crypto::r1::public_key_shim& r1) {
            return r1._data == std::get<fc::crypto::r1::public_key_shim>(rhs.pubkey)._data;
         },
         [&](const shared_string& wa) {
            return wa == std::get<shared_string>(rhs.pubkey);
         }
      }, lhs.pubkey);
   }

   friend bool operator==(const shared_public_key& l, const public_key_type& r) {
      if(l.pubkey.index() != r._storage.index())
         return false;

      return std::visit(overloaded {
         [&](const fc::ecc::public_key_shim& k1) {
            return k1._data == std::get<fc::ecc::public_key_shim>(r._storage)._data;
         },
         [&](const fc::crypto::r1::public_key_shim& r1) {
            return r1._data == std::get<fc::crypto::r1::public_key_shim>(r._storage)._data;
         },
         [&](const shared_string& wa) {
            fc::datastream<const char*> ds(wa.data(), wa.size());
            fc::crypto::webauthn::public_key pub;
            fc::raw::unpack(ds, pub);
            return pub == std::get<fc::crypto::webauthn::public_key>(r._storage);
         }
      }, l.pubkey);
   }

   friend bool operator==(const public_key_type& l, const shared_public_key& r) {
      return r == l;
   }
};

struct permission_level_weight {
   permission_level  permission;
   weight_type       weight;

   friend bool operator == ( const permission_level_weight& lhs, const permission_level_weight& rhs ) {
      return tie( lhs.permission, lhs.weight ) == tie( rhs.permission, rhs.weight );
   }

   friend bool operator < ( const permission_level_weight& lhs, const permission_level_weight& rhs ) {
      return tie( lhs.permission, lhs.weight ) < tie( rhs.permission, rhs.weight );
   }
};

struct shared_key_weight;

struct key_weight {
   public_key_type key;
   weight_type     weight;

   friend bool operator == ( const key_weight& lhs, const key_weight& rhs ) {
      return tie( lhs.key, lhs.weight ) == tie( rhs.key, rhs.weight );
   }

   friend bool operator==( const key_weight& lhs, const shared_key_weight& rhs );

   friend bool operator < ( const key_weight& lhs, const key_weight& rhs ) {
      return tie( lhs.key, lhs.weight ) < tie( rhs.key, rhs.weight );
   }
};


struct shared_key_weight {
   shared_key_weight(shared_public_key_data&& k, const weight_type& w) :
      key(std::move(k)), weight(w) {}

   key_weight to_key_weight() const {
      return key_weight{key.to_public_key(), weight};
   }

   shared_key_weight(const key_weight& k) : key(fc::ecc::public_key_shim()), weight(k.weight) {
      std::visit(overloaded {
         [&]<class T>(const T& k1r1) {
            key.pubkey.emplace<T>(k1r1);
         },
         [&](const fc::crypto::webauthn::public_key& wa) {
            size_t psz = fc::raw::pack_size(wa);
            // create a shared_string in the pubkey that we will write (using pack) directly into.
            key.pubkey.emplace<shared_string>(psz,  boost::container::default_init_t());
            auto& s = std::get<shared_string>(key.pubkey);
            assert(s.mutable_data() && s.size() == psz);
            fc::datastream<char*> ds(s.mutable_data(), psz);
            fc::raw::pack(ds, wa);
         }
      }, k.key._storage);
   }

   shared_public_key key;
   weight_type       weight;

   friend bool operator == ( const shared_key_weight& lhs, const shared_key_weight& rhs ) {
      return tie( lhs.key, lhs.weight ) == tie( rhs.key, rhs.weight );
   }
};

inline bool operator==( const key_weight& lhs, const shared_key_weight& rhs ) {
   return tie( lhs.key, lhs.weight ) == tie( rhs.key, rhs.weight );
}

struct wait_weight {
   uint32_t     wait_sec;
   weight_type  weight;

   friend bool operator == ( const wait_weight& lhs, const wait_weight& rhs ) {
      return tie( lhs.wait_sec, lhs.weight ) == tie( rhs.wait_sec, rhs.weight );
   }

   friend bool operator < ( const wait_weight& lhs, const wait_weight& rhs ) {
      return tie( lhs.wait_sec, lhs.weight ) < tie( rhs.wait_sec, rhs.weight );
   }
};

namespace config {
   template<>
   struct billable_size<permission_level_weight> {
      static const uint64_t value = 24; ///< over value of weight for safety
   };

   template<>
   struct billable_size<key_weight> {
      static const uint64_t value = 8; ///< over value of weight for safety, dynamically sizing key
   };

   template<>
   struct billable_size<wait_weight> {
      static const uint64_t value = 16; ///< over value of weight and wait_sec for safety
   };
}

struct shared_authority;

struct authority {
   authority( public_key_type k, uint32_t delay_sec = 0 )
   :threshold(1),keys({{k,1}})
   {
      if( delay_sec > 0 ) {
         threshold = 2;
         waits.push_back(wait_weight{delay_sec, 1});
      }
   }

   explicit authority( permission_level p, uint32_t delay_sec = 0 )
   :threshold(1),accounts({{p,1}})
   {
      if( delay_sec > 0 ) {
         threshold = 2;
         waits.push_back(wait_weight{delay_sec, 1});
      }
   }

   authority( uint32_t t, vector<key_weight> k, vector<permission_level_weight> p = {}, vector<wait_weight> w = {} )
   :threshold(t),keys(std::move(k)),accounts(std::move(p)),waits(std::move(w)){}
   authority(){}

   uint32_t                          threshold = 0;
   vector<key_weight>                keys;
   vector<permission_level_weight>   accounts;
   vector<wait_weight>               waits;

   friend bool operator == ( const authority& lhs, const authority& rhs ) {
      return tie( lhs.threshold, lhs.keys, lhs.accounts, lhs.waits ) == tie( rhs.threshold, rhs.keys, rhs.accounts, rhs.waits );
   }

   friend bool operator == ( const authority& lhs, const shared_authority& rhs );

   void sort_fields () {
      std::sort(std::begin(keys), std::end(keys));
      std::sort(std::begin(accounts), std::end(accounts));
      std::sort(std::begin(waits), std::end(waits));
   }
};


struct shared_authority {
   explicit shared_authority() = default;

   shared_authority(const authority& auth) :
      threshold(auth.threshold),
      keys(auth.keys),
      accounts(auth.accounts),
      waits(auth.waits)
   {
   }

   shared_authority(authority&& auth) :
      threshold(auth.threshold),
      keys(std::move(auth.keys)),
      accounts(std::move(auth.accounts)),
      waits(std::move(auth.waits))
   {
   }

   shared_authority& operator=(const authority& auth) {
      threshold = auth.threshold;
      keys = auth.keys;
      accounts = auth.accounts;
      waits = auth.waits;
      return *this;
   }

   shared_authority& operator=(authority&& auth) {
      threshold = auth.threshold;
      keys = std::move(auth.keys);
      accounts = std::move(auth.accounts);
      waits = std::move(auth.waits);
      return *this;
   }

   uint32_t                                   threshold = 0;
   shared_vector<shared_key_weight>           keys;
   shared_vector<permission_level_weight>     accounts;
   shared_vector<wait_weight>                 waits;

   authority to_authority()const {
      authority auth;
      auth.threshold = threshold;
      auth.keys.reserve(keys.size());
      auth.accounts.reserve(accounts.size());
      auth.waits.reserve(waits.size());
      for( const auto& k : keys ) { auth.keys.emplace_back( k.to_key_weight() ); }
      for( const auto& a : accounts ) { auth.accounts.emplace_back( a ); }
      for( const auto& w : waits ) { auth.waits.emplace_back( w ); }
      return auth;
   }

   size_t get_billable_size() const {
      size_t accounts_size = accounts.size() * config::billable_size_v<permission_level_weight>;
      size_t waits_size = waits.size() * config::billable_size_v<wait_weight>;
      size_t keys_size = 0;
      for (const auto& k: keys) {
         keys_size += config::billable_size_v<key_weight>;
         keys_size += fc::raw::pack_size(k.key);  ///< serialized size of the key
      }

      return accounts_size + waits_size + keys_size;
   }
};

inline bool operator==( const authority& lhs, const shared_authority& rhs ) {
   return lhs.threshold == rhs.threshold &&
          lhs.keys.size() == rhs.keys.size() &&
          lhs.accounts.size() == rhs.accounts.size() &&
          lhs.waits.size() == rhs.waits.size() &&
          std::equal(lhs.keys.cbegin(), lhs.keys.cend(), rhs.keys.cbegin(), rhs.keys.cend()) &&
          std::equal(lhs.accounts.cbegin(), lhs.accounts.cend(), rhs.accounts.cbegin(), rhs.accounts.cend()) &&
          std::equal(lhs.waits.cbegin(), lhs.waits.cend(), rhs.waits.cbegin(), rhs.waits.cend());
}

namespace config {
   template<>
   struct billable_size<shared_authority> {
      static const uint64_t value = (3 * config::fixed_overhead_shared_vector_ram_bytes) + 4;
   };
}

/**
 * Makes sure all keys are unique and sorted and all account permissions are unique and sorted and that authority can
 * be satisfied
 */
template<typename Authority>
inline bool validate( const Authority& auth ) {
   decltype(auth.threshold) total_weight = 0;

   static_assert( std::is_same<decltype(auth.threshold), uint32_t>::value &&
                  std::is_same<weight_type, uint16_t>::value &&
                  std::is_same<typename decltype(auth.keys)::value_type, key_weight>::value &&
                  std::is_same<typename decltype(auth.accounts)::value_type, permission_level_weight>::value &&
                  std::is_same<typename decltype(auth.waits)::value_type, wait_weight>::value,
                  "unexpected type for threshold and/or weight in authority" );

   if( ( auth.keys.size() + auth.accounts.size() + auth.waits.size() ) > (1 << 16) )
      return false; // overflow protection (assumes weight_type is uint16_t and threshold is of type uint32_t)

   if( auth.threshold == 0 )
      return false;

   {
      const key_weight* prev = nullptr;
      for( const auto& k : auth.keys ) {
         if( prev && !(prev->key < k.key) ) return false; // TODO: require keys to be sorted in ascending order rather than descending (requires modifying many tests)
         total_weight += k.weight;
         prev = &k;
      }
   }
   {
      const permission_level_weight* prev = nullptr;
      for( const auto& a : auth.accounts ) {
         if( prev && ( prev->permission >= a.permission ) ) return false; // TODO: require permission_levels to be sorted in ascending order rather than descending (requires modifying many tests)
         total_weight += a.weight;
         prev = &a;
      }
   }
   {
      const wait_weight* prev = nullptr;
      if( auth.waits.size() > 0 && auth.waits.front().wait_sec == 0 )
         return false;
      for( const auto& w : auth.waits ) {
         if( prev && ( prev->wait_sec >= w.wait_sec ) ) return false;
         total_weight += w.weight;
         prev = &w;
      }
   }

   return total_weight >= auth.threshold;
}

} } // namespace eosio::chain

namespace fc {
   void to_variant(const eosio::chain::shared_public_key& var, fc::variant& vo);
} // namespace fc

FC_REFLECT(eosio::chain::permission_level_weight, (permission)(weight) )
FC_REFLECT(eosio::chain::key_weight, (key)(weight) )
FC_REFLECT(eosio::chain::wait_weight, (wait_sec)(weight) )
FC_REFLECT(eosio::chain::authority, (threshold)(keys)(accounts)(waits) )
FC_REFLECT(eosio::chain::shared_key_weight, (key)(weight) )
FC_REFLECT(eosio::chain::shared_authority, (threshold)(keys)(accounts)(waits) )
FC_REFLECT(eosio::chain::shared_public_key, (pubkey))
