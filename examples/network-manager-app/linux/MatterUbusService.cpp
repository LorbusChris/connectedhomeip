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

#include "MatterUbusService.h"

#include "UboxUtils.h"

#include <app/clusters/thread-network-directory-server/ThreadNetworkDirectoryStorage.h>
#include <app/clusters/wifi-network-management-server/WiFiNetworkManagementCluster.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>
#include <app/util/endpoint-config-api.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ThreadOperationalDataset.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include <inttypes.h>

extern "C" {
#include <libubus.h>
#undef fallthrough
}

using namespace chip::app::Clusters::AdministratorCommissioning;

namespace chip {

namespace {

// The spec caps a commissioning window at 15 minutes; default to the maximum,
// since the code is meant to be typed into a controller by a person.
constexpr uint32_t kDefaultWindowSeconds = 900;
constexpr uint32_t kMinWindowSeconds     = 180;

const char * WindowStatusString()
{
    // Not CommissioningWindowStatusForCluster(): that deliberately reports
    // locally opened windows as closed, because the cluster attribute only
    // covers windows opened through it. Here the local ones are the point.
    auto & mgr = Server::GetInstance().GetCommissioningWindowManager();
    if (!mgr.IsCommissioningWindowOpen())
    {
        return "closed";
    }
    switch (mgr.CommissioningWindowStatusForCluster())
    {
    case CommissioningWindowStatusEnum::kEnhancedWindowOpen:
        return "enhanced";
    case CommissioningWindowStatusEnum::kBasicWindowOpen:
    default:
        // A window opened locally is always a basic window.
        return "basic";
    }
}

void AddOnboarding(ubus::BlobMsgBuf & buf)
{
    PayloadContents payload;
    if (GetPayloadContents(payload, RendezvousInformationFlag::kOnNetwork) == CHIP_NO_ERROR)
    {
        char code[32] = {};
        char qr[128]  = {};
        MutableCharSpan codeSpan(code), qrSpan(qr);
        if (GetManualPairingCode(codeSpan, payload) == CHIP_NO_ERROR)
        {
            buf.Add("ManualCode", static_cast<const char *>(code));
        }
        if (GetQRCode(qrSpan, payload) == CHIP_NO_ERROR)
        {
            buf.Add("QrCode", static_cast<const char *>(qr));
        }
        buf.Add("VendorId", static_cast<uint32_t>(payload.vendorID));
        buf.Add("ProductId", static_cast<uint32_t>(payload.productID));
        buf.Add("Discriminator", static_cast<uint32_t>(payload.discriminator.GetLongValue()));
    }
}

app::Clusters::WiFiNetworkManagementCluster * sWifiCluster  = nullptr;
app::ThreadNetworkDirectoryStorage * sDirectory             = nullptr;

// The clusters this node actually implements, per endpoint, read from the
// data model rather than listed here, so the report cannot drift from what
// a controller sees.
void AddClusters(ubus::BlobMsgBuf & buf)
{
    auto cookie = buf.AddArray("Endpoints");
    for (uint16_t index = 0; index < emberAfEndpointCount(); index++)
    {
        VerifyOrDo(emberAfEndpointIndexIsEnabled(index), continue);
        EndpointId endpoint = emberAfEndpointFromIndex(index);

        ClusterId clusters[64];
        uint8_t count = emberAfGetClustersFromEndpoint(endpoint, clusters, MATTER_ARRAY_SIZE(clusters), /* server = */ true);

        auto entry = buf.AddTable(nullptr);
        buf.Add("Endpoint", static_cast<uint32_t>(endpoint));
        auto list = buf.AddArray("Clusters");
        for (uint8_t i = 0; i < count; i++)
        {
            buf.AddFormat(nullptr, "0x%04x", static_cast<unsigned>(clusters[i]));
        }
    }
}

// The controllers this node is paired with. The node id is the identity
// this node was given on that fabric, i.e. how the controller addresses it.
void AddFabrics(ubus::BlobMsgBuf & buf)
{
    auto cookie = buf.AddArray("FabricList");
    for (const auto & fabric : Server::GetInstance().GetFabricTable())
    {
        auto entry = buf.AddTable(nullptr);
        buf.Add("Index", static_cast<uint32_t>(fabric.GetFabricIndex()));
        buf.Add("VendorId", static_cast<uint32_t>(fabric.GetVendorId()));
        buf.AddFormat("FabricId", "%016" PRIx64, fabric.GetFabricId());
        buf.AddFormat("NodeId", "%016" PRIx64, fabric.GetNodeId());
        CharSpan label = fabric.GetFabricLabel();
        if (!label.empty())
        {
            buf.AddFormat("Label", "%.*s", static_cast<int>(label.size()), label.data());
        }
    }
}

// What the node currently shares with controllers: the Wi-Fi credentials
// state (without the passphrase) and the Thread networks in the directory.
void AddSharingState(ubus::BlobMsgBuf & buf)
{
    if (sWifiCluster != nullptr)
    {
        const bool sharing = sWifiCluster->HasNetworkCredentials();
        buf.Add("WifiShare", sharing);
        if (sharing)
        {
            ByteSpan ssid = sWifiCluster->Ssid();
            buf.AddFormat("WifiSsid", "%.*s", static_cast<int>(ssid.size()), reinterpret_cast<const char *>(ssid.data()));
        }
    }

    // Reported either way, so a user interface can tell "this node does not
    // manage Thread" from "it does, and the directory happens to be empty".
    buf.Add("ThreadManaged", sDirectory != nullptr);

    if (sDirectory != nullptr)
    {
        auto cookie = buf.AddArray("Directory");
        auto * it   = sDirectory->IterateNetworkIds();
        VerifyOrReturn(it != nullptr);
        app::ThreadNetworkDirectoryStorage::ExtendedPanId exPanId;
        while (it->Next(exPanId))
        {
            uint8_t datasetBuffer[app::ThreadNetworkDirectoryStorage::kMaxThreadDatasetLen];
            MutableByteSpan dataset(datasetBuffer);
            auto entry = buf.AddTable(nullptr);
            buf.AddFormat("ExtendedPanId", "%016" PRIx64, exPanId.AsNumber());
            Thread::OperationalDatasetView view;
            if (sDirectory->GetNetworkDataset(exPanId, dataset) == CHIP_NO_ERROR && view.Init(dataset) == CHIP_NO_ERROR)
            {
                char name[Thread::kSizeNetworkName + 1];
                if (view.GetNetworkName(name) == CHIP_NO_ERROR)
                {
                    buf.Add("NetworkName", static_cast<const char *>(name));
                }
            }
        }
        it->Release();
    }
}

int HandleStatus(ubus_context * ctx, ubus_object * obj, ubus_request_data * req, const char * method, blob_attr * msg)
{
    ubus::BlobMsgBuf buf;
    buf.Add("Fabrics", static_cast<uint32_t>(Server::GetInstance().GetFabricTable().FabricCount()));
    buf.Add("Window", WindowStatusString());
    // The initial onboarding code authenticates commissioning only while no
    // fabric is on the device (the initial basic window) or while a basic
    // window is open; report it so a UI can decide what to show.
    AddOnboarding(buf);
    AddFabrics(buf);
    AddSharingState(buf);
    AddClusters(buf);
    ubus_send_reply(ctx, req, buf.head);
    return 0;
}

enum
{
    OPEN_WINDOW_TIMEOUT,
    __OPEN_WINDOW_MAX,
};

const blobmsg_policy kOpenWindowPolicy[__OPEN_WINDOW_MAX] = {
    [OPEN_WINDOW_TIMEOUT] = { .name = "timeout", .type = BLOBMSG_TYPE_INT32 },
};

int HandleOpenWindow(ubus_context * ctx, ubus_object * obj, ubus_request_data * req, const char * method, blob_attr * msg)
{
    // A caller can omit the argument table entirely; msg is NULL then.
    blob_attr * tb[__OPEN_WINDOW_MAX] = {};
    if (msg != nullptr)
    {
        blobmsg_parse(kOpenWindowPolicy, __OPEN_WINDOW_MAX, tb, blobmsg_data(msg), blobmsg_len(msg));
    }

    uint32_t timeout = kDefaultWindowSeconds;
    if (tb[OPEN_WINDOW_TIMEOUT] != nullptr)
    {
        timeout = blobmsg_get_u32(tb[OPEN_WINDOW_TIMEOUT]);
        VerifyOrReturnValue(timeout >= kMinWindowSeconds && timeout <= kDefaultWindowSeconds, UBUS_STATUS_INVALID_ARGUMENT);
    }

    // Opens a basic window: the device's own onboarding code becomes valid
    // for the duration, which is what lets a router UI show a code that a
    // controller can actually use after the device is already commissioned.
    auto & mgr     = Server::GetInstance().GetCommissioningWindowManager();
    CHIP_ERROR err = mgr.OpenBasicCommissioningWindow(System::Clock::Seconds32(timeout));

    ubus::BlobMsgBuf buf;
    buf.Add("Error", static_cast<uint32_t>(err == CHIP_NO_ERROR ? 0 : 1));
    buf.Add("Window", WindowStatusString());
    ubus_send_reply(ctx, req, buf.head);
    return 0;
}

int HandleCloseWindow(ubus_context * ctx, ubus_object * obj, ubus_request_data * req, const char * method, blob_attr * msg)
{
    Server::GetInstance().GetCommissioningWindowManager().CloseCommissioningWindow();

    ubus::BlobMsgBuf buf;
    buf.Add("Error", static_cast<uint32_t>(0));
    buf.Add("Window", WindowStatusString());
    ubus_send_reply(ctx, req, buf.head);
    return 0;
}

MatterUbusService::ReloadWifiHandler sReloadWifiHandler = nullptr;
void * sReloadWifiContext                               = nullptr;
int HandleReloadWifi(ubus_context * ctx, ubus_object * obj, ubus_request_data * req, const char * method, blob_attr * msg)
{
    if (sReloadWifiHandler != nullptr)
    {
        sReloadWifiHandler(sReloadWifiContext);
    }

    ubus::BlobMsgBuf buf;
    buf.Add("Error", static_cast<uint32_t>(0));
    ubus_send_reply(ctx, req, buf.head);
    return 0;
}

enum
{
    REMOVE_FABRIC_INDEX,
    __REMOVE_FABRIC_MAX,
};

const blobmsg_policy kRemoveFabricPolicy[__REMOVE_FABRIC_MAX] = {
    [REMOVE_FABRIC_INDEX] = { .name = "index", .type = BLOBMSG_TYPE_INT32 },
};

// Unpairs a controller. The fabric table's delegates take care of the
// associated sessions, ACL entries and group keys, which is what the
// RemoveFabric command of the Operational Credentials cluster does too.
int HandleRemoveFabric(ubus_context * ctx, ubus_object * obj, ubus_request_data * req, const char * method, blob_attr * msg)
{
    blob_attr * tb[__REMOVE_FABRIC_MAX];
    blobmsg_parse(kRemoveFabricPolicy, __REMOVE_FABRIC_MAX, tb, blobmsg_data(msg), blobmsg_len(msg));
    VerifyOrReturnValue(tb[REMOVE_FABRIC_INDEX] != nullptr, UBUS_STATUS_INVALID_ARGUMENT);

    uint32_t index = blobmsg_get_u32(tb[REMOVE_FABRIC_INDEX]);
    VerifyOrReturnValue(index <= UINT8_MAX && IsValidFabricIndex(static_cast<FabricIndex>(index)),
                        UBUS_STATUS_INVALID_ARGUMENT);

    CHIP_ERROR err = Server::GetInstance().GetFabricTable().Delete(static_cast<FabricIndex>(index));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "Removing fabric %u failed: %" CHIP_ERROR_FORMAT, index, err.Format());
    }

    ubus::BlobMsgBuf buf;
    buf.Add("Error", static_cast<uint32_t>(err == CHIP_NO_ERROR ? 0 : 1));
    buf.Add("Fabrics", static_cast<uint32_t>(Server::GetInstance().GetFabricTable().FabricCount()));
    ubus_send_reply(ctx, req, buf.head);
    return 0;
}

ubus_method sMethods[] = {
    UBUS_METHOD_NOARG("status", HandleStatus),
    UBUS_METHOD("open_commissioning_window", HandleOpenWindow, kOpenWindowPolicy),
    UBUS_METHOD_NOARG("close_commissioning_window", HandleCloseWindow),
    UBUS_METHOD_NOARG("reload_wifi", HandleReloadWifi),
    UBUS_METHOD("remove_fabric", HandleRemoveFabric, kRemoveFabricPolicy),
};

ubus_object_type sObjectType = UBUS_OBJECT_TYPE("matter", sMethods);

ubus_object sObject = {
    .name      = "matter",
    .type      = &sObjectType,
    .methods   = sMethods,
    .n_methods = MATTER_ARRAY_SIZE(sMethods),
};

} // namespace

CHIP_ERROR MatterUbusService::Init()
{
    return mUbusManager.Host(sObject);
}

void MatterUbusService::SetReloadWifiHandler(ReloadWifiHandler handler, void * context)
{
    sReloadWifiHandler = handler;
    sReloadWifiContext = context;
}

void MatterUbusService::SetWiFiCluster(app::Clusters::WiFiNetworkManagementCluster * cluster)
{
    sWifiCluster = cluster;
}

void MatterUbusService::SetThreadDirectory(app::ThreadNetworkDirectoryStorage * storage)
{
    sDirectory = storage;
}

} // namespace chip
