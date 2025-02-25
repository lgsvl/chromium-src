// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_LAYERS_LAYER_CLIENT_H_
#define CC_LAYERS_LAYER_CLIENT_H_

#include <memory>

#include "cc/cc_export.h"

namespace base {
namespace trace_event {
class ConvertableToTraceFormat;
}
}

namespace cc {

class Layer;

class CC_EXPORT LayerClient {
 public:
  // Returns a pointer to a debug info object, if one has been computed.
  // If not, returns nullptr.
  // If the returned pointer is non-nullptr, the caller takes
  // ownership of the pointer.
  //
  // A pointer to the layer is provided for the convenience of layer clients
  // which service multiple layers.
  virtual std::unique_ptr<base::trace_event::ConvertableToTraceFormat>
  TakeDebugInfo(Layer* layer) = 0;
  virtual void didUpdateMainThreadScrollingReasons() = 0;
  virtual void didChangeScrollbarsHidden(bool) = 0;

  // Invoked when the layer's opacity has changed.
  virtual void DidChangeLayerOpacity(float old_opacity, float new_opacity) = 0;

 protected:
  virtual ~LayerClient() {}
};

}  // namespace cc

#endif  // CC_LAYERS_LAYER_CLIENT_H_
