// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_WEBUI_WEB_UI_IOS_DATA_SOURCE_IMPL_H_
#define IOS_WEB_WEBUI_WEB_UI_IOS_DATA_SOURCE_IMPL_H_

#include <map>
#include <string>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/values.h"
#include "ios/web/public/url_data_source_ios.h"
#include "ios/web/public/web_ui_ios_data_source.h"
#include "ios/web/webui/url_data_manager_ios.h"
#include "ios/web/webui/url_data_source_ios_impl.h"
#include "ui/base/template_expressions.h"

namespace web {

class WebUIIOSDataSourceImpl : public URLDataSourceIOSImpl,
                               public WebUIIOSDataSource {
 public:
  // WebUIIOSDataSource implementation:
  void AddString(const std::string& name, const base::string16& value) override;
  void AddString(const std::string& name, const std::string& value) override;
  void AddLocalizedString(const std::string& name, int ids) override;
  void AddLocalizedStrings(
      const base::DictionaryValue& localized_strings) override;
  void AddBoolean(const std::string& name, bool value) override;
  void SetJsonPath(const std::string& path) override;
  void AddResourcePath(const std::string& path, int resource_id) override;
  void SetDefaultResource(int resource_id) override;
  void DisableDenyXFrameOptions() override;

 protected:
  ~WebUIIOSDataSourceImpl() override;

  // Completes a request by sending our dictionary of localized strings.
  void SendLocalizedStringsAsJSON(
      const URLDataSourceIOS::GotDataCallback& callback);

  // Completes a request to |path| by sending the file specified by |idr| as the
  // response.
  void SendFromResourceBundle(const std::string& path,
                              const URLDataSourceIOS::GotDataCallback& callback,
                              int idr);

 private:
  class InternalDataSource;
  friend class InternalDataSource;
  friend class WebUIIOSDataSourceTest;
  friend class WebUIIOSDataSource;

  explicit WebUIIOSDataSourceImpl(const std::string& source_name);

  // Adds the locale to the load time data defaults. May be called repeatedly.
  void EnsureLoadTimeDataDefaultsAdded();

  // Methods that match URLDataSource which are called by
  // InternalDataSource.
  std::string GetSource() const;
  std::string GetMimeType(const std::string& path) const;
  void StartDataRequest(const std::string& path,
                        const URLDataSourceIOS::GotDataCallback& callback);

  // The name of this source.
  // E.g., for favicons, this could be "favicon", which results in paths for
  // specific resources like "favicon/34" getting sent to this source.
  std::string source_name_;
  int default_resource_;
  std::string json_path_;
  std::map<std::string, int> path_to_idr_map_;
  // The replacements are initiallized in the main thread and then used in the
  // IO thread. The map is safe to read from multiple threads as long as no
  // further changes are made to it after initialization.
  ui::TemplateReplacements replacements_;
  // The |replacements_| is intended to replace |localized_strings_|.
  base::DictionaryValue localized_strings_;
  bool deny_xframe_options_;
  bool load_time_data_defaults_added_;
  bool replace_existing_source_;

  DISALLOW_COPY_AND_ASSIGN(WebUIIOSDataSourceImpl);
};

}  // web

#endif  // IOS_WEB_WEBUI_WEB_UI_IOS_DATA_SOURCE_IMPL_H_
