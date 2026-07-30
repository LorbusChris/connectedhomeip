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

#include "NimInstanceInfo.h"

#include <lib/support/CHIPMemString.h>

#include <fstream>
#include <sstream>

namespace chip {

namespace {

// Values in os-release are shell-quoted; the quotes are not part of them.
std::string Unquote(std::string value)
{
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::string OsReleaseField(const char * key)
{
    std::ifstream file("/etc/os-release");
    std::string line;
    while (std::getline(file, line))
    {
        auto eq = line.find('=');
        if (eq != std::string::npos && line.compare(0, eq, key) == 0)
        {
            return Unquote(line.substr(eq + 1));
        }
    }
    return "";
}

std::string FirstLine(const char * path)
{
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    return line;
}

} // namespace

NimInstanceInfoProvider & NimInstanceInfoProvider::Instance()
{
    static NimInstanceInfoProvider sInstance;
    return sInstance;
}

void NimInstanceInfoProvider::Init()
{
    mFallback = DeviceLayer::GetDeviceInstanceInfoProvider();

    if (mVendorName.empty())
    {
        mVendorName = OsReleaseField("OPENWRT_DEVICE_MANUFACTURER");
    }
    mProductUrl = OsReleaseField("OPENWRT_DEVICE_MANUFACTURER_URL");

    // The device the firmware runs on, with its revision when the firmware
    // knows one: "Turris Omnia (v0)".
    std::string product = OsReleaseField("OPENWRT_DEVICE_PRODUCT");
    if (product.empty())
    {
        product = FirstLine("/tmp/sysinfo/model");
    }
    if (product.empty())
    {
        product = FirstLine("/proc/device-tree/model");
    }
    if (!product.empty())
    {
        std::string revision = OsReleaseField("OPENWRT_DEVICE_REVISION");
        std::ostringstream hardware;
        hardware << product;
        if (!revision.empty())
        {
            hardware << " (" << revision << ")";
        }
        mHardware = hardware.str();
    }

    DeviceLayer::SetDeviceInstanceInfoProvider(this);
}

CHIP_ERROR NimInstanceInfoProvider::CopyOrDelegate(
    const std::string & value, char * buf, size_t bufSize,
    CHIP_ERROR (DeviceLayer::DeviceInstanceInfoProvider::*fallback)(char *, size_t))
{
    if (!value.empty())
    {
        VerifyOrReturnError(value.size() < bufSize, CHIP_ERROR_BUFFER_TOO_SMALL);
        Platform::CopyString(buf, bufSize, value.c_str());
        return CHIP_NO_ERROR;
    }
    VerifyOrReturnError(mFallback != nullptr, CHIP_ERROR_INCORRECT_STATE);
    return (mFallback->*fallback)(buf, bufSize);
}

CHIP_ERROR NimInstanceInfoProvider::GetVendorName(char * buf, size_t bufSize)
{
    return CopyOrDelegate(mVendorName, buf, bufSize, &DeviceLayer::DeviceInstanceInfoProvider::GetVendorName);
}

CHIP_ERROR NimInstanceInfoProvider::GetProductURL(char * buf, size_t bufSize)
{
    return CopyOrDelegate(mProductUrl, buf, bufSize, &DeviceLayer::DeviceInstanceInfoProvider::GetProductURL);
}

CHIP_ERROR NimInstanceInfoProvider::GetHardwareVersionString(char * buf, size_t bufSize)
{
    return CopyOrDelegate(mHardware, buf, bufSize, &DeviceLayer::DeviceInstanceInfoProvider::GetHardwareVersionString);
}

CHIP_ERROR NimInstanceInfoProvider::GetVendorId(uint16_t & vendorId)
{
    return mFallback->GetVendorId(vendorId);
}

CHIP_ERROR NimInstanceInfoProvider::GetProductName(char * buf, size_t bufSize)
{
    return mFallback->GetProductName(buf, bufSize);
}

CHIP_ERROR NimInstanceInfoProvider::GetProductId(uint16_t & productId)
{
    return mFallback->GetProductId(productId);
}

CHIP_ERROR NimInstanceInfoProvider::GetPartNumber(char * buf, size_t bufSize)
{
    return mFallback->GetPartNumber(buf, bufSize);
}

CHIP_ERROR NimInstanceInfoProvider::GetProductLabel(char * buf, size_t bufSize)
{
    return mFallback->GetProductLabel(buf, bufSize);
}

CHIP_ERROR NimInstanceInfoProvider::GetSerialNumber(char * buf, size_t bufSize)
{
    return mFallback->GetSerialNumber(buf, bufSize);
}

CHIP_ERROR NimInstanceInfoProvider::GetManufacturingDate(uint16_t & year, uint8_t & month, uint8_t & day)
{
    return mFallback->GetManufacturingDate(year, month, day);
}

CHIP_ERROR NimInstanceInfoProvider::GetHardwareVersion(uint16_t & hardwareVersion)
{
    return mFallback->GetHardwareVersion(hardwareVersion);
}

CHIP_ERROR NimInstanceInfoProvider::GetRotatingDeviceIdUniqueId(MutableByteSpan & uniqueIdSpan)
{
    return mFallback->GetRotatingDeviceIdUniqueId(uniqueIdSpan);
}

} // namespace chip
