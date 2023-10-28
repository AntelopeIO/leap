#include <eosio/chain/protocol_state_object.hpp>

namespace eosio { namespace chain {

   namespace detail {

      snapshot_protocol_state_object
      snapshot_row_traits<protocol_state_object>::to_snapshot_row( const protocol_state_object& value,
                                                                   const chainbase::database& db )
      {
         snapshot_protocol_state_object res;

         res.activated_protocol_features.reserve( value.activated_protocol_features.size() );
         for( const auto& v : value.activated_protocol_features ) {
            res.activated_protocol_features.emplace_back( v );
         }

         res.preactivated_protocol_features.reserve( value.preactivated_protocol_features.size() );
         for( const auto& v : value.preactivated_protocol_features ) {
            res.preactivated_protocol_features.emplace_back( v );
         }

         res.whitelisted_intrinsics = convert_intrinsic_whitelist_to_set( value.whitelisted_intrinsics );

         res.num_supported_key_types = value.num_supported_key_types;

         return res;
      }

      void
      snapshot_row_traits<protocol_state_object>::from_snapshot_row( snapshot_protocol_state_object&& row,
                                                                     protocol_state_object& value,
                                                                     chainbase::database& db )
      {
         value.activated_protocol_features.clear_and_construct( row.activated_protocol_features.size(), 0, [&](void* dest, std::size_t idx){
            new (dest) protocol_state_object::activated_protocol_feature(row.activated_protocol_features[idx]);
         });

         value.preactivated_protocol_features.clear_and_construct(row.preactivated_protocol_features.size(), 0, [&](void* dest, std::size_t idx) {
            new (dest) digest_type(row.preactivated_protocol_features[idx]);
         });

         reset_intrinsic_whitelist( value.whitelisted_intrinsics, row.whitelisted_intrinsics );

         value.num_supported_key_types = row.num_supported_key_types;
      }

   }

}}
