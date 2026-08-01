/*
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

#include <platform/DeviceInstanceInfoProvider.h>

#include <string>

namespace chip {

// A router's identity is in its firmware, not in whoever compiled this
// daemon: OpenWrt publishes the device manufacturer and product in
// /etc/os-release and /tmp/sysinfo on every board. Serving Basic
// Information from there makes a commissioned router introduce itself as
// the hardware it is, on any OpenWrt device, without per-device builds.
class NimInstanceInfoProvider : public DeviceLayer::DeviceInstanceInfoProvider
{
public:
    static NimInstanceInfoProvider & Instance();

    // Reads the firmware identity and installs this provider in front of
    // the platform one. Call after the stack is initialised.
    void Init();

    // Overrides the firmware's manufacturer string (uci option).
    void SetVendorName(const char * name) { mVendorName = name; }

    // Overrides the firmware's distribution name (uci option).
    void SetProductName(const char * name) { mProductName = name; }

    CHIP_ERROR GetVendorName(char * buf, size_t bufSize) override;
    CHIP_ERROR GetVendorId(uint16_t & vendorId) override;
    CHIP_ERROR GetProductName(char * buf, size_t bufSize) override;
    CHIP_ERROR GetProductId(uint16_t & productId) override;
    CHIP_ERROR GetPartNumber(char * buf, size_t bufSize) override;
    CHIP_ERROR GetProductURL(char * buf, size_t bufSize) override;
    CHIP_ERROR GetProductLabel(char * buf, size_t bufSize) override;
    CHIP_ERROR GetSerialNumber(char * buf, size_t bufSize) override;
    CHIP_ERROR GetManufacturingDate(uint16_t & year, uint8_t & month, uint8_t & day) override;
    CHIP_ERROR GetHardwareVersion(uint16_t & hardwareVersion) override;
    CHIP_ERROR GetHardwareVersionString(char * buf, size_t bufSize) override;
    CHIP_ERROR GetRotatingDeviceIdUniqueId(MutableByteSpan & uniqueIdSpan) override;

private:
    CHIP_ERROR CopyOrDelegate(const std::string & value, char * buf, size_t bufSize,
                              CHIP_ERROR (DeviceLayer::DeviceInstanceInfoProvider::*fallback)(char *, size_t));

    DeviceLayer::DeviceInstanceInfoProvider * mFallback = nullptr;
    std::string mVendorName;
    std::string mProductName;
    std::string mProductUrl;
    std::string mHardware;
};

} // namespace chip
