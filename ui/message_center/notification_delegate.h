// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_MESSAGE_CENTER_NOTIFICATION_DELEGATE_H_
#define UI_MESSAGE_CENTER_NOTIFICATION_DELEGATE_H_

#include <memory>
#include <string>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string16.h"
#include "ui/message_center/message_center_export.h"

namespace message_center {

class MessageCenterController;
class MessageView;
class Notification;

// Delegate for a notification. This class has two roles: to implement callback
// methods for notification, and to provide an identity of the associated
// notification.
class MESSAGE_CENTER_EXPORT NotificationDelegate
    : public base::RefCountedThreadSafe<NotificationDelegate> {
 public:
  // To be called when the desktop notification is actually shown.
  virtual void Display();

  // To be called when the desktop notification is closed.  If closed by a
  // user explicitly (as opposed to timeout/script), |by_user| should be true.
  virtual void Close(bool by_user);

  // Returns true if the delegate can handle click event.
  virtual bool HasClickedListener();

  // To be called when a desktop notification is clicked.
  virtual void Click();

  // To be called when the user clicks a button in a notification.
  virtual void ButtonClick(int button_index);

  // To be called when the user types a reply to a notification.
  // TODO(crbug.com/599859) Support this feature in the message center -
  // currently it is only supported on Android.
  virtual void ButtonClickWithReply(int button_index,
                                    const base::string16& reply);

  // To be called when the user clicks the settings button in a notification.
  // Returns whether the settings click was handled by the delegate.
  virtual bool SettingsClick();

  // To be called in order to detect if a settings button should be displayed.
  // This also controls whether a context menu is enabled (as the context menu
  // is also used for controlling settings).
  virtual bool ShouldDisplaySettingsButton();

  // Called when the user attempts to disable the notification.
  virtual void DisableNotification();

#if defined(TOOLKIT_VIEWS) && !defined(OS_MACOSX)
  // To be called to construct the message view for notifications whose type is
  // NOTIFICATION_TYPE_CUSTOM.
  virtual std::unique_ptr<MessageView> CreateCustomMessageView(
      MessageCenterController* controller,
      const Notification& notification);
#endif

  // Indicates whether this notification should be displayed when there is
  // fullscreen content being displayed.
  virtual bool ShouldDisplayOverFullscreen() const;

 protected:
  virtual ~NotificationDelegate() {}

 private:
  friend class base::RefCountedThreadSafe<NotificationDelegate>;
};

// A simple notification delegate which invokes the passed closure when clicked.
class MESSAGE_CENTER_EXPORT HandleNotificationClickedDelegate
    : public NotificationDelegate {
 public:
  explicit HandleNotificationClickedDelegate(const base::Closure& closure);

  // message_center::NotificationDelegate overrides:
  void Click() override;
  bool HasClickedListener() override;

 protected:
  ~HandleNotificationClickedDelegate() override;

 private:
  std::string id_;
  base::Closure closure_;

  DISALLOW_COPY_AND_ASSIGN(HandleNotificationClickedDelegate);
};

// A notification delegate which invokes a callback when a notification button
// has been clicked.
class MESSAGE_CENTER_EXPORT HandleNotificationButtonClickDelegate
    : public NotificationDelegate {
 public:
  typedef base::Callback<void(int)> ButtonClickCallback;

  explicit HandleNotificationButtonClickDelegate(
      const ButtonClickCallback& button_callback);

  // message_center::NotificationDelegate overrides:
  void ButtonClick(int button_index) override;

 protected:
  ~HandleNotificationButtonClickDelegate() override;

 private:
  ButtonClickCallback button_callback_;

  DISALLOW_COPY_AND_ASSIGN(HandleNotificationButtonClickDelegate);
};

}  //  namespace message_center

#endif  // UI_MESSAGE_CENTER_NOTIFICATION_DELEGATE_H_
