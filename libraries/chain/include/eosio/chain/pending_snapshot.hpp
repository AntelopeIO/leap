#pragma once

#include <eosio/chain/controller.hpp>
#include <eosio/chain/types.hpp>

#include <string>

namespace eosio::chain {

namespace fs = std::filesystem;

template<typename T>
class pending_snapshot {
public:
   using next_t = eosio::chain::next_function<T>;

   pending_snapshot(const chain::block_id_type& block_id, const next_t& next, std::string pending_path, std::string final_path)
       : block_id(block_id), next(next), pending_path(std::move(pending_path)), final_path(std::move(final_path)) {}

   uint32_t get_height() const {
      return chain::block_header::num_from_id(block_id);
   }

   static fs::path get_final_path(const chain::block_id_type& block_id, const fs::path& snapshots_dir) {
      return snapshots_dir / fc::format_string("snapshot-${id}.bin", fc::mutable_variant_object()("id", block_id));
   }

   static fs::path get_pending_path(const chain::block_id_type& block_id, const fs::path& snapshots_dir) {
      return snapshots_dir / fc::format_string(".pending-snapshot-${id}.bin", fc::mutable_variant_object()("id", block_id));
   }

   static fs::path get_temp_path(const chain::block_id_type& block_id, const fs::path& snapshots_dir) {
      return snapshots_dir / fc::format_string(".incomplete-snapshot-${id}.bin", fc::mutable_variant_object()("id", block_id));
   }

   T finalize(const chain::controller& chain) const {
      auto block_ptr = chain.fetch_block_by_id(block_id);
      auto in_chain = (bool) block_ptr;
      std::error_code ec;

      if(!in_chain) {
         fs::remove(fs::path(pending_path), ec);
         EOS_THROW(chain::snapshot_finalization_exception,
                   "Snapshotted block was forked out of the chain.  ID: ${block_id}",
                   ("block_id", block_id));
      }

      fs::rename(fs::path(pending_path), fs::path(final_path), ec);
      EOS_ASSERT(!ec, chain::snapshot_finalization_exception,
                 "Unable to finalize valid snapshot of block number ${bn}: [code: ${ec}] ${message}",
                 ("bn", get_height())("ec", ec.value())("message", ec.message()));

      return {block_id, block_ptr->block_num(), block_ptr->timestamp, chain::chain_snapshot_header::current_version, final_path};
   }

   chain::block_id_type block_id;
   next_t next;
   std::string pending_path;
   std::string final_path;
};
}// namespace eosio::chain
