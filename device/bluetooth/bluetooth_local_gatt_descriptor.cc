// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/bluetooth_local_gatt_descriptor.h"

#include "base/logging.h"
#include "build/build_config.h"

namespace device {

#if !defined(OS_LINUX)
// static
base::WeakPtr<BluetoothLocalGattDescriptor>
BluetoothLocalGattDescriptor::Create(
    const BluetoothUUID& uuid,
    BluetoothGattCharacteristic::Permissions permissions,
    BluetoothLocalGattCharacteristic* characteristic) {
  NOTIMPLEMENTED();
  return nullptr;
}
#endif

BluetoothLocalGattDescriptor::BluetoothLocalGattDescriptor() {}

BluetoothLocalGattDescriptor::~BluetoothLocalGattDescriptor() {}

}  // namespace device
