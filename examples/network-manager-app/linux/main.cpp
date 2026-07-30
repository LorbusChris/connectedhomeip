/*
 *    Copyright (c) 2023 Project CHIP Authors
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

#include <AppMain.h>
#include <app/clusters/network-identity-management-server/AuthenticatorDriver.h>
#include <app/clusters/network-identity-management-server/DefaultNetworkIdentityStorage.h>
#include <app/clusters/network-identity-management-server/NetworkIdentityManagementCluster.h>
#include <app/clusters/network-identity-management-server/RawKeyNetworkIdentityKeystore.h>
#include <app/clusters/thread-border-router-management-server/thread-border-router-management-server.h>
#include <app/clusters/thread-network-directory-server/thread-network-directory-server.h>
#include <app/clusters/wifi-network-management-server/wifi-network-management-server.h>
#include <app/server-cluster/ServerClusterInterfaceRegistry.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <lib/core/CHIPSafeCasts.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>

#if MATTER_ENABLE_UBUS
#include "MatterUbusService.h"
#include "NimDiagnostics.h"
#include "NimInstanceInfo.h"
#include "ThreadBROpenThreadUbus.h"
#include "ThreadDiagnosticsUbus.h"
#include "UbusManager.h"
#include "WiFiCredentialsUbus.h"
#else
#include "ThreadBRFake.h"
#endif

#include <optional>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

ByteSpan ByteSpanFromCharSpan(CharSpan span)
{
    return ByteSpan(Uint8::from_const_char(span.data()), span.size());
}

#if MATTER_ENABLE_UBUS
ubus::UbusManager gUbusManager{};
MatterUbusService gMatterUbusService{ gUbusManager };
std::optional<WiFiCredentialsUbusProvider> gWiFiCredentialsProvider;

// The netifd network whose access point credentials the Wi-Fi Network
// Management cluster shares, and an optional wifi-iface section override.
const char * gWifiNetworkName  = "lan";
const char * gWifiIfaceSection = nullptr;
bool gThreadManaged            = true;

// The interface this node is reachable on; drives the diagnostics report
// and the identity the ethernet layers pick on this multi-homed host.
const char * gPrimaryInterface = "br-lan";

constexpr uint16_t kOptionWifiNetwork = 0x1001;
constexpr uint16_t kOptionWifiIface   = 0x1002;
constexpr uint16_t kOptionNoWifiShare = 0x1003;
constexpr uint16_t kOptionNoThread    = 0x1004;
constexpr uint16_t kOptionPrimaryIface = 0x1005;
constexpr uint16_t kOptionVendorName   = 0x1006;
constexpr uint16_t kOptionDiagIface    = 0x1007;
constexpr uint16_t kOptionNoEthDiag    = 0x1008;

bool HandleAppOption(const char * program, ArgParser::OptionSet * options, int identifier, const char * name, const char * value)
{
    switch (identifier)
    {
    case kOptionWifiNetwork:
        gWifiNetworkName = value;
        return true;
    case kOptionWifiIface:
        gWifiIfaceSection = value;
        return true;
    case kOptionNoWifiShare:
        gWifiNetworkName  = nullptr;
        gWifiIfaceSection = nullptr;
        return true;
    case kOptionNoThread:
        gThreadManaged = false;
        return true;
    case kOptionPrimaryIface:
        gPrimaryInterface = value;
        setenv("CHIP_ETHERNET_INTERFACE", value, 1);
        return true;
    case kOptionVendorName:
        NimInstanceInfoProvider::Instance().SetVendorName(value);
        return true;
    case kOptionDiagIface:
        NimDiagnosticsProvider::Instance().SetDiagnosticsInterface(value);
        return true;
    case kOptionNoEthDiag:
        NimDiagnosticsProvider::Instance().SetEthernetDiagnosticsEnabled(false);
        return true;
    default:
        return false;
    }
}

ArgParser::OptionDef sAppOptionDefs[] = {
    { "wifi-network", ArgParser::kArgumentRequired, kOptionWifiNetwork },
    { "wifi-iface", ArgParser::kArgumentRequired, kOptionWifiIface },
    { "no-wifi-share", ArgParser::kNoArgument, kOptionNoWifiShare },
    { "no-thread", ArgParser::kNoArgument, kOptionNoThread },
    { "primary-interface", ArgParser::kArgumentRequired, kOptionPrimaryIface },
    { "vendor-name", ArgParser::kArgumentRequired, kOptionVendorName },
    { "diagnostics-interface", ArgParser::kArgumentRequired, kOptionDiagIface },
    { "no-ethernet-diagnostics", ArgParser::kNoArgument, kOptionNoEthDiag },
    {},
};

const char sAppOptionHelp[] = "  --wifi-network <name>\n"
                              "       Share the Wi-Fi credentials of the access point attached to this\n"
                              "       netifd network (default: lan).\n"
                              "  --wifi-iface <section>\n"
                              "       Share the Wi-Fi credentials of this uci wifi-iface section,\n"
                              "       overriding the automatic selection.\n"
                              "  --no-wifi-share\n"
                              "       Do not share any Wi-Fi credentials.\n"
                              "  --no-thread\n"
                              "       No Thread border router is present: do not record or share\n"
                              "       any Thread networks.\n"
                              "  --primary-interface <name>\n"
                              "       The interface this node is reachable on (default: br-lan).\n"
                              "  --vendor-name <name>\n"
                              "       Manufacturer reported in Basic Information, overriding the\n"
                              "       one the firmware states in /etc/os-release.\n"
                              "  --diagnostics-interface <name>\n"
                              "       Feed the Ethernet diagnostics from this interface instead of\n"
                              "       the primary one.\n"
                              "  --no-ethernet-diagnostics\n"
                              "       Report no Ethernet diagnostics at all.\n";

ArgParser::OptionSet sAppOptions = { HandleAppOption, sAppOptionDefs, "APP OPTIONS", sAppOptionHelp };
#endif

std::optional<DefaultThreadNetworkDirectoryServer> gThreadNetworkDirectoryServer;
void emberAfThreadNetworkDirectoryClusterInitCallback(EndpointId endpoint)
{
    VerifyOrDie(!gThreadNetworkDirectoryServer);
    TEMPORARY_RETURN_IGNORED gThreadNetworkDirectoryServer.emplace(endpoint).Init();
}

std::optional<WiFiNetworkManagementServer> gWiFiNetworkManagementServer;
void emberAfWiFiNetworkManagementClusterInitCallback(EndpointId endpoint)
{
    VerifyOrDie(!gWiFiNetworkManagementServer);
    TEMPORARY_RETURN_IGNORED gWiFiNetworkManagementServer.emplace(endpoint).Init();
}

#if MATTER_ENABLE_UBUS
std::optional<OpenThreadUbusBorderRouterDelegate> gBorderRouterDelegate;
#else
std::optional<FakeBorderRouterDelegate> gBorderRouterDelegate;
#endif

std::optional<ThreadBorderRouterManagement::ServerInstance> gThreadBorderRouterManagementServer;
void emberAfThreadBorderRouterManagementClusterInitCallback(EndpointId endpoint)
{
    VerifyOrDie(!gThreadBorderRouterManagementServer);
#if MATTER_ENABLE_UBUS
    gBorderRouterDelegate.emplace(gUbusManager);
#else
    gBorderRouterDelegate.emplace();
#endif
    TEMPORARY_RETURN_IGNORED gThreadBorderRouterManagementServer
        .emplace(endpoint, &*gBorderRouterDelegate, Server::GetInstance().GetFailSafeContext())
        .Init();
}

// Null AuthenticatorDriver for standalone testing (no real authenticator).
class NullAuthenticatorDriver : public NetworkIdentityManagement::AuthenticatorDriver
{
public:
    void OnStartup(NetworkIdentityManagement::AuthenticatorDriverCallback &, ReadOnlyNetworkIdentityStorage &) override {}
};

std::optional<DefaultNetworkIdentityStorage> gNetworkIdentityStorage;
Crypto::RawKeyNetworkIdentityKeystore gNetworkIdentityKeystore;
NullAuthenticatorDriver gNullAuthenticatorDriver;
LazyRegisteredServerCluster<NetworkIdentityManagementCluster> gNetworkIdentityManagementCluster;

void emberAfNetworkIdentityManagementClusterInitCallback(EndpointId endpoint)
{
    VerifyOrDie(!gNetworkIdentityManagementCluster.IsConstructed());
    gNetworkIdentityStorage.emplace(Server::GetInstance().GetPersistentStorage());
    gNetworkIdentityManagementCluster.Create(endpoint, *gNetworkIdentityStorage, gNetworkIdentityKeystore,
                                             gNullAuthenticatorDriver);
    SuccessOrDie(CodegenDataModelProvider::Instance().Registry().Register(gNetworkIdentityManagementCluster.Registration()));
}

#if MATTER_ENABLE_UBUS
std::optional<OtbrThreadNetworkDiagnosticsProvider> gThreadDiagnosticsProvider;
#endif

static void ApplicationEarlyInit()
{
#if MATTER_ENABLE_UBUS
    NimDiagnosticsProvider::Instance().SetPrimaryInterface(gPrimaryInterface);
    DeviceLayer::SetDiagnosticDataProvider(&NimDiagnosticsProvider::Instance());
    NimInstanceInfoProvider::Instance().Init();
#endif
#if MATTER_ENABLE_UBUS
    SuccessOrDie(gUbusManager.Init());

    // Must be in place before endpoint initialization constructs the
    // Thread Network Diagnostics cluster: without an in-process Thread
    // stack, the default provider would report an unprovisioned device.
    gThreadDiagnosticsProvider.emplace(gUbusManager);
    SuccessOrDie(gThreadDiagnosticsProvider->Init());
    app::Clusters::ThreadNetworkDiagnostics::SetDefaultThreadNetworkDiagnosticsProvider(&*gThreadDiagnosticsProvider);
#endif
}

#if MATTER_ENABLE_UBUS
// Records the border router's own network in the Thread Network Directory,
// so the directory answers with the network of the home it lives in even
// before any controller has populated it. A dataset change (e.g. a PAN
// migration) updates the entry; past networks deliberately stay listed.
void SeedThreadNetworkDirectory(void * context, const Thread::OperationalDataset & dataset)
{
    ByteSpan extPanId;
    VerifyOrReturn(gThreadNetworkDirectoryServer.has_value());
    VerifyOrReturn(dataset.GetExtendedPanIdAsByteSpan(extPanId) == CHIP_NO_ERROR);
    CHIP_ERROR err = gThreadNetworkDirectoryServer->Storage().AddOrUpdateNetwork(
        ThreadNetworkDirectoryStorage::ExtendedPanId(extPanId), dataset.AsByteSpan());
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "Seeding the Thread Network Directory failed: %" CHIP_ERROR_FORMAT, err.Format());
    }
}
#endif

// The directory is persisted, so networks recorded while a border router
// was installed outlive it. Without one there is nothing to share, and a
// stale entry would advertise a network this node can no longer reach.
void ClearThreadNetworkDirectory()
{
    VerifyOrReturn(gThreadNetworkDirectoryServer.has_value());
    auto & storage = gThreadNetworkDirectoryServer->Storage();

    // Removing while iterating skips entries, so collect the ids first.
    ThreadNetworkDirectoryStorage::ExtendedPanId ids[CHIP_CONFIG_MAX_THREAD_NETWORK_DIRECTORY_STORAGE_CAPACITY];
    size_t count = 0;
    {
        auto * it = storage.IterateNetworkIds();
        VerifyOrReturn(it != nullptr);
        while (count < MATTER_ARRAY_SIZE(ids) && it->Next(ids[count]))
        {
            count++;
        }
        it->Release();
    }

    for (size_t i = 0; i < count; i++)
    {
        CHIP_ERROR err = storage.RemoveNetwork(ids[i]);
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(AppServer, "Clearing the Thread Network Directory failed: %" CHIP_ERROR_FORMAT, err.Format());
        }
    }
    if (count != 0)
    {
        ChipLogProgress(AppServer, "Cleared %u Thread network(s) from the directory", static_cast<unsigned>(count));
    }
}

void ApplicationInit()
{
#if MATTER_ENABLE_UBUS
    // The cluster serves the router's real access point credentials, kept in
    // sync from netifd; procd pokes reload_wifi on wireless config changes.
    // An empty network name with no override disables sharing entirely: the
    // cluster then never holds credentials, so SSID reads null and
    // NetworkPassphraseRequest fails with InvalidInState.
    if ((gWifiNetworkName != nullptr && gWifiNetworkName[0] != '\0') || gWifiIfaceSection != nullptr)
    {
        ChipLogProgress(AppServer, "Wi-Fi credential source: network '%s', iface override '%s'", gWifiNetworkName,
                        gWifiIfaceSection != nullptr ? gWifiIfaceSection : "(none)");
        gWiFiCredentialsProvider.emplace(gUbusManager, *gWiFiNetworkManagementServer);
        SuccessOrDie(gWiFiCredentialsProvider->Init(gWifiNetworkName, gWifiIfaceSection));
        gMatterUbusService.SetReloadWifiHandler(
            [](void * context) { static_cast<WiFiCredentialsUbusProvider *>(context)->Refresh(); }, &*gWiFiCredentialsProvider);
    }
    else
    {
        ChipLogProgress(AppServer, "Wi-Fi credential sharing disabled");
    }

    if (gThreadManaged)
    {
        gBorderRouterDelegate->SetActiveDatasetObserver(SeedThreadNetworkDirectory, nullptr);
    }
    else
    {
        ClearThreadNetworkDirectory();
    }

    // Status sources for the ubus object: what the node currently shares.
    gMatterUbusService.SetWiFiCluster(&*gWiFiNetworkManagementServer);
    if (gThreadManaged && gThreadNetworkDirectoryServer.has_value())
    {
        gMatterUbusService.SetThreadDirectory(&gThreadNetworkDirectoryServer->Storage());
    }

    // Publish the "matter" ubus object once the server is up; its handlers
    // read commissioning state owned by the server.
    SuccessOrDie(gMatterUbusService.Init());
#else
    // Demo credentials for standalone testing without an OpenWrt backend.
    TEMPORARY_RETURN_IGNORED gWiFiNetworkManagementServer->SetNetworkCredentials(ByteSpanFromCharSpan("MatterAP"_span),
                                                                                 ByteSpanFromCharSpan("Setec Astronomy"_span));
#endif
}

void ApplicationShutdown()
{
#if MATTER_ENABLE_UBUS
    gUbusManager.Shutdown();
#endif
}

int main(int argc, char * argv[])
{
#if MATTER_ENABLE_UBUS
    // Must be in place before the stack caches its ethernet interface;
    // --primary-interface overrides it during argument parsing.
    setenv("CHIP_ETHERNET_INTERFACE", gPrimaryInterface, 0);
    VerifyOrReturnValue(ChipLinuxAppInit(argc, argv, &sAppOptions) == 0, -1);
#else
    VerifyOrReturnValue(ChipLinuxAppInit(argc, argv) == 0, -1);
#endif
    ApplicationEarlyInit();
    ChipLinuxAppMainLoop();
    return 0;
}
