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

#include "yb/docdb/doc_operation.h"

namespace yb {

namespace docdb {
class ChangeMetadataDocOperation : public DocOperation {
 public:
  // Cannot make serialized_table_info a reference. The serialized string is not guaranteed to exist
  // by the time Apply is called. See Tablet::AddMultipleTables for instance.
  ChangeMetadataDocOperation(
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
};
}  // namespace docdb
}  // namespace yb
