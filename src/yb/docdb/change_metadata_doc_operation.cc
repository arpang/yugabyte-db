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

ChangeMetadataDocOperation::ChangeMetadataDocOperation(
    // const tablet::TableInfoPtr& metadata_table,
    const tablet::TableInfoPB& table_info)
    : table_info_(table_info) {
  QLValuePB table_id_value;
  table_id_value.set_string_value(table_info_.table_id());

  std::string key_string;
  AppendToKey(table_id_value, &key_string);
  DocKeyHash hash = YBPartition::HashColumnCompoundValue(key_string);
  auto hash_component =
      KeyEntryValue::FromQLValuePB(table_id_value, metadata_table_key_col.sorting_type());
  // const auto& metadata_schema = metadata_table->doc_read_context->schema;
  // if (table_info.schema().table_properties().is_ysql_catalog_table()) {
  //   DCHECK(metadata_schema.has_cotable_id());
  //   doc_key_ = DocKey(metadata_schema.cotable_id(), hash, {key});
  // } else {
  //   DCHECK(metadata_schema.has_colocation_id());
  //   doc_key_ = DocKey(metadata_schema.colocation_id(), hash, {key});
  // }
  // encoded_doc_key_ = doc_key_->EncodeAsRefCntPrefix();
  // KeyBytes key_bytes;
  // key_bytes.AppendKeyEntryType(KeyEntryType::kTabletMetadata);
  DocKey doc_key = DocKey(true, hash, {hash_component});
  // doc_key.AppendTo(&key_bytes);
  encoded_doc_key_ = doc_key.EncodeAsRefCntPrefix();;
}

Status ChangeMetadataDocOperation::Apply(const DocOperationApplyData& data) {
  string table_info_pb_string;
  table_info_.SerializeToString(&table_info_pb_string);

  QLValuePB table_info_value;
  table_info_value.set_string_value(table_info_pb_string);

  DocPath sub_path(
      encoded_doc_key_.as_slice(), KeyEntryValue::MakeColumnId(metadata_table_value_col_id));
  RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
      sub_path, ValueRef(table_info_value, metadata_table_value_col.sorting_type()), data.read_time,
      data.deadline));
  return Status::OK();
}
}  // namespace docdb
}  // namespace yb
