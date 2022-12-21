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

#include "yb/docdb/change_metadata_doc_operation.h"

namespace yb {

namespace docdb {

ChangeMetadataDocOperation::ChangeMetadataDocOperation(
    const Schema& metadata_schema, const std::string& table_id,
    const std::string serialized_table_info, bool is_delete)
    : metadata_schema_(metadata_schema),
      serialized_table_info_(serialized_table_info),
      is_delete_(is_delete) {
  QLValuePB table_id_value;
  table_id_value.set_string_value(table_id);
  std::string key_string;
  AppendToKey(table_id_value, &key_string);
  DocKeyHash hash = YBPartition::HashColumnCompoundValue(key_string);
  const auto& metadata_id_col_idx = metadata_schema_.find_column(kSysCatalogTableColId);
  DCHECK_NE(metadata_id_col_idx, Schema::kColumnNotFound);
  // todo: can we not do with SortingType::kNotSpecified
  auto hash_component = KeyEntryValue::FromQLValuePB(
      table_id_value, metadata_schema_.column(metadata_id_col_idx).sorting_type());
  // auto hash_component = KeyEntryValue::FromQLValuePB(table_id_value, SortingType::kNotSpecified);
  DocKey doc_key = DocKey(true, hash, {hash_component});
  encoded_doc_key_ = doc_key.EncodeAsRefCntPrefix();
}

Status ChangeMetadataDocOperation::Apply(const DocOperationApplyData& data) {
  const auto& metadata_col_id =
      VERIFY_RESULT(metadata_schema_.ColumnIdByName(kSysCatalogTableColMetadata));
  const auto& metadata_col = metadata_schema_.column_by_id(metadata_col_id);
  DocPath sub_path(encoded_doc_key_.as_slice(), KeyEntryValue::MakeColumnId(metadata_col_id));
  if (is_delete_) {
    RETURN_NOT_OK(data.doc_write_batch->DeleteSubDoc(sub_path, data.read_time, data.deadline));
  } else {
    QLValuePB table_info_value;
    table_info_value.set_string_value(serialized_table_info_);
    // RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
    //     sub_path, ValueRef(table_info_value, SortingType::kNotSpecified), data.read_time,
    //     data.deadline));
    // todo: can we not do with SortingType::kNotSpecified
    RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
        sub_path, ValueRef(table_info_value, metadata_col->sorting_type()), data.read_time,
        data.deadline));
  }
  return Status::OK();
}
}  // namespace docdb
}  // namespace yb
