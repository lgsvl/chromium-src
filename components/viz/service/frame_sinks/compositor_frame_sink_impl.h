// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_SERVICE_FRAME_SINKS_COMPOSITOR_FRAME_SINK_IMPL_H_
#define COMPONENTS_VIZ_SERVICE_FRAME_SINKS_COMPOSITOR_FRAME_SINK_IMPL_H_

#include "base/macros.h"
#include "components/viz/common/surfaces/frame_sink_id.h"
#include "components/viz/common/surfaces/local_surface_id.h"
#include "components/viz/service/frame_sinks/compositor_frame_sink_support.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "services/viz/public/interfaces/compositing/compositor_frame_sink.mojom.h"

namespace viz {

class FrameSinkManagerImpl;

// The viz portion of a non-root CompositorFrameSink. Holds the
// Binding/InterfacePtr for the mojom::CompositorFrameSink interface.
class CompositorFrameSinkImpl : public mojom::CompositorFrameSink {
 public:
  CompositorFrameSinkImpl(FrameSinkManagerImpl* frame_sink_manager,
                          const FrameSinkId& frame_sink_id,
                          mojom::CompositorFrameSinkRequest request,
                          mojom::CompositorFrameSinkClientPtr client);

  ~CompositorFrameSinkImpl() override;

  // mojom::CompositorFrameSink:
  void SetNeedsBeginFrame(bool needs_begin_frame) override;
  void SubmitCompositorFrame(const LocalSurfaceId& local_surface_id,
                             CompositorFrame frame,
                             mojom::HitTestRegionListPtr hit_test_region_list,
                             uint64_t submit_time) override;
  void DidNotProduceFrame(const BeginFrameAck& begin_frame_ack) override;

 private:
  void OnClientConnectionLost();

  mojom::CompositorFrameSinkClientPtr compositor_frame_sink_client_;
  mojo::Binding<mojom::CompositorFrameSink> compositor_frame_sink_binding_;

  // Must be destroyed before |compositor_frame_sink_client_|.
  std::unique_ptr<CompositorFrameSinkSupport> support_;

  DISALLOW_COPY_AND_ASSIGN(CompositorFrameSinkImpl);
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_SERVICE_FRAME_SINKS_COMPOSITOR_FRAME_SINK_IMPL_H_
