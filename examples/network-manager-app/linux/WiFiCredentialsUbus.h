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
#include <app/clusters/wifi-network-management-server/WiFiNetworkManagementCluster.h>

struct blob_attr;

namespace chip {

// Feeds the Wi-Fi Network Management cluster with the router's real access
// point credentials, read from netifd over ubus (network.wireless status).
//
// The access point to share is selected automatically: the first AP-mode
// interface on an enabled radio that is attached to the given network
// (usually the LAN, which is where Matter devices belong - guest networks
// are excluded by construction). A specific wifi-iface section can be
// pinned instead to override the automatic choice.
//
// The cluster owns change detection: SetNetworkCredentials() no-ops on
// identical values and bumps PassphraseSurrogate on change, so Refresh()
// can be called freely (it is invoked from the "matter" ubus object's
// reload_wifi method, triggered by procd on wireless config changes).
class WiFiCredentialsUbusProvider final
{
public:
    WiFiCredentialsUbusProvider(ubus::UbusManager & ubusManager,
                                app::Clusters::WiFiNetworkManagementCluster & cluster) :
        mUbusManager(ubusManager),
        mCluster(cluster)
    {}

    // network: netifd interface name whose access point is shared (auto mode).
    // section: optional uci wifi-iface section overriding the selection.
    // The strings must outlive the provider.
    CHIP_ERROR Init(const char * network, const char * section);

    // Re-reads the wireless status and updates the cluster.
    void Refresh();

private:
    void OnStatus(blob_attr * msg);
    bool SelectAccessPoint(blob_attr * radios, blob_attr *& section, blob_attr *& config);
    void Apply(blob_attr * config);

    ubus::UbusManager & mUbusManager;
    app::Clusters::WiFiNetworkManagementCluster & mCluster;
    ubus::UbusWatch mWireless{ "network.wireless", this };

    const char * mNetworkName  = nullptr;
    const char * mIfaceSection = nullptr;
};

} // namespace chip
