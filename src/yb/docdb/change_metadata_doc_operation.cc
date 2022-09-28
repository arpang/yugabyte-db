// #include "yb/docdb/doc_expr.h"
// #include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_operation.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/common/schema.h"
#include "yb/tablet/metadata.pb.h"

namespace yb {

namespace docdb {

ChangeMetadataDocOperation::ChangeMetadataDocOperation(const tablet::TableInfoPB& table_info)
    : table_info_(table_info) {
  QLValuePB table_id_value;
  table_id_value.set_string_value(table_info_.table_id());

  std::string key_string;
  AppendToKey(table_id_value, &key_string);
  DocKeyHash hash = YBPartition::HashColumnCompoundValue(key_string);
  auto key = KeyEntryValue::FromQLValuePB(table_id_value, metadata_table_key_col.sorting_type());
  if (table_info.schema().table_properties().is_ysql_catalog_table()) {
    LOG(INFO) << "metadata_table_cotable_id " << metadata_table_cotable_id.ToString();
    doc_key_ = DocKey(metadata_table_cotable_id, hash, {key});
  } else {
    doc_key_ = DocKey(metadata_table_colocation_id, hash, {key});
  }
  encoded_doc_key_ = doc_key_->EncodeAsRefCntPrefix();
}

Status ChangeMetadataDocOperation::Apply(const DocOperationApplyData& data) {
  // tablet::TableInfoPB table_info_pb;
  // table_info_.ToPB(&table_info_pb);
  string table_info_pb_string;
  table_info_.SerializeToString(&table_info_pb_string);

  QLValuePB table_info_value;
  table_info_value.set_string_value(table_info_pb_string);

  // TODO: How to get this? Might want to refer to metrics and transactions table
  const ColumnId column_id(11);

  DocPath sub_path(encoded_doc_key_.as_slice(), KeyEntryValue::MakeColumnId(column_id));
  RETURN_NOT_OK(data.doc_write_batch->InsertSubDocument(
      sub_path, ValueRef(table_info_value, metadata_table_value_col.sorting_type()), data.read_time,
      data.deadline));
  return Status::OK();
}
}  // namespace docdb
}  // namespace yb