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

#include "UbusManager.h"

namespace chip {

namespace app {
class ThreadNetworkDirectoryStorage;
namespace Clusters {
class WiFiNetworkManagementCluster;
} // namespace Clusters
} // namespace app

// Publishes a "matter" ubus object exposing this node's onboarding
// information and local control of the commissioning window, so a router UI
// can pair the device with a controller without access to the daemon's log.
class MatterUbusService
{
public:
    using ReloadWifiHandler = void (*)(void * context);

    MatterUbusService(ubus::UbusManager & ubusManager) : mUbusManager(ubusManager) {}

    CHIP_ERROR Init();

    // Called from the reload_wifi ubus method; procd triggers it on wireless
    // configuration changes. Must be set before Init().
    void SetReloadWifiHandler(ReloadWifiHandler handler, void * context);

    // Sources for the sharing state reported by the status method. Must be
    // set before Init(); pass nullptr to omit the respective fields.
    void SetWiFiCluster(app::Clusters::WiFiNetworkManagementCluster * cluster);
    void SetThreadDirectory(app::ThreadNetworkDirectoryStorage * storage);

private:
    ubus::UbusManager & mUbusManager;
};

} // namespace chip
