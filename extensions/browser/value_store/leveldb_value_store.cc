// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/value_store/leveldb_value_store.h"

#include <inttypes.h>
#include <stdint.h>

#include <utility>

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/sys_string_conversions.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/threading/thread_restrictions.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/process_memory_dump.h"
#include "third_party/leveldatabase/env_chromium.h"
#include "third_party/leveldatabase/src/include/leveldb/iterator.h"
#include "third_party/leveldatabase/src/include/leveldb/write_batch.h"

using base::StringPiece;

namespace {

const char kInvalidJson[] = "Invalid JSON";
const char kCannotSerialize[] = "Cannot serialize value to JSON";

}  // namespace

LeveldbValueStore::LeveldbValueStore(const std::string& uma_client_name,
                                     const base::FilePath& db_path)
    : LazyLevelDb(uma_client_name, db_path) {
  base::trace_event::MemoryDumpManager::GetInstance()
      ->RegisterDumpProviderWithSequencedTaskRunner(
          this, "LeveldbValueStore", base::SequencedTaskRunnerHandle::Get(),
          base::trace_event::MemoryDumpProvider::Options());
}

LeveldbValueStore::~LeveldbValueStore() {
  base::ThreadRestrictions::AssertIOAllowed();
  base::trace_event::MemoryDumpManager::GetInstance()->UnregisterDumpProvider(
      this);
}

size_t LeveldbValueStore::GetBytesInUse(const std::string& key) {
  // Let SettingsStorageQuotaEnforcer implement this.
  NOTREACHED() << "Not implemented";
  return 0;
}

size_t LeveldbValueStore::GetBytesInUse(
    const std::vector<std::string>& keys) {
  // Let SettingsStorageQuotaEnforcer implement this.
  NOTREACHED() << "Not implemented";
  return 0;
}

size_t LeveldbValueStore::GetBytesInUse() {
  // Let SettingsStorageQuotaEnforcer implement this.
  NOTREACHED() << "Not implemented";
  return 0;
}

ValueStore::ReadResult LeveldbValueStore::Get(const std::string& key) {
  return Get(std::vector<std::string>(1, key));
}

ValueStore::ReadResult LeveldbValueStore::Get(
    const std::vector<std::string>& keys) {
  base::ThreadRestrictions::AssertIOAllowed();

  Status status = EnsureDbIsOpen();
  if (!status.ok())
    return MakeReadResult(status);

  std::unique_ptr<base::DictionaryValue> settings(new base::DictionaryValue());

  for (const std::string& key : keys) {
    std::unique_ptr<base::Value> setting;
    status.Merge(Read(key, &setting));
    if (!status.ok())
      return MakeReadResult(status);
    if (setting)
      settings->SetWithoutPathExpansion(key, std::move(setting));
  }

  return MakeReadResult(std::move(settings), status);
}

ValueStore::ReadResult LeveldbValueStore::Get() {
  base::ThreadRestrictions::AssertIOAllowed();

  Status status = EnsureDbIsOpen();
  if (!status.ok())
    return MakeReadResult(status);

  base::JSONReader json_reader;
  std::unique_ptr<base::DictionaryValue> settings(new base::DictionaryValue());

  std::unique_ptr<leveldb::Iterator> it(db()->NewIterator(read_options()));
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    std::string key = it->key().ToString();
    std::unique_ptr<base::Value> value =
        json_reader.Read(StringPiece(it->value().data(), it->value().size()));
    if (!value) {
      return MakeReadResult(
          Status(CORRUPTION, Delete(key).ok() ? VALUE_RESTORE_DELETE_SUCCESS
                                              : VALUE_RESTORE_DELETE_FAILURE,
                 kInvalidJson));
    }
    settings->SetWithoutPathExpansion(key, std::move(value));
  }

  if (!it->status().ok()) {
    status.Merge(ToValueStoreError(it->status()));
    return MakeReadResult(status);
  }

  return MakeReadResult(std::move(settings), status);
}

ValueStore::WriteResult LeveldbValueStore::Set(WriteOptions options,
                                               const std::string& key,
                                               const base::Value& value) {
  base::ThreadRestrictions::AssertIOAllowed();

  Status status = EnsureDbIsOpen();
  if (!status.ok())
    return MakeWriteResult(status);

  leveldb::WriteBatch batch;
  std::unique_ptr<ValueStoreChangeList> changes(new ValueStoreChangeList());
  status.Merge(AddToBatch(options, key, value, &batch, changes.get()));
  if (!status.ok())
    return MakeWriteResult(status);

  status.Merge(WriteToDb(&batch));
  return status.ok() ? MakeWriteResult(std::move(changes), status)
                     : MakeWriteResult(status);
}

ValueStore::WriteResult LeveldbValueStore::Set(
    WriteOptions options,
    const base::DictionaryValue& settings) {
  base::ThreadRestrictions::AssertIOAllowed();

  Status status = EnsureDbIsOpen();
  if (!status.ok())
    return MakeWriteResult(status);

  leveldb::WriteBatch batch;
  std::unique_ptr<ValueStoreChangeList> changes(new ValueStoreChangeList());

  for (base::DictionaryValue::Iterator it(settings);
       !it.IsAtEnd(); it.Advance()) {
    status.Merge(
        AddToBatch(options, it.key(), it.value(), &batch, changes.get()));
    if (!status.ok())
      return MakeWriteResult(status);
  }

  status.Merge(WriteToDb(&batch));
  return status.ok() ? MakeWriteResult(std::move(changes), status)
                     : MakeWriteResult(status);
}

ValueStore::WriteResult LeveldbValueStore::Remove(const std::string& key) {
  base::ThreadRestrictions::AssertIOAllowed();
  return Remove(std::vector<std::string>(1, key));
}

ValueStore::WriteResult LeveldbValueStore::Remove(
    const std::vector<std::string>& keys) {
  base::ThreadRestrictions::AssertIOAllowed();

  Status status = EnsureDbIsOpen();
  if (!status.ok())
    return MakeWriteResult(status);

  leveldb::WriteBatch batch;
  std::unique_ptr<ValueStoreChangeList> changes(new ValueStoreChangeList());

  for (const std::string& key : keys) {
    std::unique_ptr<base::Value> old_value;
    status.Merge(Read(key, &old_value));
    if (!status.ok())
      return MakeWriteResult(status);

    if (old_value) {
      changes->push_back(ValueStoreChange(key, std::move(old_value), nullptr));
      batch.Delete(key);
    }
  }

  leveldb::Status ldb_status = db()->Write(leveldb::WriteOptions(), &batch);
  if (!ldb_status.ok() && !ldb_status.IsNotFound()) {
    status.Merge(ToValueStoreError(ldb_status));
    return MakeWriteResult(status);
  }
  return MakeWriteResult(std::move(changes), status);
}

ValueStore::WriteResult LeveldbValueStore::Clear() {
  base::ThreadRestrictions::AssertIOAllowed();

  std::unique_ptr<ValueStoreChangeList> changes(new ValueStoreChangeList());

  ReadResult read_result = Get();
  if (!read_result->status().ok())
    return MakeWriteResult(read_result->status());

  base::DictionaryValue& whole_db = read_result->settings();
  while (!whole_db.empty()) {
    std::string next_key = base::DictionaryValue::Iterator(whole_db).key();
    std::unique_ptr<base::Value> next_value;
    whole_db.RemoveWithoutPathExpansion(next_key, &next_value);
    changes->push_back(
        ValueStoreChange(next_key, std::move(next_value), nullptr));
  }

  DeleteDbFile();
  return MakeWriteResult(std::move(changes), read_result->status());
}

bool LeveldbValueStore::WriteToDbForTest(leveldb::WriteBatch* batch) {
  Status status = EnsureDbIsOpen();
  if (!status.ok())
    return false;
  return WriteToDb(batch).ok();
}

bool LeveldbValueStore::OnMemoryDump(
    const base::trace_event::MemoryDumpArgs& args,
    base::trace_event::ProcessMemoryDump* pmd) {
  base::ThreadRestrictions::AssertIOAllowed();

  // Return true so that the provider is not disabled.
  if (!db())
    return true;

  std::string value;
  uint64_t size;
  bool res = db()->GetProperty("leveldb.approximate-memory-usage", &value);
  DCHECK(res);
  res = base::StringToUint64(value, &size);
  DCHECK(res);

  auto* dump = pmd->CreateAllocatorDump(base::StringPrintf(
      "extensions/value_store/%s/0x%" PRIXPTR, open_histogram_name().c_str(),
      reinterpret_cast<uintptr_t>(this)));
  dump->AddScalar(base::trace_event::MemoryAllocatorDump::kNameSize,
                  base::trace_event::MemoryAllocatorDump::kUnitsBytes, size);

  // All leveldb databases are already dumped by leveldb_env::DBTracker. Add
  // an edge to avoid double counting.
  auto* tracker_db =
      leveldb_env::DBTracker::GetOrCreateAllocatorDump(pmd, db());
  if (tracker_db)
    pmd->AddOwnershipEdge(dump->guid(), tracker_db->guid());

  return true;
}

ValueStore::Status LeveldbValueStore::AddToBatch(
    ValueStore::WriteOptions options,
    const std::string& key,
    const base::Value& value,
    leveldb::WriteBatch* batch,
    ValueStoreChangeList* changes) {
  bool write_new_value = true;

  if (!(options & NO_GENERATE_CHANGES)) {
    std::unique_ptr<base::Value> old_value;
    Status status = Read(key, &old_value);
    if (!status.ok())
      return status;
    if (!old_value || !old_value->Equals(&value)) {
      changes->push_back(
          ValueStoreChange(key, std::move(old_value), value.CreateDeepCopy()));
    } else {
      write_new_value = false;
    }
  }

  if (write_new_value) {
    std::string value_as_json;
    if (!base::JSONWriter::Write(value, &value_as_json))
      return Status(OTHER_ERROR, kCannotSerialize);
    batch->Put(key, value_as_json);
  }

  return Status();
}

ValueStore::Status LeveldbValueStore::WriteToDb(leveldb::WriteBatch* batch) {
  return ToValueStoreError(db()->Write(write_options(), batch));
}
