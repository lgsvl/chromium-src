// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/importer/import_lock_dialog_view.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/metrics/user_metrics.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "chrome/browser/importer/importer_lock_dialog.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/views/harmony/chrome_layout_provider.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/locale_settings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/ui_features.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/widget/widget.h"

using base::UserMetricsAction;

namespace importer {

#if !defined(OS_MACOSX) || BUILDFLAG(MAC_VIEWS_BROWSER)
void ShowImportLockDialog(gfx::NativeWindow parent,
                          const base::Callback<void(bool)>& callback) {
  ImportLockDialogView::Show(parent, callback);
}
#endif  // !OS_MACOSX || MAC_VIEWS_BROWSER

}  // namespace importer

// static
void ImportLockDialogView::Show(gfx::NativeWindow parent,
                                const base::Callback<void(bool)>& callback) {
  views::DialogDelegate::CreateDialogWidget(
      new ImportLockDialogView(callback), NULL, NULL)->Show();
  base::RecordAction(UserMetricsAction("ImportLockDialogView_Shown"));
}

ImportLockDialogView::ImportLockDialogView(
    const base::Callback<void(bool)>& callback)
    : callback_(callback) {
  SetLayoutManager(new views::FillLayout());
  views::Label* description_label =
      new views::Label(l10n_util::GetStringUTF16(IDS_IMPORTER_LOCK_TEXT));
  description_label->SetBorder(views::CreateEmptyBorder(
      ChromeLayoutProvider::Get()->GetDialogInsetsForContentType(views::TEXT,
                                                                 views::TEXT)));
  description_label->SetMultiLine(true);
  description_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  AddChildView(description_label);
  chrome::RecordDialogCreation(chrome::DialogIdentifier::IMPORT_LOCK);
}

ImportLockDialogView::~ImportLockDialogView() {
}

gfx::Size ImportLockDialogView::CalculatePreferredSize() const {
  const int width = ChromeLayoutProvider::Get()->GetDistanceMetric(
      DISTANCE_MODAL_DIALOG_WIDTH_CONTAINING_MULTILINE_TEXT);
  return gfx::Size(width, GetHeightForWidth(width));
}

base::string16 ImportLockDialogView::GetDialogButtonLabel(
    ui::DialogButton button) const {
  return l10n_util::GetStringUTF16((button == ui::DIALOG_BUTTON_OK) ?
      IDS_IMPORTER_LOCK_OK : IDS_IMPORTER_LOCK_CANCEL);
}

base::string16 ImportLockDialogView::GetWindowTitle() const {
  return l10n_util::GetStringUTF16(IDS_IMPORTER_LOCK_TITLE);
}

bool ImportLockDialogView::Accept() {
  if (callback_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(callback_, true));
  }
  return true;
}

bool ImportLockDialogView::Cancel() {
  if (callback_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::BindOnce(callback_, false));
  }
  return true;
}

bool ImportLockDialogView::ShouldShowCloseButton() const {
  return false;
}
