// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_APPS_QUIT_WITH_APPS_CONTROLLER_MAC_H_
#define CHROME_BROWSER_UI_COCOA_APPS_QUIT_WITH_APPS_CONTROLLER_MAC_H_

#include <memory>

#include "base/macros.h"
#include "ui/message_center/notification_delegate.h"

class Notification;
class PrefRegistrySimple;
class Profile;

// QuitWithAppsController checks whether any apps are running and shows a
// notification to quit all of them.
class QuitWithAppsController : public message_center::NotificationDelegate {
 public:
  static const char kQuitWithAppsNotificationID[];

  QuitWithAppsController();

  // NotificationDelegate interface.
  void Display() override;
  void Close(bool by_user) override;
  void Click() override;
  void ButtonClick(int button_index) override;

  // Attempt to quit Chrome. This will display a notification and return false
  // if there are apps running.
  bool ShouldQuit();

  // Register prefs used by QuitWithAppsController.
  static void RegisterPrefs(PrefRegistrySimple* registry);

 private:
  ~QuitWithAppsController() override;

  std::unique_ptr<Notification> notification_;
  // The Profile instance associated with the notification_. We need to cache
  // the instance here because when we want to cancel the notification we need
  // to provide the profile which was used to add the notification previously.
  // Not owned by this class.
  Profile* notification_profile_;

  // Whether to suppress showing the notification for the rest of the session.
  bool suppress_for_session_;

  // Display a notification when quitting Chrome with hosted apps running?
  bool hosted_app_quit_notification_;

  DISALLOW_COPY_AND_ASSIGN(QuitWithAppsController);
};

#endif  // CHROME_BROWSER_UI_COCOA_APPS_QUIT_WITH_APPS_CONTROLLER_MAC_H_
