#pragma once
#include <eosio/chain/database_utils.hpp>
#include <eosio/chain/authority.hpp>
#include <eosio/chain/code_object.hpp>
#include <eosio/chain/block_timestamp.hpp>
#include <eosio/chain/abi_def.hpp>
#include <eosio/chain/chain_snapshot.hpp>

#include "multi_index_includes.hpp"

namespace eosio { namespace chain {

   struct snapshot_account_object_v6 {
      static constexpr uint32_t minimum_version = 2;
      static constexpr uint32_t maximum_version = 6;
      static_assert(chain_snapshot_header::minimum_compatible_version <= maximum_version, "snapshot_account_object_v6 is no longer needed");

      account_name         name; //< name should not be changed within a chainbase modifier lambda
      block_timestamp_type creation_date;
      shared_blob          abi;
   };
   struct snapshot_account_metadata_object_v6 {
      static constexpr uint32_t minimum_version = 2;
      static constexpr uint32_t maximum_version = 6;
      static_assert(chain_snapshot_header::minimum_compatible_version <= maximum_version, "snapshot_account_metadata_object_v6 is no longer needed");

      account_name          name; //< name should not be changed within a chainbase modifier lambda
      uint64_t              recv_sequence = 0;
      uint64_t              auth_sequence = 0;
      uint64_t              code_sequence = 0;
      uint64_t              abi_sequence  = 0;
      digest_type           code_hash;
      time_point            last_code_update;
      uint32_t              flags = 0;
      uint8_t               vm_type = 0;
      uint8_t               vm_version = 0;
   };

   class account_object : public chainbase::object<account_object_type, account_object> {
      OBJECT_CTOR(account_object)
      id_type              id;
      account_name         name; //< name should not be changed within a chainbase modifier lambda
      block_timestamp_type creation_date;
      uint64_t             recv_sequence = 0;
      uint64_t             auth_sequence = 0;
   };
   using account_id_type = account_object::id_type;

   struct by_name;
   using account_index = chainbase::shared_multi_index_container<
      account_object,
      indexed_by<
         ordered_unique<tag<by_id>, member<account_object, account_object::id_type, &account_object::id>>,
         ordered_unique<tag<by_name>, member<account_object, account_name, &account_object::name>>
      >
   >;

   class account_metadata_object : public chainbase::object<account_metadata_object_type, account_metadata_object>
   {
      OBJECT_CTOR(account_metadata_object,(abi));

      enum class flags_fields : uint32_t {
         privileged = 1
      };

      id_type               id;
      account_name          name; //< name should not be changed within a chainbase modifier lambda
      uint64_t              code_sequence = 0;
      uint64_t              abi_sequence  = 0;
      digest_type           code_hash;
      time_point            last_code_update;
      uint32_t              flags = 0;
      uint8_t               vm_type = 0;
      uint8_t               vm_version = 0;
      shared_blob           abi;

      bool is_privileged()const { return has_field( flags, flags_fields::privileged ); }

      void set_privileged( bool privileged )  {
         flags = set_field( flags, flags_fields::privileged, privileged );
      }

      void set_abi( const eosio::chain::abi_def& a ) {
         abi.resize_and_fill( fc::raw::pack_size( a ), [&a](char* data, std::size_t size) {
            fc::datastream<char*> ds( data, size );
            fc::raw::pack( ds, a );
         });
      }

      eosio::chain::abi_def get_abi()const {
         eosio::chain::abi_def a;
         EOS_ASSERT( abi.size() != 0, abi_not_found_exception, "No ABI set on account ${n}", ("n",name) );

         fc::datastream<const char*> ds( abi.data(), abi.size() );
         fc::raw::unpack( ds, a );
         return a;
      }
   };

   struct by_name;
   using account_metadata_index = chainbase::shared_multi_index_container<
      account_metadata_object,
      indexed_by<
         ordered_unique<tag<by_id>, member<account_metadata_object, account_metadata_object::id_type, &account_metadata_object::id>>,
         ordered_unique<tag<by_name>, member<account_metadata_object, account_name, &account_metadata_object::name>>
      >
   >;

   class account_ram_correction_object : public chainbase::object<account_ram_correction_object_type, account_ram_correction_object>
   {
      OBJECT_CTOR(account_ram_correction_object);

      id_type      id;
      account_name name; //< name should not be changed within a chainbase modifier lambda
      uint64_t     ram_correction = 0;
   };

   struct by_name;
   using account_ram_correction_index = chainbase::shared_multi_index_container<
      account_ram_correction_object,
      indexed_by<
         ordered_unique<tag<by_id>, member<account_ram_correction_object, account_ram_correction_object::id_type, &account_ram_correction_object::id>>,
         ordered_unique<tag<by_name>, member<account_ram_correction_object, account_name, &account_ram_correction_object::name>>
      >
   >;

   namespace config {
      template<>
      struct billable_size<account_metadata_object> {
         static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 2; ///< 2x indices id, name
         static const uint64_t value = 78 + overhead; ///< fixed field + overhead
      };
   }
} } // eosio::chain

CHAINBASE_SET_INDEX_TYPE(eosio::chain::account_object, eosio::chain::account_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::account_metadata_object, eosio::chain::account_metadata_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::account_ram_correction_object, eosio::chain::account_ram_correction_index)

FC_REFLECT(eosio::chain::account_object, (name)(creation_date)(recv_sequence)(auth_sequence))
FC_REFLECT(eosio::chain::account_metadata_object, (name)(code_sequence)(abi_sequence)
                                                  (code_hash)(last_code_update)(flags)(vm_type)(vm_version)(abi))
FC_REFLECT(eosio::chain::account_ram_correction_object, (name)(ram_correction))

FC_REFLECT(eosio::chain::snapshot_account_object_v6, (name)(creation_date)(abi))
FC_REFLECT(eosio::chain::snapshot_account_metadata_object_v6, (name)(recv_sequence)(auth_sequence)(code_sequence)(abi_sequence)
                                                  (code_hash)(last_code_update)(flags)(vm_type)(vm_version))