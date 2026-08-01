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
#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>

#include "NimBackend.h"
#include "NimDiagnostics.h"
#include "NimInstanceInfo.h"

#include <cstring>
#include <optional>
#include <string>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

// Everything in this file is the same for every operating system the
// network manager runs on; what differs lives behind NimBackend.

bool gThreadManaged = true;

// The interface this node is reachable on; drives the diagnostics report
// and the identity the ethernet layers pick on this multi-homed host. Null
// until the command line or the backend says.
const char * gPrimaryInterface = nullptr;

constexpr int kOptionNoThread     = 0x1001;
constexpr int kOptionPrimaryIface = 0x1002;
constexpr int kOptionVendorName   = 0x1003;
constexpr int kOptionDiagIface    = 0x1004;
constexpr int kOptionNoEthDiag    = 0x1005;
constexpr int kOptionProductName  = 0x1006;
static_assert(kOptionProductName < NimBackend::kFirstOptionId, "option identifiers collide with the backend's");

bool HandleAppOption(const char * program, ArgParser::OptionSet * options, int identifier, const char * name, const char * value)
{
    switch (identifier)
    {
    case kOptionNoThread:
        gThreadManaged = false;
        return true;
    case kOptionPrimaryIface:
        gPrimaryInterface = value;
        return true;
    case kOptionVendorName:
        NimInstanceInfoProvider::Instance().SetVendorName(value);
        return true;
    case kOptionProductName:
        NimInstanceInfoProvider::Instance().SetProductName(value);
        return true;
    case kOptionDiagIface:
        NimDiagnosticsProvider::Instance().SetDiagnosticsInterface(value);
        return true;
    case kOptionNoEthDiag:
        NimDiagnosticsProvider::Instance().SetEthernetDiagnosticsEnabled(false);
        return true;
    default:
        return GetNimBackend().HandleOption(identifier, value);
    }
}

const ArgParser::OptionDef sCommonOptionDefs[] = {
    { "no-thread", ArgParser::kNoArgument, kOptionNoThread },
    { "primary-interface", ArgParser::kArgumentRequired, kOptionPrimaryIface },
    { "vendor-name", ArgParser::kArgumentRequired, kOptionVendorName },
    { "product-name", ArgParser::kArgumentRequired, kOptionProductName },
    { "diagnostics-interface", ArgParser::kArgumentRequired, kOptionDiagIface },
    { "no-ethernet-diagnostics", ArgParser::kNoArgument, kOptionNoEthDiag },
    {},
};

const char sCommonOptionHelp[] = "  --no-thread\n"
                                 "       No Thread border router is present: do not record or share\n"
                                 "       any Thread networks.\n"
                                 "  --primary-interface <name>\n"
                                 "       The interface this node is reachable on.\n"
                                 "  --vendor-name <name>\n"
                                 "       Manufacturer reported in Basic Information, overriding the\n"
                                 "       one the firmware states in /etc/os-release.\n"
                                 "  --product-name <name>\n"
                                 "       Product reported in Basic Information, overriding the\n"
                                 "       distribution name from /etc/os-release.\n"
                                 "  --diagnostics-interface <name>\n"
                                 "       Feed the Ethernet diagnostics from this interface instead of\n"
                                 "       the primary one.\n"
                                 "  --no-ethernet-diagnostics\n"
                                 "       Report no Ethernet diagnostics at all.\n";

// The option table handed to the argument parser: the common options followed
// by whatever the backend adds. One table, because the parser takes one.
constexpr size_t kMaxOptionDefs = 32;
ArgParser::OptionDef sOptionDefs[kMaxOptionDefs];
std::string sOptionHelp;
ArgParser::OptionSet sAppOptions = { HandleAppOption, sOptionDefs, "APP OPTIONS", nullptr };

void AssembleOptions()
{
    size_t count = 0;
    for (const ArgParser::OptionDef * def = sCommonOptionDefs; def->Name != nullptr; def++)
    {
        sOptionDefs[count++] = *def;
    }
    if (const ArgParser::OptionDef * defs = GetNimBackend().OptionDefs())
    {
        for (const ArgParser::OptionDef * def = defs; def->Name != nullptr; def++)
        {
            VerifyOrDie(count < kMaxOptionDefs - 1);
            sOptionDefs[count++] = *def;
        }
    }
    sOptionDefs[count] = {};

    sOptionHelp = sCommonOptionHelp;
    if (const char * help = GetNimBackend().OptionHelp())
    {
        sOptionHelp += help;
    }
    sAppOptions.OptionHelp = sOptionHelp.c_str();
}

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

std::optional<ThreadBorderRouterManagement::ServerInstance> gThreadBorderRouterManagementServer;

void emberAfThreadBorderRouterManagementClusterInitCallback(EndpointId endpoint)
{
    VerifyOrDie(!gThreadBorderRouterManagementServer);
    TEMPORARY_RETURN_IGNORED gThreadBorderRouterManagementServer
        .emplace(endpoint, &GetNimBackend().BorderRouterDelegate(), Server::GetInstance().GetFailSafeContext())
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

static void ApplicationEarlyInit()
{
    // The identity comes from os-release and the host name everywhere; the
    // Ethernet diagnostics need an interface to describe, so they are only
    // installed when one is known.
    NimInstanceInfoProvider::Instance().Init();
    if (gPrimaryInterface != nullptr)
    {
        NimDiagnosticsProvider::Instance().SetPrimaryInterface(gPrimaryInterface);
        NimDiagnosticsProvider::Instance().Init();
        DeviceLayer::SetDiagnosticDataProvider(&NimDiagnosticsProvider::Instance());
    }

    ChipLogProgress(AppServer, "Network manager backend: %s", GetNimBackend().Name());
    SuccessOrDie(GetNimBackend().EarlyInit());
}

// The Extended PAN ID of the entry this node put in the directory itself, as
// opposed to one a controller added. Persisted, because the entry is: a
// migration that completes while the daemon is down would otherwise leave the
// superseded network — and its network key — in the directory with nothing
// left that knows it was ours to remove.
constexpr char kSeededNetworkKey[] = "nim/seeded-ext-pan-id";

std::optional<ThreadNetworkDirectoryStorage::ExtendedPanId> LoadSeededNetwork()
{
    ThreadNetworkDirectoryStorage::ExtendedPanId id;
    uint16_t size = sizeof(id.bytes);
    VerifyOrReturnValue(Server::GetInstance().GetPersistentStorage().SyncGetKeyValue(kSeededNetworkKey, id.bytes, size) ==
                                CHIP_NO_ERROR &&
                            size == sizeof(id.bytes),
                        std::nullopt);
    return id;
}

void StoreSeededNetwork(const std::optional<ThreadNetworkDirectoryStorage::ExtendedPanId> & id)
{
    auto & storage = Server::GetInstance().GetPersistentStorage();
    CHIP_ERROR err = id.has_value()
        ? storage.SyncSetKeyValue(kSeededNetworkKey, id->bytes, static_cast<uint16_t>(sizeof(id->bytes)))
        : storage.SyncDeleteKeyValue(kSeededNetworkKey);
    if (err != CHIP_NO_ERROR && err != CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND)
    {
        ChipLogError(AppServer, "Recording the seeded Thread network failed: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

// Records the border router's own network in the Thread Network Directory, so
// the directory answers with the network of the home it lives in even before
// any controller has populated it.
//
// An empty dataset means there is no network any more — deprovisioned, or the
// border router gone — and a network the node has left is one it can no longer
// let anyone join. The entry goes, and with it the credentials it carries. The
// same applies to the network we were on before a migration to a different
// Extended PAN ID: it is retracted rather than left listed. Only the entry this
// node seeded is ever retracted; what a controller added is left alone.
void SeedThreadNetworkDirectory(void * context, const Thread::OperationalDataset & dataset)
{
    VerifyOrReturn(gThreadNetworkDirectoryServer.has_value());

    std::optional<ThreadNetworkDirectoryStorage::ExtendedPanId> seeded = LoadSeededNetwork();

    ByteSpan extPanId;
    if (dataset.IsEmpty() || dataset.GetExtendedPanIdAsByteSpan(extPanId) != CHIP_NO_ERROR)
    {
        if (seeded.has_value())
        {
            CHIP_ERROR err = gThreadNetworkDirectoryServer->ForgetNetwork(*seeded);
            if (err != CHIP_NO_ERROR && err != CHIP_ERROR_NOT_FOUND)
            {
                ChipLogError(AppServer, "Retracting the Thread network failed: %" CHIP_ERROR_FORMAT, err.Format());
                return;
            }
            StoreSeededNetwork(std::nullopt);
        }
        return;
    }

    const ThreadNetworkDirectoryStorage::ExtendedPanId current(extPanId);

    // Retract before recording, not after: the table has a fixed capacity, and
    // adding first would be refused on a full one, leaving the superseded entry
    // behind for good.
    bool preferSuccessor = false;
    if (seeded.has_value() && !(*seeded == current))
    {
        // A preference naming the network being retracted is cleared by
        // ForgetNetwork, since a preference must always name a listed network.
        // Note it now so it can follow to the replacement once that is listed.
        std::optional<ThreadNetworkDirectoryStorage::ExtendedPanId> preferred;
        preferSuccessor = gThreadNetworkDirectoryServer->GetPreferredNetwork(preferred) == CHIP_NO_ERROR && preferred.has_value() &&
            preferred.value() == *seeded;

        CHIP_ERROR err = gThreadNetworkDirectoryServer->ForgetNetwork(*seeded);
        if (err != CHIP_NO_ERROR && err != CHIP_ERROR_NOT_FOUND)
        {
            ChipLogError(AppServer, "Retracting the superseded Thread network failed: %" CHIP_ERROR_FORMAT, err.Format());
            preferSuccessor = false;
        }
    }

    // The border router states the dataset again on every reconnect, byte for
    // byte the same. Recording it again would wake every subscriber to
    // ThreadNetworks for no change, so only an actual difference is written.
    uint8_t stored[Thread::kSizeOperationalDataset];
    MutableByteSpan storedSpan(stored);
    if (gThreadNetworkDirectoryServer->Storage().GetNetworkDataset(current, storedSpan) == CHIP_NO_ERROR &&
        storedSpan.data_equal(dataset.AsByteSpan()))
    {
        return;
    }

    CHIP_ERROR err = gThreadNetworkDirectoryServer->AddOrUpdateNetwork(current, dataset.AsByteSpan());
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "Seeding the Thread Network Directory failed: %" CHIP_ERROR_FORMAT, err.Format());
        return;
    }
    if (!seeded.has_value() || !(*seeded == current))
    {
        StoreSeededNetwork(current);
    }

    // Now that the replacement is listed, the preference can point at it.
    if (preferSuccessor)
    {
        CHIP_ERROR preferErr = gThreadNetworkDirectoryServer->SetPreferredNetwork(&current);
        if (preferErr != CHIP_NO_ERROR)
        {
            ChipLogError(AppServer, "Moving the preferred Thread network failed: %" CHIP_ERROR_FORMAT, preferErr.Format());
        }
    }
}

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
        // Through the cluster, so subscribers are told and a preference that
        // named one of these is cleared rather than left dangling.
        CHIP_ERROR err = gThreadNetworkDirectoryServer->ForgetNetwork(ids[i]);
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(AppServer, "Clearing the Thread Network Directory failed: %" CHIP_ERROR_FORMAT, err.Format());
        }
    }
    if (count != 0)
    {
        ChipLogProgress(AppServer, "Cleared %u Thread network(s) from the directory", static_cast<unsigned>(count));
    }
    StoreSeededNetwork(std::nullopt);
}

void ApplicationInit()
{
    // Without credentials the cluster's SSID reads null and
    // NetworkPassphraseRequest fails with InvalidInState, which is the right
    // answer for a node that shares nothing.
    CHIP_ERROR err = GetNimBackend().StartWiFiCredentialSharing(*gWiFiNetworkManagementServer);
    if (err == CHIP_ERROR_NOT_IMPLEMENTED)
    {
        ChipLogProgress(AppServer, "Wi-Fi credential sharing disabled");
    }
    else
    {
        SuccessOrDie(err);
    }

    if (gThreadManaged)
    {
        GetNimBackend().SetActiveDatasetObserver(SeedThreadNetworkDirectory, nullptr);
    }
    else
    {
        ClearThreadNetworkDirectory();
    }

    SuccessOrDie(GetNimBackend().Start(
        &*gWiFiNetworkManagementServer,
        gThreadManaged && gThreadNetworkDirectoryServer.has_value() ? &gThreadNetworkDirectoryServer->Storage() : nullptr));
}

void ApplicationShutdown()
{
    GetNimBackend().Shutdown();
}

int main(int argc, char * argv[])
{
    AssembleOptions();
    gPrimaryInterface = GetNimBackend().DefaultPrimaryInterface();

    // The parser runs inside ChipLinuxAppInit, after which the stack has
    // already cached its ethernet interface, so the command line is scanned
    // for the primary interface ahead of it. Overwritten rather than
    // defaulted: the diagnostics this node reports are taken from
    // gPrimaryInterface, and an interface inherited from the environment
    // would make the platform describe a different one.
    for (int i = 1; i + 1 < argc; i++)
    {
        if (strcmp(argv[i], "--primary-interface") == 0)
        {
            gPrimaryInterface = argv[i + 1];
        }
    }
    if (gPrimaryInterface != nullptr)
    {
        setenv("CHIP_ETHERNET_INTERFACE", gPrimaryInterface, 1);
    }

    VerifyOrReturnValue(ChipLinuxAppInit(argc, argv, &sAppOptions) == 0, -1);
    ApplicationEarlyInit();
    ChipLinuxAppMainLoop();
    return 0;
}
