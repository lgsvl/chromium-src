// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTET_COMMON_DEVTOOLS_DEVTOOLS_NETWORK_CONDITIONS_H_
#define CONTET_COMMON_DEVTOOLS_DEVTOOLS_NETWORK_CONDITIONS_H_

#include <string>
#include <vector>

#include "base/macros.h"

#include "content/common/content_export.h"

namespace content {

// DevToolsNetworkConditions holds information about desired network conditions.
class CONTENT_EXPORT DevToolsNetworkConditions {
 public:
  DevToolsNetworkConditions();
  ~DevToolsNetworkConditions();

  explicit DevToolsNetworkConditions(bool offline);
  DevToolsNetworkConditions(bool offline,
                            double latency,
                            double download_throughput,
                            double upload_throughput);

  bool IsThrottling() const;

  bool offline() const { return offline_; }
  double latency() const { return latency_; }
  double download_throughput() const { return download_throughput_; }
  double upload_throughput() const { return upload_throughput_; }

 private:
  const bool offline_;
  const double latency_;
  const double download_throughput_;
  const double upload_throughput_;

  DISALLOW_COPY_AND_ASSIGN(DevToolsNetworkConditions);
};

}  // namespace content

#endif  // CONTET_COMMON_DEVTOOLS_DEVTOOLS_NETWORK_CONDITIONS_H_
