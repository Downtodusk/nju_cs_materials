/*------------------------------------------------------------------------------
 - Copyright (c) 2024. Websoft research group, Nanjing University.
 -
 - This program is free software: you can redistribute it and/or modify
 - it under the terms of the GNU General Public License as published by
 - the Free Software Foundation, either version 3 of the License, or
 - (at your option) any later version.
 -
 - This program is distributed in the hope that it will be useful,
 - but WITHOUT ANY WARRANTY; without even the implied warranty of
 - MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 - GNU General Public License for more details.
 -
 - You should have received a copy of the GNU General Public License
 - along with this program.  If not, see <https://www.gnu.org/licenses/>.
 -----------------------------------------------------------------------------*/

//
// Created by ziqi on 2024/7/28.
//

#include "table_manager.h"
#include "common/page.h"

#include <iostream>

namespace wsdb {
void TableManager::CreateTable(
    const std::string &db_name, const std::string &table_name, const RecordSchema &schema, StorageModel storage_model)
{
  if (schema.GetRecordLength() > MAX_REC_SIZE || schema.GetRecordLength() < 1) {
    WSDB_THROW(WSDB_RECLEN_ERROR, fmt::format("{}", schema.GetRecordLength()));
  }

  // 1. create and open table file
  DiskManager::CreateFile(FILE_NAME(db_name, table_name, TAB_SUFFIX));
  auto table_file = disk_manager_->OpenFile(FILE_NAME(db_name, table_name, TAB_SUFFIX));
  // 2. prepare table header
  TableHeader table_header;
  table_header.page_num_        = 1;
  table_header.first_free_page_ = INVALID_PAGE_ID;
  table_header.rec_num_         = 0;
  table_header.rec_size_        = schema.GetRecordLength();
  table_header.nullmap_size_    = BITMAP_SIZE(schema.GetFieldCount());
  // n = rec_per_page, PAGE_HDR_SIZE + BITMAP_SIZE(n) + n * (rec_size + nullmap_size) <= PAGE_SIZE
  table_header.rec_per_page_ = (BITMAP_WIDTH * (PAGE_SIZE - PAGE_HEADER_SIZE - 1) + 1) /
                               (1 + (table_header.rec_size_ + table_header.nullmap_size_) * BITMAP_WIDTH);
  table_header.field_num_   = schema.GetFieldCount();
  table_header.bitmap_size_ = BITMAP_SIZE(table_header.rec_per_page_);
  // 3. write table header to the zero page
  WriteTableHeader(table_file, table_header, schema);
  // 4. close table file
  disk_manager_->CloseFile(table_file);
}

void TableManager::DropTable(const std::string &db_name, const std::string &table_name)
{
  DiskManager::DestroyFile(FILE_NAME(db_name, table_name, TAB_SUFFIX));
}

TableHandleUptr TableManager::OpenTable(
    const std::string &db_name, const std::string &table_name, StorageModel storage_model)
{
  auto table_file    = disk_manager_->OpenFile(FILE_NAME(db_name, table_name, TAB_SUFFIX));
  auto file_hdr_data = new char[PAGE_SIZE];
  disk_manager_->ReadPage(table_file, FILE_HEADER_PAGE_ID, file_hdr_data);
  TableHeader      header;
  RecordSchemaUptr schema;
  char            *cursor = file_hdr_data;
  memcpy(&header, cursor, sizeof(TableHeader));
  cursor += sizeof(TableHeader);
  // parse field schemas, field is arranged as a formatted string:
  // field_name1:field_type1:field_size1:field_name2:field_type2:field_size2:...
  std::vector<RTField> fields;
  fields.reserve(header.field_num_);
  for (size_t i = 0; i < header.field_num_; ++i) {
    FieldSchema field;
    field.field_name_ = cursor;
    cursor += field.field_name_.size() + 1;
    field.field_type_ = *reinterpret_cast<FieldType *>(cursor);
    cursor += sizeof(FieldType);
    field.field_size_ = *reinterpret_cast<size_t *>(cursor);
    cursor += sizeof(size_t);
    fields.push_back({.field_ = field});
  }
  schema = std::make_unique<RecordSchema>(fields);
  delete[] file_hdr_data;
  return std::make_unique<TableHandle>(disk_manager_, buffer_pool_manager_, table_file, header, schema, storage_model);
}

void TableManager::CloseTable(const std::string &db_name, const TableHandle &table_handle)
{
  // 1. write table header to the zero page

  WriteTableHeader(table_handle.GetTableId(), table_handle.GetTableHeader(), table_handle.GetSchema());
  // 2. flush all pages to disk
	// std::cout << "breakpoint1" << table_handle.GetTableId() << std::endl;
  buffer_pool_manager_->FlushAllPages(table_handle.GetTableId());
  // delete all pages
	// std::cout << "breakpoint1" << std::endl;
  buffer_pool_manager_->DeleteAllPages(table_handle.GetTableId());
  // 3. close table file
	// std::cout << "breakpoint1" << std::endl;
  disk_manager_->CloseFile(table_handle.GetTableId());
}

void TableManager::WriteTableHeader(table_id_t tid, const TableHeader &header, const RecordSchema &schema)
{
  disk_manager_->WriteFile(tid, reinterpret_cast<const char *>(&header), sizeof(TableHeader), SEEK_SET);
  // 4. write schema following the table header
  // field_name1:field_type1:field_size1:field_name2:field_type2:field_size2:..
  for (size_t i = 0; i < schema.GetFieldCount(); ++i) {
    const FieldSchema &field = schema.GetFieldAt(i).field_;
    disk_manager_->WriteFile(tid, field.field_name_.c_str(), field.field_name_.size() + 1, SEEK_CUR);
    disk_manager_->WriteFile(tid, reinterpret_cast<const char *>(&field.field_type_), sizeof(FieldType), SEEK_CUR);
    disk_manager_->WriteFile(tid, reinterpret_cast<const char *>(&field.field_size_), sizeof(size_t), SEEK_CUR);
  }
}

auto TableManager::GetTableId(const std::string &db_name, const std::string &table_name) -> table_id_t
{
  return disk_manager_->GetFileId(FILE_NAME(db_name, table_name, TAB_SUFFIX));
}
}  // namespace wsdb