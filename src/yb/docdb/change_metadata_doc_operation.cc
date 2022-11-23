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

#include "yb/docdb/doc_operation.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/common/schema.h"
#include "yb/tablet/metadata.pb.h"
#include "yb/docdb/doc_read_context.h"

namespace yb {

namespace docdb {

// TODO: Can replace metadata_change enum with just two: add/delete
ChangeMetadataDocOperation::ChangeMetadataDocOperation(
    const tablet::MetadataChange metadata_change, const std::string& table_id,
    const std::string& serialized_table_info)
    : serialized_table_info_(serialized_table_info), metadata_change_(metadata_change) {
  QLValuePB table_id_value;
  table_id_value.set_string_value(table_id);

  std::string key_string;
  AppendToKey(table_id_value, &key_string);
  DocKeyHash hash = YBPartition::HashColumnCompoundValue(key_string);
  auto hash_component =
      KeyEntryValue::FromQLValuePB(table_id_value, metadata_table_key_col.sorting_type());
  DocKey doc_key = DocKey(true, hash, {hash_component});
  encoded_doc_key_ = doc_key.EncodeAsRefCntPrefix();
}

Status ChangeMetadataDocOperation::Apply(const DocOperationApplyData& data) {
  DocPath sub_path(
      encoded_doc_key_.as_slice(), KeyEntryValue::MakeColumnId(metadata_table_value_col_id));
  switch (metadata_change_) {
    case tablet::BACKFILL_DONE:
      FALLTHROUGH_INTENDED;
    case tablet::SCHEMA:
      FALLTHROUGH_INTENDED;
    case tablet::ADD_MULTIPLE_TABLES:
      FALLTHROUGH_INTENDED;
    case yb::tablet::MetadataChange::ADD_TABLE: {
      QLValuePB table_info_value;
      table_info_value.set_string_value(serialized_table_info_);
      RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
          sub_path, ValueRef(table_info_value, metadata_table_value_col.sorting_type()),
          data.read_time, data.deadline));
      break;
    }
    case tablet::REMOVE_TABLE:
      RETURN_NOT_OK(data.doc_write_batch->DeleteSubDoc(sub_path, data.read_time, data.deadline));
      break;
    case tablet::NONE:
      break;
  }
  return Status::OK();
}
}  // namespace docdb
}  // namespace yb
