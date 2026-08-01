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

#include <lib/support/CodeUtils.h>
#include <platform/KeyValueStoreManager.h>

#include <cstdio>
#include <cstring>
#include <sys/sysinfo.h>

namespace chip {

using DeviceLayer::BootReasonType;
using DeviceLayer::NetworkInterface;
using InterfaceType = app::Clusters::GeneralDiagnostics::InterfaceTypeEnum;

namespace {

// A UUID the kernel generates once per boot. Comparing it against the one
// seen last is the only portable way to tell a reboot of the router from a
// restart of this daemon.
constexpr char kBootIdPath[]    = "/proc/sys/kernel/random/boot_id";
constexpr char kBootIdKey[]     = "nim/boot-id";
constexpr char kRebootKey[]     = "nim/reboot-count";
constexpr char kBootReasonKey[] = "nim/boot-reason";
constexpr size_t kBootIdSize    = 36; // canonical UUID text, no terminator

// Set by the driver when the last reset came from the watchdog. Boards that
// cannot tell report zero, which stays "unspecified" rather than a guess.
constexpr char kWatchdogBootStatus[] = "/sys/class/watchdog/watchdog0/bootstatus";
constexpr long kWatchdogCardReset    = 0x0020; // WDIOF_CARDRESET

// In EthCounter order.
constexpr const char * kCounterFiles[] = {
    "statistics/rx_packets", "statistics/tx_packets", "statistics/tx_errors", "statistics/collisions", "statistics/rx_over_errors",
};

bool ReadFileBytes(const char * path, char * buffer, size_t size)
{
    FILE * fp = fopen(path, "r");
    VerifyOrReturnValue(fp != nullptr, false);
    size_t read = fread(buffer, 1, size, fp);
    fclose(fp);
    return read == size;
}

bool ReadFileNumber(const char * path, long & value)
{
    FILE * fp = fopen(path, "r");
    VerifyOrReturnValue(fp != nullptr, false);
    int matched = fscanf(fp, "%ld", &value);
    fclose(fp);
    return matched == 1;
}

} // namespace

NimDiagnosticsProvider & NimDiagnosticsProvider::Instance()
{
    static NimDiagnosticsProvider sInstance;
    return sInstance;
}

uint64_t NimDiagnosticsProvider::Uptime()
{
    struct sysinfo info;
    VerifyOrReturnValue(sysinfo(&info) == 0, 0);
    return static_cast<uint64_t>(info.uptime);
}

void NimDiagnosticsProvider::Init()
{
    auto & kvs = DeviceLayer::PersistedStorage::KeyValueStoreMgr();

    char bootId[kBootIdSize] = {};
    const bool haveBootId    = ReadFileBytes(kBootIdPath, bootId, sizeof(bootId));

    char storedId[kBootIdSize] = {};
    size_t storedSize          = 0;
    const bool sameBoot        = haveBootId && kvs.Get(kBootIdKey, storedId, sizeof(storedId), &storedSize) == CHIP_NO_ERROR &&
        storedSize == sizeof(storedId) && memcmp(bootId, storedId, sizeof(bootId)) == 0;

    uint16_t count = 0;
    (void) kvs.Get(kRebootKey, &count);
    uint8_t reason = static_cast<uint8_t>(BootReasonType::kUnspecified);
    (void) kvs.Get(kBootReasonKey, &reason);

    if (!sameBoot)
    {
        // The host has booted since this daemon last ran, so this is a
        // reboot in the sense the cluster means. Saturate rather than wrap:
        // a counter that rolls over to zero reads as a factory reset.
        if (count < UINT16_MAX)
        {
            count++;
        }

        long status              = 0;
        const bool watchdogReset = ReadFileNumber(kWatchdogBootStatus, status) && (status & kWatchdogCardReset) != 0;
        reason = static_cast<uint8_t>(watchdogReset ? BootReasonType::kHardwareWatchdogReset : BootReasonType::kUnspecified);

        (void) kvs.Put(kRebootKey, count);
        (void) kvs.Put(kBootReasonKey, reason);
        if (haveBootId)
        {
            (void) kvs.Put(kBootIdKey, bootId, sizeof(bootId));
        }
    }

    mRebootCount = count;
    mBootReason  = static_cast<BootReasonType>(reason);
}

CHIP_ERROR NimDiagnosticsProvider::GetRebootCount(uint16_t & rebootCount)
{
    rebootCount = mRebootCount;
    return CHIP_NO_ERROR;
}

CHIP_ERROR NimDiagnosticsProvider::GetBootReason(BootReasonType & bootReason)
{
    bootReason = mBootReason;
    return CHIP_NO_ERROR;
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
    VerifyOrReturnError(mEthernetDiagnostics, CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);
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
    VerifyOrReturnError(mEthernetDiagnostics, CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);
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

CHIP_ERROR NimDiagnosticsProvider::ReadCounter(EthCounter counter, uint64_t & value) const
{
    const size_t index = static_cast<size_t>(counter);

    long long raw = 0;
    VerifyOrReturnError(ReadSysfs(kCounterFiles[index], raw) == CHIP_NO_ERROR, CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);

    // The interface can be recreated under the same name, taking its
    // counters back to zero; a baseline above the reading then means the
    // count since the reset is all of it.
    const uint64_t current = static_cast<uint64_t>(raw);
    value                  = current >= mBaseline[index] ? current - mBaseline[index] : current;
    return CHIP_NO_ERROR;
}

CHIP_ERROR NimDiagnosticsProvider::GetEthPacketRxCount(uint64_t & packetRxCount)
{
    return ReadCounter(EthCounter::kPacketRx, packetRxCount);
}

CHIP_ERROR NimDiagnosticsProvider::GetEthPacketTxCount(uint64_t & packetTxCount)
{
    return ReadCounter(EthCounter::kPacketTx, packetTxCount);
}

CHIP_ERROR NimDiagnosticsProvider::GetEthTxErrCount(uint64_t & txErrCount)
{
    return ReadCounter(EthCounter::kTxErr, txErrCount);
}

CHIP_ERROR NimDiagnosticsProvider::GetEthCollisionCount(uint64_t & collisionCount)
{
    return ReadCounter(EthCounter::kCollision, collisionCount);
}

CHIP_ERROR NimDiagnosticsProvider::GetEthOverrunCount(uint64_t & overrunCount)
{
    return ReadCounter(EthCounter::kOverrun, overrunCount);
}

CHIP_ERROR NimDiagnosticsProvider::GetEthTimeSinceReset(uint64_t & timeSinceReset)
{
    VerifyOrReturnError(mEthernetDiagnostics, CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);
    // The sysfs counters run from boot, so that is when they last stood at
    // zero, unless ResetCounts has moved the baseline since.
    const uint64_t uptime = Uptime();
    timeSinceReset        = uptime >= mResetUptime ? uptime - mResetUptime : uptime;
    return CHIP_NO_ERROR;
}

CHIP_ERROR NimDiagnosticsProvider::ResetEthNetworkDiagnosticsCounts()
{
    VerifyOrReturnError(mEthernetDiagnostics, CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE);

    // The kernel counters belong to the whole system and cannot be zeroed
    // from here, so record where they stand and report the difference.
    for (size_t index = 0; index < kEthCounterCount; index++)
    {
        long long raw    = 0;
        mBaseline[index] = (ReadSysfs(kCounterFiles[index], raw) == CHIP_NO_ERROR) ? static_cast<uint64_t>(raw) : 0;
    }
    mResetUptime = Uptime();
    return CHIP_NO_ERROR;
}

} // namespace chip
