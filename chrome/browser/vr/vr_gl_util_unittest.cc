// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/vr/vr_gl_util.h"

#include "chrome/browser/vr/test/constants.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/size_f.h"
#include "ui/gfx/transform.h"

namespace vr {

TEST(VrGlUtilTest, CalculateScreenSize) {
  gfx::Transform model_matrix;
  model_matrix.Translate3d(0.0f, -0.25f, -2.5f);
  gfx::SizeF size(2.4f, 1.6f);

  gfx::SizeF screen_size = CalculateScreenSize(kProjMatrix, model_matrix, size);

  EXPECT_FLOAT_EQ(screen_size.width(), 0.49592164);
  EXPECT_FLOAT_EQ(screen_size.height(), 0.27598655);
}

}  // namespace vr
