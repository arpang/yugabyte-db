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

#pragma once

#include <boost/container/small_vector.hpp>

#include "yb/common/ql_value.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.pb.h"

#include "yb/docdb/doc_path.h"
#include "yb/docdb/docdb_fwd.h"

#include "yb/tablet/metadata.pb.h"
#include "yb/tablet/operations/change_metadata_operation.h"
#include "yb/util/monotime.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/docdb/doc_key.h"
#include "yb/util/yb_partition.h"
#include "yb/docdb/doc_write_batch.h"
#include "yb/common/schema.h"

namespace yb {
namespace docdb {

struct DocOperationApplyData {
  DocWriteBatch* doc_write_batch;
  CoarseTimePoint deadline;
  ReadHybridTime read_time;
  HybridTime* restart_read_ht;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(deadline, read_time, restart_read_ht);
  }
};

// When specifiying the parent key, the constant -1 is used for the subkey index.
const int kNilSubkeyIndex = -1;

typedef boost::container::small_vector_base<RefCntPrefix> DocPathsToLock;

YB_DEFINE_ENUM(GetDocPathsMode, (kLock)(kIntents));
YB_DEFINE_ENUM(
    DocOperationType,
    (PGSQL_WRITE_OPERATION)
    (QL_WRITE_OPERATION)
    (REDIS_WRITE_OPERATION)
    (CHANGE_METADATA_DOC_OPERATION)
  );

class DocOperation {
 public:
  typedef DocOperationType Type;

  virtual ~DocOperation() {}

  // Does the operation require a read snapshot to be taken before being applied? If so, a
  // clean snapshot hybrid_time will be supplied when Apply() is called. For example,
  // QLWriteOperation for a DML with a "... IF <condition> ..." clause needs to read the row to
  // evaluate the condition before the write and needs a read snapshot for a consistent read.
  virtual bool RequireReadSnapshot() const = 0;

  // Returns doc paths for this operation and isolation level this operation.
  // Doc paths are added to the end of paths, i.e. paths content is not cleared before it.
  //
  // Returned doc paths are controlled by mode argument:
  //   kLock - paths should be locked for this operation.
  //   kIntents - paths that should be used when writing intents, i.e. for conflict resolution.
  virtual Status GetDocPaths(
      GetDocPathsMode mode, DocPathsToLock *paths, IsolationLevel *level) const = 0;

  virtual Status Apply(const DocOperationApplyData& data) = 0;
  virtual Type OpType() = 0;
  virtual void ClearResponse() = 0;

  virtual std::string ToString() const = 0;
};

template <DocOperationType OperationType, class RequestPB>
class DocOperationBase : public DocOperation {
 public:
  explicit DocOperationBase(std::reference_wrapper<const RequestPB> request) : request_(request) {}

  Type OpType() override {
    return OperationType;
  }

  std::string ToString() const override {
    return Format("$0 { request: $1 }", OperationType, request_);
  }

 protected:
  const RequestPB& request_;
};


class ChangeMetadataDocOperation : public DocOperation {
 public:
  // Cannot make serialized_table_info a reference. The serialized string is not guaranteed to exist
  // by the time Apply is called. See Tablet::AddMultipleTables for instance.
  ChangeMetadataDocOperation(
      // const tablet::MetadataChange metadata_change,
      const std::string& table_id, const std::string serialized_table_info, bool is_delete = false);

  Status Apply(const DocOperationApplyData& data) override;

  Type OpType() override { return DocOperationType::CHANGE_METADATA_DOC_OPERATION; }

  bool RequireReadSnapshot() const override { return false; }

  Status GetDocPaths(
      GetDocPathsMode mode, DocPathsToLock* paths, IsolationLevel* level) const override {
    paths->push_back(encoded_doc_key_);
    return Status::OK();
  }

  void ClearResponse() override {
    // NOOP
  }

  std::string ToString() const override {
    return "";  // TODO
  }

 private:
  const std::string serialized_table_info_;
  RefCntPrefix encoded_doc_key_;
  bool is_delete_ = false;
  // const tablet::MetadataChange metadata_change_;
};

typedef std::vector<std::unique_ptr<DocOperation>> DocOperations;

}  // namespace docdb
}  // namespace yb
