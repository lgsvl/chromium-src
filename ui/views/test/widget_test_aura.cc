// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/test/widget_test.h"

#include "build/build_config.h"
#include "ui/aura/client/focus_client.h"
#include "ui/aura/mus/window_tree_client.h"
#include "ui/aura/test/aura_test_helper.h"
#include "ui/aura/window.h"
#include "ui/aura/window_delegate.h"
#include "ui/aura/window_tree_host.h"
#include "ui/views/mus/mus_client.h"
#include "ui/views/widget/widget.h"

#if defined(USE_X11)
#include <X11/Xutil.h>
#include "ui/gfx/x/x11_types.h"  // nogncheck
#endif

#if defined(USE_X11)
#include "ui/views/widget/desktop_aura/desktop_window_tree_host_x11.h"
#endif

namespace views {
namespace test {

namespace {

// Perform a pre-order traversal of |children| and all descendants, looking for
// |first| and |second|. If |first| is found before |second|, return true.
// When a layer is found, it is set to null. Returns once |second| is found, or
// when there are no children left.
// Note that ui::Layer children are bottom-to-top stacking order.
bool FindLayersInOrder(const std::vector<ui::Layer*>& children,
                       const ui::Layer** first,
                       const ui::Layer** second) {
  for (const ui::Layer* child : children) {
    if (child == *second) {
      *second = nullptr;
      return *first == nullptr;
    }

    if (child == *first)
      *first = nullptr;

    if (FindLayersInOrder(child->children(), first, second))
      return true;

    // If second is cleared without success, exit early with failure.
    if (!*second)
      return false;
  }
  return false;
}

#if defined(OS_WIN)

struct FindAllWindowsData {
  std::vector<aura::Window*>* windows;
};

BOOL CALLBACK FindAllWindowsCallback(HWND hwnd, LPARAM param) {
  FindAllWindowsData* data = reinterpret_cast<FindAllWindowsData*>(param);
  if (aura::WindowTreeHost* host =
          aura::WindowTreeHost::GetForAcceleratedWidget(hwnd))
    data->windows->push_back(host->window());
  return TRUE;
}

#endif  // OS_WIN

std::vector<aura::Window*> GetAllTopLevelWindows() {
  std::vector<aura::Window*> roots;
#if defined(USE_X11)
  roots = DesktopWindowTreeHostX11::GetAllOpenWindows();
#endif
#if defined(OS_WIN)
  {
    FindAllWindowsData data = {&roots};
    EnumThreadWindows(GetCurrentThreadId(), FindAllWindowsCallback,
                      reinterpret_cast<LPARAM>(&data));
  }
#endif  // OS_WIN
  aura::test::AuraTestHelper* aura_test_helper =
      aura::test::AuraTestHelper::GetInstance();
  if (aura_test_helper)
    roots.push_back(aura_test_helper->root_window());

  if (MusClient::Get()) {
    auto mus_roots = MusClient::Get()->window_tree_client()->GetRoots();
    roots.insert(roots.end(), mus_roots.begin(), mus_roots.end());
  }
  return roots;
}

}  // namespace

// static
void WidgetTest::SimulateNativeActivate(Widget* widget) {
  gfx::NativeView native_view = widget->GetNativeView();
  aura::client::GetFocusClient(native_view)->FocusWindow(native_view);
}

// static
bool WidgetTest::IsNativeWindowVisible(gfx::NativeWindow window) {
  return window->IsVisible();
}

// static
bool WidgetTest::IsWindowStackedAbove(Widget* above, Widget* below) {
  EXPECT_TRUE(above->IsVisible());
  EXPECT_TRUE(below->IsVisible());

  ui::Layer* root_layer = above->GetNativeWindow()->GetRootWindow()->layer();

  // Traversal is bottom-to-top, so |below| should be found first.
  const ui::Layer* first = below->GetLayer();
  const ui::Layer* second = above->GetLayer();
  return FindLayersInOrder(root_layer->children(), &first, &second);
}

gfx::Size WidgetTest::GetNativeWidgetMinimumContentSize(Widget* widget) {
  if (IsMus())
    return widget->GetNativeWindow()->delegate()->GetMinimumSize();

  // On Windows, HWNDMessageHandler receives a WM_GETMINMAXINFO message whenever
  // the window manager is interested in knowing the size constraints. On
  // ChromeOS, it's handled internally. Elsewhere, the size constraints need to
  // be pushed to the window server when they change.
#if defined(OS_CHROMEOS) || defined(OS_WIN)
  return widget->GetNativeWindow()->delegate()->GetMinimumSize();
#elif defined(USE_X11)
  XSizeHints hints;
  long supplied_return;
  XGetWMNormalHints(
      gfx::GetXDisplay(),
      widget->GetNativeWindow()->GetHost()->GetAcceleratedWidget(), &hints,
      &supplied_return);
  return gfx::Size(hints.min_width, hints.min_height);
#else
  NOTREACHED();
  return gfx::Size();
#endif
}

// static
ui::EventSink* WidgetTest::GetEventSink(Widget* widget) {
  return widget->GetNativeWindow()->GetHost()->event_sink();
}

// static
ui::internal::InputMethodDelegate* WidgetTest::GetInputMethodDelegateForWidget(
    Widget* widget) {
  return widget->GetNativeWindow()->GetRootWindow()->GetHost();
}

// static
bool WidgetTest::IsNativeWindowTransparent(gfx::NativeWindow window) {
  return window->transparent();
}

// static
Widget::Widgets WidgetTest::GetAllWidgets() {
  Widget::Widgets all_widgets;
  for (aura::Window* window : GetAllTopLevelWindows())
    Widget::GetAllChildWidgets(window->GetRootWindow(), &all_widgets);
  return all_widgets;
}

}  // namespace test
}  // namespace views
