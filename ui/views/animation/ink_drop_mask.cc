// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/animation/ink_drop_mask.h"

#include "cc/paint/paint_flags.h"
#include "ui/compositor/paint_recorder.h"
#include "ui/gfx/canvas.h"

namespace views {

// InkDropMask

InkDropMask::InkDropMask(const gfx::Size& layer_size)
    : layer_(ui::LAYER_TEXTURED) {
  layer_.set_delegate(this);
  layer_.SetBounds(gfx::Rect(layer_size));
  layer_.SetFillsBoundsOpaquely(false);
  layer_.set_name("InkDropMaskLayer");
}

InkDropMask::~InkDropMask() {
  layer_.set_delegate(nullptr);
}

void InkDropMask::UpdateLayerSize(const gfx::Size& new_layer_size) {
  layer_.SetBounds(gfx::Rect(new_layer_size));
}

void InkDropMask::OnDelegatedFrameDamage(const gfx::Rect& damage_rect_in_dip) {}

void InkDropMask::OnDeviceScaleFactorChanged(float old_device_scale_factor,
                                             float new_device_scale_factor) {}

// RoundRectInkDropMask

RoundRectInkDropMask::RoundRectInkDropMask(const gfx::Size& layer_size,
                                           const gfx::InsetsF& mask_insets,
                                           int corner_radius)
    : InkDropMask(layer_size),
      mask_insets_(mask_insets),
      corner_radius_(corner_radius) {}

void RoundRectInkDropMask::OnPaintLayer(const ui::PaintContext& context) {
  cc::PaintFlags flags;
  flags.setAlpha(255);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setAntiAlias(true);

  ui::PaintRecorder recorder(context, layer()->size());
  const float dsf = recorder.canvas()->UndoDeviceScaleFactor();

  gfx::RectF masking_bound(layer()->bounds());
  masking_bound.Inset(mask_insets_);

  const gfx::Rect masking_bound_scaled =
      gfx::ScaleToRoundedRect(gfx::ToNearestRect(masking_bound), dsf);
  recorder.canvas()->DrawRoundRect(masking_bound_scaled, corner_radius_ * dsf,
                                   flags);
}

// CircleInkDropMask

CircleInkDropMask::CircleInkDropMask(const gfx::Size& layer_size,
                                     const gfx::Point& mask_center,
                                     int mask_radius)
    : InkDropMask(layer_size),
      mask_center_(mask_center),
      mask_radius_(mask_radius) {}

void CircleInkDropMask::OnPaintLayer(const ui::PaintContext& context) {
  cc::PaintFlags flags;
  flags.setAlpha(255);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setAntiAlias(true);

  ui::PaintRecorder recorder(context, layer()->size());
  recorder.canvas()->DrawCircle(mask_center_, mask_radius_, flags);
}

}  // namespace views
