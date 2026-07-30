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

#include <app/clusters/thread-network-diagnostics-server/ThreadNetworkDiagnosticsProvider.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

namespace chip {

namespace {

constexpr int kOptionWifiNetwork = NimBackend::kFirstOptionId + 0;
constexpr int kOptionWifiIface   = NimBackend::kFirstOptionId + 1;
constexpr int kOptionNoWifiShare = NimBackend::kFirstOptionId + 2;

const ArgParser::OptionDef sOptionDefs[] = {
    { "wifi-network", ArgParser::kArgumentRequired, kOptionWifiNetwork },
    { "wifi-iface", ArgParser::kArgumentRequired, kOptionWifiIface },
    { "no-wifi-share", ArgParser::kNoArgument, kOptionNoWifiShare },
    {},
};

const char sOptionHelp[] = "  --wifi-network <name>\n"
                           "       Share the Wi-Fi credentials of the access point attached to this\n"
                           "       netifd network (default: lan).\n"
                           "  --wifi-iface <section>\n"
                           "       Share the Wi-Fi credentials of this uci wifi-iface section,\n"
                           "       overriding the automatic selection.\n"
                           "  --no-wifi-share\n"
                           "       Do not share any Wi-Fi credentials.\n";

UbusNimBackend sBackend;

} // namespace

NimBackend & GetNimBackend()
{
    return sBackend;
}

const ArgParser::OptionDef * UbusNimBackend::OptionDefs() const
{
    return sOptionDefs;
}

const char * UbusNimBackend::OptionHelp() const
{
    return sOptionHelp;
}

bool UbusNimBackend::HandleOption(int identifier, const char * value)
{
    switch (identifier)
    {
    case kOptionWifiNetwork:
        mWifiNetworkName = value;
        return true;
    case kOptionWifiIface:
        mWifiIfaceSection = value;
        return true;
    case kOptionNoWifiShare:
        mWifiNetworkName  = nullptr;
        mWifiIfaceSection = nullptr;
        return true;
    default:
        return false;
    }
}

CHIP_ERROR UbusNimBackend::EarlyInit()
{
    ReturnErrorOnFailure(mUbusManager.Init());

    // Must be in place before endpoint initialization constructs the
    // Thread Network Diagnostics cluster: without an in-process Thread
    // stack, the default provider would report an unprovisioned device.
    mThreadDiagnostics.emplace(mUbusManager);
    ReturnErrorOnFailure(mThreadDiagnostics->Init());
    app::Clusters::ThreadNetworkDiagnostics::SetDefaultThreadNetworkDiagnosticsProvider(&*mThreadDiagnostics);
    return CHIP_NO_ERROR;
}

app::Clusters::ThreadBorderRouterManagement::Delegate & UbusNimBackend::BorderRouterDelegate()
{
    if (!mBorderRouterDelegate.has_value())
    {
        mBorderRouterDelegate.emplace(mUbusManager);
    }
    return *mBorderRouterDelegate;
}

void UbusNimBackend::SetActiveDatasetObserver(ActiveDatasetObserver observer, void * context)
{
    BorderRouterDelegate();
    mBorderRouterDelegate->SetActiveDatasetObserver(observer, context);
}

CHIP_ERROR UbusNimBackend::StartWiFiCredentialSharing(app::Clusters::WiFiNetworkManagementCluster & cluster)
{
    // The cluster serves the router's real access point credentials, kept in
    // sync from netifd; procd pokes reload_wifi on wireless config changes.
    // An empty network name with no override disables sharing entirely.
    const bool share = (mWifiNetworkName != nullptr && mWifiNetworkName[0] != '\0') || mWifiIfaceSection != nullptr;
    VerifyOrReturnError(share, CHIP_ERROR_NOT_IMPLEMENTED);

    ChipLogProgress(AppServer, "Wi-Fi credential source: network '%s', iface override '%s'", mWifiNetworkName,
                    mWifiIfaceSection != nullptr ? mWifiIfaceSection : "(none)");
    mWiFiCredentials.emplace(mUbusManager, cluster);
    ReturnErrorOnFailure(mWiFiCredentials->Init(mWifiNetworkName, mWifiIfaceSection));
    mService.SetReloadWifiHandler([](void * context) { static_cast<WiFiCredentialsUbusProvider *>(context)->Refresh(); },
                                  &*mWiFiCredentials);
    return CHIP_NO_ERROR;
}

CHIP_ERROR UbusNimBackend::Start(app::Clusters::WiFiNetworkManagementCluster * wifi,
                                 app::ThreadNetworkDirectoryStorage * threadDirectory)
{
    // Status sources for the ubus object: what the node currently shares.
    mService.SetWiFiCluster(wifi);
    mService.SetThreadDirectory(threadDirectory);

    // Publish the "matter" ubus object once the server is up; its handlers
    // read commissioning state owned by the server.
    return mService.Init();
}

void UbusNimBackend::Shutdown()
{
    mUbusManager.Shutdown();
}

} // namespace chip
