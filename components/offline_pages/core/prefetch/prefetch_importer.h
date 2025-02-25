// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_OFFLINE_PAGES_CORE_PREFETCH_PREFETCH_IMPORTER_H_
#define COMPONENTS_OFFLINE_PAGES_CORE_PREFETCH_PREFETCH_IMPORTER_H_

#include "components/offline_pages/core/prefetch/prefetch_types.h"

namespace offline_pages {

class PrefetchDispatcher;

// Interface to import the downloaded archive such that it can be rendered as
// offline page.
class PrefetchImporter {
 public:
  explicit PrefetchImporter(PrefetchDispatcher* dispatcher);
  virtual ~PrefetchImporter() = default;

  // Imports the downloaded archive by moving the file into archive directory
  // and creating an entry in the offline metadata database.
  virtual void ImportArchive(const PrefetchArchiveInfo& info) = 0;

 protected:
  void NotifyImportCompleted(int64_t offline_id, bool success);

 private:
  PrefetchDispatcher* dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(PrefetchImporter);
};

}  // namespace offline_pages

#endif  // COMPONENTS_OFFLINE_PAGES_CORE_PREFETCH_PREFETCH_IMPORTER_H_
