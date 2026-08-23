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

#include "NimBackendUbus.h"

#include <lib/support/CodeUtils.h>

namespace chip {

namespace {

UbusNimBackend sBackend;

} // namespace

NimBackend & GetNimBackend()
{
    return sBackend;
}

CHIP_ERROR UbusNimBackend::EarlyInit()
{
    return mUbusManager.Init();
}

app::Clusters::ThreadBorderRouterManagement::Delegate & UbusNimBackend::BorderRouterDelegate()
{
    if (!mBorderRouterDelegate.has_value())
    {
        mBorderRouterDelegate.emplace(mUbusManager);
    }
    return *mBorderRouterDelegate;
}

CHIP_ERROR UbusNimBackend::StartWiFiCredentialSharing(app::Clusters::WiFiNetworkManagementCluster & cluster)
{
    // Nothing reads the router's access point configuration yet.
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR UbusNimBackend::Start()
{
    // Publish the "matter" ubus object once the server is up; its handlers
    // read commissioning state owned by the server.
    return mService.Init();
}

void UbusNimBackend::Shutdown()
{
    mUbusManager.Shutdown();
}

} // namespace chip
