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

#include <app/clusters/thread-border-router-management-server/thread-br-delegate.h>
#include <lib/core/CHIPError.h>

namespace chip {

namespace app {
namespace Clusters {
class WiFiNetworkManagementCluster;
} // namespace Clusters
} // namespace app

// The operating-system side of the network manager.
//
// A Network Infrastructure Manager does not join networks, it serves them: the
// access point credentials the Wi-Fi Network Management cluster hands out, the
// border router behind Thread Border Router Management, and a control surface
// the OS uses to open the commissioning window. Where those come from differs
// per OS -- otbr-agent and procd over ubus on OpenWrt; NetworkManager and
// otbr-agent over D-Bus elsewhere -- while the clusters and everything that
// reads the cluster state are the same everywhere. This interface is the seam
// between the two: main.cpp only knows this class, and the build selects one
// implementation of it.
class NimBackend
{
public:
    virtual ~NimBackend() = default;

    // A human-readable name, for the startup log.
    virtual const char * Name() const = 0;

    // Before the Matter stack initialises, once the command line is parsed.
    // Transport connections and providers the stack consults during its own
    // initialisation belong here.
    virtual CHIP_ERROR EarlyInit() = 0;

    // The delegate behind Thread Border Router Management. Owned by the
    // backend; constructed on first use, which the cluster init callback
    // makes.
    virtual app::Clusters::ThreadBorderRouterManagement::Delegate & BorderRouterDelegate() = 0;

    // Feeds the cluster with the access point credentials this node shares,
    // now and whenever they change. Returns CHIP_ERROR_NOT_IMPLEMENTED if this
    // backend has nothing to share; the cluster then holds no credentials.
    virtual CHIP_ERROR StartWiFiCredentialSharing(app::Clusters::WiFiNetworkManagementCluster & cluster) = 0;

    // After every cluster is up: publish the OS-facing control surface, if
    // any.
    virtual CHIP_ERROR Start() = 0;

    virtual void Shutdown() = 0;
};

// The backend this build was made for.
NimBackend & GetNimBackend();

} // namespace chip
