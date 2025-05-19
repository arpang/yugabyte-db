//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_select_index.h"

#include "yb/util/atomic.h"
#include "yb/util/status_format.h"

#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/util/pg_doc_data.h"

bool yb_indexscan_batch_ybctids_across_requests;

namespace yb::pggate {

PgSelectIndex::PgSelectIndex(const PgSession::ScopedRefPtr& pg_session)
    : PgSelect(pg_session) {
}

Status PgSelectIndex::PrepareSubquery(
    const PgObjectId& index_id, std::shared_ptr<LWPgsqlReadRequestPB>&& read_req) {
  // Setup target and bind descriptor.
  target_ = bind_ = PgTable(VERIFY_RESULT(pg_session_->LoadTable(index_id)));

  read_req_ = std::move(read_req);
  read_req_->dup_table_id(index_id.GetYbTableId()); // TODO(LW_PERFORM)

  // Prepare index key columns.
  PrepareBinds();

  return Status::OK();
}

Result<std::optional<YbctidBatch>> PgSelectIndex::FetchYbctidBatch() {
  // Keep reading until we get one batch of ybctids or EOF.
  while (!VERIFY_RESULT(GetNextYbctidBatch())) {
    if (!VERIFY_RESULT(FetchDataFromServer())) {
      // Server returns no more rows.
      return std::nullopt;
    }
  }

  // Got the next batch of ybctids.
  DCHECK(!ybctids_.empty());

  AtomicFlagSleepMs(&FLAGS_TEST_inject_delay_between_prepare_ybctid_execute_batch_ybctid_ms);
  return YbctidBatch{ybctids_, read_req_->has_is_forward_scan()};
}

Result<bool> PgSelectIndex::GetNextYbctidBatch() {
  // std::vector<Slice>().swap(ybctids_);
  ybctids_.clear();
  int64_t count = 0;
  for (auto rowset_iter = rowsets_.begin(); rowset_iter != rowsets_.end();) {
    if (rowset_iter->is_eof()) {
      rowset_iter = rowsets_.erase(rowset_iter);
    } else if (count == 0 || (count + rowset_iter->row_count() <= yb_fetch_row_limit))  {
      // Write all found rows to ybctid array.
      count += rowset_iter->row_count();
      RETURN_NOT_OK(rowset_iter->ProcessSystemColumns(&ybctids_));
      if (!yb_indexscan_batch_ybctids_across_requests)
        break;
      ++rowset_iter;
    } else {
      break;
    }
  }

  return count > 0;
}

Result<std::unique_ptr<PgSelectIndex>> PgSelectIndex::Make(
    const PgSession::ScopedRefPtr& pg_session, const PgObjectId& index_id, bool is_region_local,
    std::shared_ptr<LWPgsqlReadRequestPB>&& read_req) {
  std::unique_ptr<PgSelectIndex> result{new PgSelectIndex{pg_session}};
  RETURN_NOT_OK(read_req
      ? result->PrepareSubquery(index_id, std::move(read_req))
      : result->Prepare(index_id, is_region_local));
  return result;
}

}  // namespace yb::pggate
