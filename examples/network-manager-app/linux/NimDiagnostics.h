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

private:
    CHIP_ERROR ReadSysfs(const char * file, long long & value) const;
    const char * DiagnosticsInterface() const { return mDiagnostics != nullptr ? mDiagnostics : mPrimary; }

    const char * mPrimary     = "br-lan";
    const char * mDiagnostics = nullptr;
    bool mEthernetDiagnostics = true;
};

} // namespace chip
