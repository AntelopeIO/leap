#include <eosio/chain/controller.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/pending_snapshot.hpp>
#include <eosio/chain/snapshot_scheduler.hpp>
#include <fc/scoped_exit.hpp>

namespace eosio {
namespace chain {

// snapshot_scheduler_listener
void snapshot_scheduler::on_start_block(uint32_t height, chain::controller& chain) {
   bool serialize_needed = false;
   bool snapshot_executed = false;

   auto execute_snapshot_with_log = [this, height, &snapshot_executed, &chain](const auto& req) {
      // one snapshot per height
      if(!snapshot_executed) {
         dlog("snapshot scheduler creating a snapshot from the request [start_block_num:${start_block_num}, end_block_num=${end_block_num}, block_spacing=${block_spacing}], height=${height}",
              ("start_block_num", req.start_block_num)("end_block_num", req.end_block_num)("block_spacing", req.block_spacing)("height", height));

         execute_snapshot(req.snapshot_request_id, chain);
         snapshot_executed = true;
      }
   };

   std::vector<uint32_t> unschedule_snapshot_request_ids;
   for(const auto& req: _snapshot_requests.get<0>()) {
      // -1 since its called from start block
      bool recurring_snapshot = req.block_spacing && (height >= req.start_block_num + 1) && (!((height - req.start_block_num - 1) % req.block_spacing));
      bool onetime_snapshot = (!req.block_spacing) && (height == req.start_block_num + 1);

      // assume "asap" for snapshot with missed/zero start, it can have spacing
      if(!req.start_block_num) {
         // update start_block_num with current height only if this is recurring
         // if non recurring, will be executed and unscheduled
         if(req.block_spacing && height) {
            auto& snapshot_by_id = _snapshot_requests.get<by_snapshot_id>();
            auto it = snapshot_by_id.find(req.snapshot_request_id);
            _snapshot_requests.modify(it, [&height](auto& p) { p.start_block_num = height - 1; });
            serialize_needed = true;
         }
         execute_snapshot_with_log(req);
      } else if(recurring_snapshot || onetime_snapshot) {
         execute_snapshot_with_log(req);
      }

      // cleanup - remove expired (or invalid) request
      if((!req.start_block_num && !req.block_spacing) ||
         (!req.block_spacing && height >= (req.start_block_num + 1)) ||
         (req.end_block_num > 0 && height >= (req.end_block_num + 1))) {
         unschedule_snapshot_request_ids.push_back(req.snapshot_request_id);
      }
   }

   for(const auto& i: unschedule_snapshot_request_ids) {
      unschedule_snapshot(i);
   }

   // store db to filesystem
   if(serialize_needed) x_serialize();
}

void snapshot_scheduler::on_irreversible_block(const signed_block_ptr& lib, const chain::controller& chain) {
   auto& snapshots_by_height = _pending_snapshot_index.get<by_height>();
   uint32_t lib_height = lib->block_num();

   while(!snapshots_by_height.empty() && snapshots_by_height.begin()->get_height() <= lib_height) {
      const auto& pending = snapshots_by_height.begin();
      auto next = pending->next;

      try {
         next(pending->finalize(chain));
      }
      CATCH_AND_CALL(next);

      snapshots_by_height.erase(snapshots_by_height.begin());
   }
}

snapshot_scheduler::snapshot_schedule_result snapshot_scheduler::schedule_snapshot(const snapshot_scheduler::snapshot_request_information& sri) {
   auto& snapshot_by_value = _snapshot_requests.get<by_snapshot_value>();
   auto existing = snapshot_by_value.find(std::make_tuple(sri.block_spacing, sri.start_block_num, sri.end_block_num));
   EOS_ASSERT(existing == snapshot_by_value.end(), chain::duplicate_snapshot_request, "Duplicate snapshot request");

   if(sri.end_block_num > 0) {
      // if "end" is specified, it should be greater then start
      EOS_ASSERT(sri.start_block_num <= sri.end_block_num, chain::invalid_snapshot_request, "End block number should be greater or equal to start block number");
      // if also block_spacing specified, check it
      if(sri.block_spacing > 0) {
         EOS_ASSERT(sri.start_block_num + sri.block_spacing <= sri.end_block_num, chain::invalid_snapshot_request, "Block spacing exceeds defined by start and end range");
      }
   }

   _snapshot_requests.emplace(snapshot_scheduler::snapshot_schedule_information{{_snapshot_id++}, {sri.block_spacing, sri.start_block_num, sri.end_block_num, sri.snapshot_description}, {}});
   x_serialize();

   // returning snapshot_schedule_result
   return snapshot_scheduler::snapshot_schedule_result{{_snapshot_id - 1}, {sri.block_spacing, sri.start_block_num, sri.end_block_num, sri.snapshot_description}};
}

snapshot_scheduler::snapshot_schedule_result snapshot_scheduler::unschedule_snapshot(uint32_t sri) {
   auto& snapshot_by_id = _snapshot_requests.get<by_snapshot_id>();
   auto existing = snapshot_by_id.find(sri);
   EOS_ASSERT(existing != snapshot_by_id.end(), chain::snapshot_request_not_found, "Snapshot request not found");

   snapshot_scheduler::snapshot_schedule_result result{{existing->snapshot_request_id}, {existing->block_spacing, existing->start_block_num, existing->end_block_num, existing->snapshot_description}};
   _snapshot_requests.erase(existing);
   x_serialize();

   // returning snapshot_schedule_result
   return result;
}

snapshot_scheduler::get_snapshot_requests_result snapshot_scheduler::get_snapshot_requests() {
   snapshot_scheduler::get_snapshot_requests_result result;
   auto& asvector = _snapshot_requests.get<as_vector>();
   result.snapshot_requests.reserve(asvector.size());
   result.snapshot_requests.insert(result.snapshot_requests.begin(), asvector.begin(), asvector.end());
   return result;
}

void snapshot_scheduler::set_db_path(bfs::path db_path) {
   _snapshot_db.set_path(std::move(db_path));
   // init from db
   if(std::filesystem::exists(_snapshot_db.get_json_path())) {
      std::vector<snapshot_scheduler::snapshot_schedule_information> sr;
      _snapshot_db >> sr;
      // if db read succeeded, clear/load
      _snapshot_requests.get<by_snapshot_id>().clear();
      _snapshot_requests.insert(sr.begin(), sr.end());
   }
}

void snapshot_scheduler::set_snapshots_path(bfs::path sn_path) {
   _snapshots_dir = std::move(sn_path);
}

void snapshot_scheduler::add_pending_snapshot_info(const snapshot_scheduler::snapshot_information& si) {
   auto& snapshot_by_id = _snapshot_requests.get<by_snapshot_id>();
   auto snapshot_req = snapshot_by_id.find(_inflight_sid);
   if(snapshot_req != snapshot_by_id.end()) {
      _snapshot_requests.modify(snapshot_req, [&si](auto& p) {
         p.pending_snapshots.emplace_back(si);
      });
   }
}

void snapshot_scheduler::execute_snapshot(uint32_t srid, chain::controller& chain) {
   _inflight_sid = srid;
   auto next = [srid, this](const chain::next_function_variant<snapshot_scheduler::snapshot_information>& result) {
      if(std::holds_alternative<fc::exception_ptr>(result)) {
         try {
            std::get<fc::exception_ptr>(result)->dynamic_rethrow_exception();
         } catch(const fc::exception& e) {
            EOS_THROW(snapshot_execution_exception,
                     "Snapshot creation error: ${details}",
                     ("details", e.to_detail_string()));
         } catch(const std::exception& e) {
            EOS_THROW(snapshot_execution_exception,
                     "Snapshot creation error: ${details}",
                     ("details", e.what()));
         }
      } else {
         // success, snapshot finalized
         auto snapshot_info = std::get<snapshot_scheduler::snapshot_information>(result);
         auto& snapshot_by_id = _snapshot_requests.get<by_snapshot_id>();
         auto snapshot_req = snapshot_by_id.find(srid);

         if(snapshot_req != snapshot_by_id.end()) {
            _snapshot_requests.modify(snapshot_req, [&](auto& p) {
               auto& pending = p.pending_snapshots;
               auto it = std::remove_if(pending.begin(), pending.end(), [&snapshot_info](const snapshot_scheduler::snapshot_information& s) { return s.head_block_num <= snapshot_info.head_block_num; });
               pending.erase(it, pending.end());
            });
         }
      }
   };
   create_snapshot(next, chain, {});
}

void snapshot_scheduler::create_snapshot(snapshot_scheduler::next_function<snapshot_scheduler::snapshot_information> next, chain::controller& chain, std::function<void(void)> predicate) {
   auto head_id = chain.head_block_id();
   const auto head_block_num = chain.head_block_num();
   const auto head_block_time = chain.head_block_time();
   const auto& snapshot_path = pending_snapshot<snapshot_scheduler::snapshot_information>::get_final_path(head_id, _snapshots_dir);
   const auto& temp_path = pending_snapshot<snapshot_scheduler::snapshot_information>::get_temp_path(head_id, _snapshots_dir);

   // maintain legacy exception if the snapshot exists
   if(bfs::is_regular_file(snapshot_path)) {
      auto ex = snapshot_exists_exception(FC_LOG_MESSAGE(error, "snapshot named ${name} already exists", ("name", _snapshots_dir)));
      next(ex.dynamic_copy_exception());
      return;
   }

   auto write_snapshot = [&](const bfs::path& p) -> void {
      if(predicate) predicate();
      bfs::create_directory(p.parent_path());
      auto snap_out = std::ofstream(p.generic_string(), (std::ios::out | std::ios::binary));
      auto writer = std::make_shared<ostream_snapshot_writer>(snap_out);
      chain.write_snapshot(writer);
      writer->finalize();
      snap_out.flush();
      snap_out.close();
   };

   // If in irreversible mode, create snapshot and return path to snapshot immediately.
   if(chain.get_read_mode() == db_read_mode::IRREVERSIBLE) {
      try {
         write_snapshot(temp_path);
         std::error_code ec;
         bfs::rename(temp_path, snapshot_path, ec);
         EOS_ASSERT(!ec, snapshot_finalization_exception,
                    "Unable to finalize valid snapshot of block number ${bn}: [code: ${ec}] ${message}",
                    ("bn", head_block_num)("ec", ec.value())("message", ec.message()));

         next(snapshot_information{head_id, head_block_num, head_block_time, chain_snapshot_header::current_version, snapshot_path.generic_string()});
      }
      CATCH_AND_CALL(next);
      return;
   }

   // Otherwise, the result will be returned when the snapshot becomes irreversible.

   // determine if this snapshot is already in-flight
   auto& pending_by_id = _pending_snapshot_index.get<by_id>();
   auto existing = pending_by_id.find(head_id);
   if(existing != pending_by_id.end()) {
      // if a snapshot at this block is already pending, attach this requests handler to it
      pending_by_id.modify(existing, [&next](auto& entry) {
         entry.next = [prev = entry.next, next](const next_function_variant<snapshot_scheduler::snapshot_information>& res) {
            prev(res);
            next(res);
         };
      });
   } else {
      const auto& pending_path = pending_snapshot<snapshot_scheduler::snapshot_information>::get_pending_path(head_id, _snapshots_dir);

      try {
         write_snapshot(temp_path);// create a new pending snapshot

         std::error_code ec;
         bfs::rename(temp_path, pending_path, ec);
         EOS_ASSERT(!ec, snapshot_finalization_exception,
                    "Unable to promote temp snapshot to pending for block number ${bn}: [code: ${ec}] ${message}",
                    ("bn", head_block_num)("ec", ec.value())("message", ec.message()));
         _pending_snapshot_index.emplace(head_id, next, pending_path.generic_string(), snapshot_path.generic_string());
         add_pending_snapshot_info(snapshot_scheduler::snapshot_information{head_id, head_block_num, head_block_time, chain_snapshot_header::current_version, pending_path.generic_string()});
      }
      CATCH_AND_CALL(next);
   }
}

}// namespace chain
}// namespace eosio
