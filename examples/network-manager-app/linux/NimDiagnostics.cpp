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

#include "NimDiagnostics.h"

#include <cstdio>
#include <cstring>
#include <sys/sysinfo.h>

namespace chip {

using DeviceLayer::NetworkInterface;
using InterfaceType = app::Clusters::GeneralDiagnostics::InterfaceTypeEnum;

NimDiagnosticsProvider & NimDiagnosticsProvider::Instance()
{
    static NimDiagnosticsProvider sInstance;
    return sInstance;
}

CHIP_ERROR NimDiagnosticsProvider::GetNetworkInterfaces(NetworkInterface ** netifpp)
{
    NetworkInterface * all = nullptr;
    ReturnErrorOnFailure(DiagnosticDataProviderImpl::GetNetworkInterfaces(&all));

    NetworkInterface * primary = nullptr;
    NetworkInterface * rest    = nullptr;

    while (all != nullptr)
    {
        NetworkInterface * current = all;
        all                        = all->Next;

        if (mPrimary != nullptr && strcmp(current->Name, mPrimary) == 0)
        {
            // The bridge the node lives on. The kernel cannot type a
            // bridge; this node knows what it stands in for.
            current->type = InterfaceType::kEthernet;
            primary       = current;
            continue;
        }
        if (strncmp(current->Name, "wpan", 4) == 0)
        {
            current->type = InterfaceType::kThread;
        }
        else if (current->type != InterfaceType::kWiFi)
        {
            delete current;
            continue;
        }
        current->Next = rest;
        rest          = current;
    }

    if (primary != nullptr)
    {
        primary->Next = rest;
        rest          = primary;
    }

    *netifpp = rest;
    return CHIP_NO_ERROR;
}


CHIP_ERROR NimDiagnosticsProvider::ReadSysfs(const char * file, long long & value) const
{
    VerifyOrReturnError(mEthernetDiagnostics, CHIP_ERROR_READ_FAILED);
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/net/%s/%s", DiagnosticsInterface(), file);
    FILE * fp = fopen(path, "r");
    VerifyOrReturnError(fp != nullptr, CHIP_ERROR_READ_FAILED);
    int matched = fscanf(fp, "%lld", &value);
    fclose(fp);
    VerifyOrReturnError(matched == 1, CHIP_ERROR_READ_FAILED);
    return CHIP_NO_ERROR;
}

CHIP_ERROR NimDiagnosticsProvider::GetEthPHYRate(app::Clusters::EthernetNetworkDiagnostics::PHYRateEnum & pHYRate)
{
    using app::Clusters::EthernetNetworkDiagnostics::PHYRateEnum;
    long long speed = 0;
    ReturnErrorOnFailure(ReadSysfs("speed", speed));
    switch (speed)
    {
    case 10:
        pHYRate = PHYRateEnum::kRate10M;
        break;
    case 100:
        pHYRate = PHYRateEnum::kRate100M;
        break;
    case 1000:
        pHYRate = PHYRateEnum::kRate1G;
        break;
    case 2500:
        pHYRate = PHYRateEnum::kRate25g;
        break;
    case 5000:
        pHYRate = PHYRateEnum::kRate5G;
        break;
    case 10000:
        pHYRate = PHYRateEnum::kRate10G;
        break;
    default:
        return CHIP_ERROR_READ_FAILED;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR NimDiagnosticsProvider::GetEthFullDuplex(bool & fullDuplex)
{
    VerifyOrReturnError(mEthernetDiagnostics, CHIP_ERROR_READ_FAILED);
    char path[128];
    snprintf(path, sizeof(path), "/sys/class/net/%s/duplex", DiagnosticsInterface());
    FILE * fp = fopen(path, "r");
    VerifyOrReturnError(fp != nullptr, CHIP_ERROR_READ_FAILED);
    char duplex[16] = {};
    int matched     = fscanf(fp, "%15s", duplex);
    fclose(fp);
    VerifyOrReturnError(matched == 1, CHIP_ERROR_READ_FAILED);
    if (strcmp(duplex, "full") == 0)
    {
        fullDuplex = true;
    }
    else if (strcmp(duplex, "half") == 0)
    {
        fullDuplex = false;
    }
    else
    {
        // A bridge has no duplex of its own; null beats a made-up answer.
        return CHIP_ERROR_READ_FAILED;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR NimDiagnosticsProvider::GetEthCarrierDetect(bool & carrierDetect)
{
    long long carrier = 0;
    ReturnErrorOnFailure(ReadSysfs("carrier", carrier));
    carrierDetect = carrier != 0;
    return CHIP_NO_ERROR;
}

CHIP_ERROR NimDiagnosticsProvider::GetEthPacketRxCount(uint64_t & packetRxCount)
{
    long long value = 0;
    ReturnErrorOnFailure(ReadSysfs("statistics/rx_packets", value));
    packetRxCount = static_cast<uint64_t>(value);
    return CHIP_NO_ERROR;
}

CHIP_ERROR NimDiagnosticsProvider::GetEthPacketTxCount(uint64_t & packetTxCount)
{
    long long value = 0;
    ReturnErrorOnFailure(ReadSysfs("statistics/tx_packets", value));
    packetTxCount = static_cast<uint64_t>(value);
    return CHIP_NO_ERROR;
}

CHIP_ERROR NimDiagnosticsProvider::GetEthTxErrCount(uint64_t & txErrCount)
{
    long long value = 0;
    ReturnErrorOnFailure(ReadSysfs("statistics/tx_errors", value));
    txErrCount = static_cast<uint64_t>(value);
    return CHIP_NO_ERROR;
}

CHIP_ERROR NimDiagnosticsProvider::GetEthCollisionCount(uint64_t & collisionCount)
{
    long long value = 0;
    ReturnErrorOnFailure(ReadSysfs("statistics/collisions", value));
    collisionCount = static_cast<uint64_t>(value);
    return CHIP_NO_ERROR;
}

CHIP_ERROR NimDiagnosticsProvider::GetEthOverrunCount(uint64_t & overrunCount)
{
    long long value = 0;
    ReturnErrorOnFailure(ReadSysfs("statistics/rx_over_errors", value));
    overrunCount = static_cast<uint64_t>(value);
    return CHIP_NO_ERROR;
}

CHIP_ERROR NimDiagnosticsProvider::GetEthTimeSinceReset(uint64_t & timeSinceReset)
{
    VerifyOrReturnError(mEthernetDiagnostics, CHIP_ERROR_READ_FAILED);
    // The sysfs counters count from boot, so that is when they were reset.
    struct sysinfo info;
    VerifyOrReturnError(sysinfo(&info) == 0, CHIP_ERROR_READ_FAILED);
    timeSinceReset = static_cast<uint64_t>(info.uptime);
    return CHIP_NO_ERROR;
}

} // namespace chip
