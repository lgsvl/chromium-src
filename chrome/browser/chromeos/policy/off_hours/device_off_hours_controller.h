// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_POLICY_OFF_HOURS_DEVICE_OFF_HOURS_CONTROLLER_H_
#define CHROME_BROWSER_CHROMEOS_POLICY_OFF_HOURS_DEVICE_OFF_HOURS_CONTROLLER_H_

#include <memory>
#include <vector>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/chromeos/policy/off_hours/off_hours_interval.h"
#include "chrome/browser/chromeos/policy/proto/chrome_device_policy.pb.h"
#include "chromeos/dbus/power_manager_client.h"
#include "chromeos/dbus/system_clock_client.h"

namespace policy {
namespace off_hours {

// The main class for handling "OffHours" policy turns "OffHours" mode on and
// off, handles server and client time, timezone.
//
// DeviceSettingsService is owner of this object. Use to get a reference:
// DeviceSettingsService::Get()->device_off_hours_controller()
//
// Device setting proto is changed in DeviceSettingsService and doesn't contain
// ignored device policies from DeviceOffHoursProto during "OffHours" mode. It
// is changed exactly in DeviceSettingsService because it's late to change
// policies in PrefValueMap and PolicyMap. The system will revert to the default
// behavior for the removed policies. And behavior of policies is handled during
// decoding process from proto to PolicyMap.
//
// "OffHours" mode is never on until device time is synchronized with
// network time because in this case device time could be incorrect.
class DeviceOffHoursController : public chromeos::SystemClockClient::Observer,
                                 public chromeos::PowerManagerClient::Observer {
 public:
  // Observer interface.
  class Observer {
   public:
    // Gets called when "OffHours" end time is changed.
    virtual void OnOffHoursEndTimeChanged() {}

   protected:
    virtual ~Observer() {}
  };

  // Creates a device off hours controller instance.
  DeviceOffHoursController();
  ~DeviceOffHoursController() override;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Return current "OffHours" mode status.
  bool is_off_hours_mode() const { return off_hours_mode_; }

  // Save actual "OffHours" intervals from |device_settings_proto| to
  // |off_hours_intervals_| and call "UpdateOffhoursMode()".
  void UpdateOffHoursPolicy(
      const enterprise_management::ChromeDeviceSettingsProto&
          device_settings_proto);

  // Return "OffHours" mode end time during "OffHours" mode is on. Return null
  // when "OffHours" mode is off.
  base::TimeTicks GetOffHoursEndTime() const { return off_hours_end_time_; }

  // chromeos::PowerManagerClient::Observer:
  void SuspendDone(const base::TimeDelta& sleep_duration) override;

  // chromeos::SystemClockClient::Observer:
  void SystemClockUpdated() override;

 private:
  // Run OnOffHoursEndTimeChanged() for observers.
  void NotifyOffHoursEndTimeChanged() const;

  // Called when "OffHours" mode is changed and ask DeviceSettingsService to
  // update current proto.
  void OffHoursModeIsChanged() const;

  // If device should be in "OffHours" mode based on the current time then apply
  // current "OffHours" interval and turn "OffHours" mode on otherwise turn
  // "OffHours" mode off and set timer to next update "OffHours" mode.
  void UpdateOffHoursMode();

  // Turn on and off "OffHours" mode and call "OffHoursModeIsChanged()" if
  // "OffHours" mode is changed.
  void SetOffHoursMode(bool off_hours_enabled);

  // Set "OffHours" mode end time.
  void SetOffHoursEndTime(base::TimeTicks off_hours_end_time);

  // Timer for update "OffHours" mode.
  void StartOffHoursTimer(base::TimeDelta delay);
  void StopOffHoursTimer();

  // Called once when the system clock service initially becomes available (or
  // immediately if it's already available).
  void SystemClockInitiallyAvailable(bool service_is_available);

  // Called when the system time synchronization status with network time is
  // changed.
  void NetworkSynchronizationUpdated(bool network_synchronized);

  base::ObserverList<Observer> observers_;

  // The main value of "OffHours" policy which indicates current "OffHours" mode
  // state.
  bool off_hours_mode_ = false;

  // "OffHours" mode end time. It is needed to show "OffHours" session limit
  // notification. When "OffHours" mode is off the value is null.
  base::TimeTicks off_hours_end_time_;

  // Timer for updating device settings at the begin of next “OffHours” interval
  // or at the end of current "OffHours" interval.
  base::OneShotTimer timer_;

  // Value is false until the system time is synchronized with network time.
  bool network_synchronized_ = false;

  // Current "OffHours" time intervals.
  std::vector<OffHoursInterval> off_hours_intervals_;

  base::WeakPtrFactory<DeviceOffHoursController> weak_ptr_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(DeviceOffHoursController);
};

}  // namespace off_hours
}  // namespace policy

#endif  // CHROME_BROWSER_CHROMEOS_POLICY_OFF_HOURS_DEVICE_OFF_HOURS_CONTROLLER_H_
