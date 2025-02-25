// Copyright (c) 2013 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef THIRD_PARTY_LEVELDATABASE_ENV_CHROMIUM_H_
#define THIRD_PARTY_LEVELDATABASE_ENV_CHROMIUM_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/containers/circular_deque.h"
#include "base/containers/linked_list.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/metrics/histogram.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "port/port_chromium.h"
#include "util/mutexlock.h"

namespace base {
namespace trace_event {
class MemoryAllocatorDump;
class ProcessMemoryDump;
}  // namespace trace_event
}  // namespace base

namespace leveldb_env {

// These entries map to values in tools/metrics/histograms/histograms.xml. New
// values should be appended at the end.
enum MethodID {
  kSequentialFileRead,
  kSequentialFileSkip,
  kRandomAccessFileRead,
  kWritableFileAppend,
  kWritableFileClose,
  kWritableFileFlush,
  kWritableFileSync,
  kNewSequentialFile,
  kNewRandomAccessFile,
  kNewWritableFile,
  kDeleteFile,
  kCreateDir,
  kDeleteDir,
  kGetFileSize,
  kRenameFile,
  kLockFile,
  kUnlockFile,
  kGetTestDirectory,
  kNewLogger,
  kSyncParent,
  kGetChildren,
  kNewAppendableFile,
  kNumEntries
};

// leveldb::Status::Code values are mapped to these values for UMA logging.
// Do not change/delete these values as you will break reporting for older
// copies of Chrome. Only add new values to the end.
enum LevelDBStatusValue {
  LEVELDB_STATUS_OK = 0,
  LEVELDB_STATUS_NOT_FOUND,
  LEVELDB_STATUS_CORRUPTION,
  LEVELDB_STATUS_NOT_SUPPORTED,
  LEVELDB_STATUS_INVALID_ARGUMENT,
  LEVELDB_STATUS_IO_ERROR,
  LEVELDB_STATUS_MAX
};

LevelDBStatusValue GetLevelDBStatusUMAValue(const leveldb::Status& s);

// Create the default leveldb options object suitable for leveldb operations.
struct Options : public leveldb::Options {
  Options();
};

const char* MethodIDToString(MethodID method);

leveldb::Status MakeIOError(leveldb::Slice filename,
                            const std::string& message,
                            MethodID method,
                            base::File::Error error);
leveldb::Status MakeIOError(leveldb::Slice filename,
                            const std::string& message,
                            MethodID method);

enum ErrorParsingResult {
  METHOD_ONLY,
  METHOD_AND_BFE,
  NONE,
};

ErrorParsingResult ParseMethodAndError(const leveldb::Status& status,
                                       MethodID* method,
                                       base::File::Error* error);
int GetCorruptionCode(const leveldb::Status& status);
int GetNumCorruptionCodes();
std::string GetCorruptionMessage(const leveldb::Status& status);
bool IndicatesDiskFull(const leveldb::Status& status);

// Determine the appropriate leveldb write buffer size to use. The default size
// (4MB) may result in a log file too large to be compacted given the available
// storage space. This function will return smaller values for smaller disks,
// and the default leveldb value for larger disks.
//
// |disk_space| is the logical partition size (in bytes), and *not* available
// space. A value of -1 will return leveldb's default write buffer size.
extern size_t WriteBufferSize(int64_t disk_space);

class UMALogger {
 public:
  virtual void RecordErrorAt(MethodID method) const = 0;
  virtual void RecordOSError(MethodID method,
                             base::File::Error error) const = 0;
  virtual void RecordBytesRead(int amount) const = 0;
  virtual void RecordBytesWritten(int amount) const = 0;
};

class RetrierProvider {
 public:
  virtual int MaxRetryTimeMillis() const = 0;
  virtual base::HistogramBase* GetRetryTimeHistogram(MethodID method) const = 0;
  virtual base::HistogramBase* GetRecoveredFromErrorHistogram(
      MethodID method) const = 0;
};

class Semaphore;

class ChromiumEnv : public leveldb::Env,
                    public UMALogger,
                    public RetrierProvider {
 public:
  ChromiumEnv();

  typedef void(ScheduleFunc)(void*);

  virtual ~ChromiumEnv();

  virtual bool FileExists(const std::string& fname);
  virtual leveldb::Status GetChildren(const std::string& dir,
                                      std::vector<std::string>* result);
  virtual leveldb::Status DeleteFile(const std::string& fname);
  virtual leveldb::Status CreateDir(const std::string& name);
  virtual leveldb::Status DeleteDir(const std::string& name);
  virtual leveldb::Status GetFileSize(const std::string& fname, uint64_t* size);
  virtual leveldb::Status RenameFile(const std::string& src,
                                     const std::string& dst);
  virtual leveldb::Status LockFile(const std::string& fname,
                                   leveldb::FileLock** lock);
  virtual leveldb::Status UnlockFile(leveldb::FileLock* lock);
  virtual void Schedule(ScheduleFunc*, void* arg);
  virtual void StartThread(void (*function)(void* arg), void* arg);
  virtual leveldb::Status GetTestDirectory(std::string* path);
  virtual uint64_t NowMicros();
  virtual void SleepForMicroseconds(int micros);
  virtual leveldb::Status NewSequentialFile(const std::string& fname,
                                            leveldb::SequentialFile** result);
  virtual leveldb::Status NewRandomAccessFile(
      const std::string& fname,
      leveldb::RandomAccessFile** result);
  virtual leveldb::Status NewWritableFile(const std::string& fname,
                                          leveldb::WritableFile** result);
  virtual leveldb::Status NewAppendableFile(const std::string& fname,
                                            leveldb::WritableFile** result);
  virtual leveldb::Status NewLogger(const std::string& fname,
                                    leveldb::Logger** result);
  void SetReadOnlyFileLimitForTesting(int max_open_files);

 protected:
  explicit ChromiumEnv(const std::string& name);

  static const char* FileErrorString(base::File::Error error);

 private:
  void RecordErrorAt(MethodID method) const override;
  void RecordOSError(MethodID method, base::File::Error error) const override;
  void RecordBytesRead(int amount) const override;
  void RecordBytesWritten(int amount) const override;
  base::HistogramBase* GetOSErrorHistogram(MethodID method, int limit) const;
  void DeleteBackupFiles(const base::FilePath& dir);

  // File locks may not be exclusive within a process (e.g. on POSIX). Track
  // locks held by the ChromiumEnv to prevent access within the process.
  class LockTable {
   public:
    bool Insert(const std::string& fname) {
      leveldb::MutexLock l(&mu_);
      return locked_files_.insert(fname).second;
    }
    bool Remove(const std::string& fname) {
      leveldb::MutexLock l(&mu_);
      return locked_files_.erase(fname) == 1;
    }
   private:
    leveldb::port::Mutex mu_;
    std::set<std::string> locked_files_;
  };

  const int kMaxRetryTimeMillis;
  // BGThread() is the body of the background thread
  void BGThread();
  static void BGThreadWrapper(void* arg) {
    reinterpret_cast<ChromiumEnv*>(arg)->BGThread();
  }

  void RecordLockFileAncestors(int num_missing_ancestors) const;
  base::HistogramBase* GetMethodIOErrorHistogram() const;
  base::HistogramBase* GetLockFileAncestorHistogram() const;

  // RetrierProvider implementation.
  virtual int MaxRetryTimeMillis() const { return kMaxRetryTimeMillis; }
  virtual base::HistogramBase* GetRetryTimeHistogram(MethodID method) const;
  virtual base::HistogramBase* GetRecoveredFromErrorHistogram(
      MethodID method) const;

  base::FilePath test_directory_;

  std::string name_;
  std::string uma_ioerror_base_name_;

  base::Lock mu_;
  base::ConditionVariable bgsignal_;
  bool started_bgthread_;

  // Entry per Schedule() call
  struct BGItem {
    void* arg;
    void (*function)(void*);
  };
  using BGQueue = base::circular_deque<BGItem>;
  BGQueue queue_;
  LockTable locks_;
  std::unique_ptr<Semaphore> file_semaphore_;
};

// Tracks databases open via OpenDatabase() method and exposes them to
// memory-infra. The class is thread safe.
class DBTracker {
 public:
  enum SharedReadCacheUse : int {
    // Use for databases whose access pattern is dictated by browser code.
    SharedReadCacheUse_Browser = 0,
    // Use for databases whose access pattern is directly influenced by Web
    // APIs, like Indexed DB, etc.
    SharedReadCacheUse_Web,
    SharedReadCacheUse_Unified,   // When Web == Browser.
    SharedReadCacheUse_InMemory,  // Shared by all in-memory databases.
    SharedReadCacheUse_NumCacheUses
  };

  // DBTracker singleton instance.
  static DBTracker* GetInstance();

  // Returns the memory-infra dump for |tracked_db|. Can be used to attach
  // additional info to the database dump, or to properly attribute memory
  // usage in memory dump providers that also dump |tracked_db|.
  // Note that |tracked_db| should be a live database instance produced by
  // OpenDatabase() method or leveldb_env::OpenDB() function.
  static base::trace_event::MemoryAllocatorDump* GetOrCreateAllocatorDump(
      base::trace_event::ProcessMemoryDump* pmd,
      leveldb::DB* tracked_db);

  // Report counts to UMA.
  void UpdateHistograms();

  // Provides extra information about a tracked database.
  class TrackedDB : public leveldb::DB {
   public:
    // Name that OpenDatabase() was called with.
    virtual const std::string& name() const = 0;
  };

  // Opens a database and starts tracking it. As long as the opened database
  // is alive (i.e. its instance is not destroyed) the database is exposed to
  // memory-infra and is enumerated by VisitDatabases() method.
  // This function is an implementation detail of leveldb_env::OpenDB(), and
  // has similar guarantees regarding |dbptr| argument.
  leveldb::Status OpenDatabase(const leveldb::Options& options,
                               const std::string& name,
                               TrackedDB** dbptr);

 private:
  class MemoryDumpProvider;
  class TrackedDBImpl;

  using DatabaseVisitor = base::RepeatingCallback<void(TrackedDB*)>;

  friend class ChromiumEnvDBTrackerTest;
  FRIEND_TEST_ALL_PREFIXES(ChromiumEnvDBTrackerTest, IsTrackedDB);
  FRIEND_TEST_ALL_PREFIXES(ChromiumEnvDBTrackerTest, GetOrCreateAllocatorDump);

  DBTracker();
  ~DBTracker();

  static base::trace_event::MemoryAllocatorDump* GetOrCreateAllocatorDump(
      base::trace_event::ProcessMemoryDump* pmd,
      TrackedDB* db);

  // Calls |visitor| for each live database. The database is live from the
  // point it was returned from OpenDatabase() and up until its instance is
  // destroyed.
  // The databases may be visited in an arbitrary order.
  // This function takes a lock, preventing any database from being opened or
  // destroyed (but doesn't lock the databases themselves).
  void VisitDatabases(const DatabaseVisitor& visitor);

  // Checks if |db| is tracked.
  bool IsTrackedDB(const leveldb::DB* db) const;

  void DatabaseOpened(TrackedDBImpl* database, SharedReadCacheUse cache_use);
  void DatabaseDestroyed(TrackedDBImpl* database, SharedReadCacheUse cache_use);

  std::unique_ptr<MemoryDumpProvider> mdp_;

  // Protect databases_ and database_use_count_.
  mutable base::Lock databases_lock_;
  base::LinkedList<TrackedDBImpl> databases_;
  int database_use_count_[SharedReadCacheUse_NumCacheUses] = {};

  DISALLOW_COPY_AND_ASSIGN(DBTracker);
};

// Opens a database with the specified "name" and "options" (see note) and
// exposes it to Chrome's tracing (see DBTracker for details). The function
// guarantees that:
//   1. |dbptr| is not touched on failure
//   2. |dbptr| is not NULL on success
//
// Note: All |options| values are honored, except if options.env is an in-memory
// Env. In this case the block cache is disabled and a minimum write buffer size
// is used to conserve memory with all other values honored.
leveldb::Status OpenDB(const leveldb_env::Options& options,
                       const std::string& name,
                       std::unique_ptr<leveldb::DB>* dbptr);

base::StringPiece MakeStringPiece(const leveldb::Slice& s);
leveldb::Slice MakeSlice(const base::StringPiece& s);

}  // namespace leveldb_env

#endif  // THIRD_PARTY_LEVELDATABASE_ENV_CHROMIUM_H_
