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

#include <platform/Linux/DiagnosticDataProviderImpl.h>

namespace chip {

// A router has dozens of kernel interfaces: VLANs, guest bridges, tunnels.
// Reporting them all in General Diagnostics buries the ones a controller
// can reason about and leaks the internal topology besides. This provider
// narrows the report to the interfaces that describe this node: the bridge
// it is reachable on first, then the Thread and Wi-Fi radios.
class NimDiagnosticsProvider : public DeviceLayer::DiagnosticDataProviderImpl
{
public:
    static NimDiagnosticsProvider & Instance();

    // Works out whether the host has rebooted since this daemon last ran.
    // Call after the stack is initialised: it reads and writes the key value
    // store.
    void Init();

    // The interface this node's Matter traffic actually uses. Reported
    // first, as Ethernet, so a controller picking "the" interface gets it.
    void SetPrimaryInterface(const char * name) { mPrimary = name; }

    // The interface whose state feeds the Ethernet diagnostics; defaults
    // to the primary interface.
    void SetDiagnosticsInterface(const char * name) { mDiagnostics = name; }

    // Diagnostics are readable by any fabric with View access; an operator
    // who considers the router's traffic counters nobody's business can
    // turn them off, which makes every reading null.
    void SetEthernetDiagnosticsEnabled(bool enabled) { mEthernetDiagnostics = enabled; }

    CHIP_ERROR GetNetworkInterfaces(DeviceLayer::NetworkInterface ** netifpp) override;

    // The platform counts every start of this daemon as a reboot, so procd
    // respawning it reads as the router restarting. Count host boots, and
    // say what the hardware can tell us about the last one.
    CHIP_ERROR GetRebootCount(uint16_t & rebootCount) override;
    CHIP_ERROR GetBootReason(DeviceLayer::BootReasonType & bootReason) override;

    // The stock implementation asks ethtool, which a bridge cannot answer,
    // so every reading comes back empty or zero. The kernel publishes the
    // real state of the primary interface in sysfs; serve that instead.
    CHIP_ERROR GetEthPHYRate(app::Clusters::EthernetNetworkDiagnostics::PHYRateEnum & pHYRate) override;
    CHIP_ERROR GetEthFullDuplex(bool & fullDuplex) override;
    CHIP_ERROR GetEthCarrierDetect(bool & carrierDetect) override;
    CHIP_ERROR GetEthPacketRxCount(uint64_t & packetRxCount) override;
    CHIP_ERROR GetEthPacketTxCount(uint64_t & packetTxCount) override;
    CHIP_ERROR GetEthTxErrCount(uint64_t & txErrCount) override;
    CHIP_ERROR GetEthCollisionCount(uint64_t & collisionCount) override;
    CHIP_ERROR GetEthOverrunCount(uint64_t & overrunCount) override;
    CHIP_ERROR GetEthTimeSinceReset(uint64_t & timeSinceReset) override;
    CHIP_ERROR ResetEthNetworkDiagnosticsCounts() override;

private:
    // The counters the Ethernet Network Diagnostics cluster requires of a
    // node claiming the packet and error count features.
    enum class EthCounter : uint8_t
    {
        kPacketRx,
        kPacketTx,
        kTxErr,
        kCollision,
        kOverrun,
        kCount
    };
    static constexpr size_t kEthCounterCount = static_cast<size_t>(EthCounter::kCount);

    CHIP_ERROR ReadSysfs(const char * file, long long & value) const;
    // Reads a counter net of the last ResetCounts. Anything the kernel does
    // not publish for this interface is reported as unsupported rather than
    // as a failure, which the cluster encodes as zero: these attributes are
    // mandatory, and a Failure status where a number belongs reads as a
    // broken node rather than as an unavailable statistic.
    CHIP_ERROR ReadCounter(EthCounter counter, uint64_t & value) const;
    const char * DiagnosticsInterface() const { return mDiagnostics != nullptr ? mDiagnostics : mPrimary; }
    static uint64_t Uptime();

    const char * mPrimary     = "br-lan";
    const char * mDiagnostics = nullptr;
    bool mEthernetDiagnostics = true;

    // The kernel counters cannot be zeroed, so ResetCounts records where
    // they stood and the readings are taken from there.
    uint64_t mBaseline[kEthCounterCount] = {};
    uint64_t mResetUptime                = 0;

    uint16_t mRebootCount                   = 0;
    DeviceLayer::BootReasonType mBootReason = DeviceLayer::BootReasonType::kUnspecified;
};

} // namespace chip
