/*
 *
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

#include "MatterUbusService.h"
#include "NimBackend.h"
#include "ThreadBROpenThreadUbus.h"
#include "ThreadDiagnosticsUbus.h"
#include "UbusManager.h"
#include "WiFiCredentialsUbus.h"

#include <optional>

namespace chip {

// OpenWrt: netifd for the access point credentials, otbr-agent for Thread,
// and a "matter" ubus object for the router's own UI, all over ubus.
class UbusNimBackend final : public NimBackend
{
public:
    const char * Name() const override { return "OpenWrt (ubus)"; }

    const ArgParser::OptionDef * OptionDefs() const override;
    const char * OptionHelp() const override;
    bool HandleOption(int identifier, const char * value) override;

    // The LAN bridge: where Matter devices live on an OpenWrt router.
    const char * DefaultPrimaryInterface() const override { return "br-lan"; }

    CHIP_ERROR EarlyInit() override;
    app::Clusters::ThreadBorderRouterManagement::Delegate & BorderRouterDelegate() override;
    void SetActiveDatasetObserver(ActiveDatasetObserver observer, void * context) override;
    CHIP_ERROR StartWiFiCredentialSharing(app::Clusters::WiFiNetworkManagementCluster & cluster) override;
    CHIP_ERROR Start(app::Clusters::WiFiNetworkManagementCluster * wifi,
                     app::ThreadNetworkDirectoryStorage * threadDirectory) override;
    void Shutdown() override;

private:
    ubus::UbusManager mUbusManager{};
    MatterUbusService mService{ mUbusManager };
    std::optional<OpenThreadUbusBorderRouterDelegate> mBorderRouterDelegate;
    std::optional<OtbrThreadNetworkDiagnosticsProvider> mThreadDiagnostics;
    std::optional<WiFiCredentialsUbusProvider> mWiFiCredentials;

    // The netifd network whose access point credentials are shared, and an
    // optional wifi-iface section overriding the automatic selection. Both
    // null means nothing is shared.
    const char * mWifiNetworkName  = "lan";
    const char * mWifiIfaceSection = nullptr;
};

} // namespace chip
