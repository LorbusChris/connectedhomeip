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

#include "ThreadDiagnosticsUbus.h"

#include "UboxUtils.h"

#include <clusters/ThreadNetworkDiagnostics/Attributes.h>
#include <clusters/ThreadNetworkDiagnostics/Structs.h>
#include <lib/support/CodeUtils.h>

#include <libubus.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace chip::ubus;
using namespace chip::app::Clusters::ThreadNetworkDiagnostics;

namespace chip {

namespace {

constexpr int kInvokeTimeout = 1000;

RoutingRoleEnum RoleFromString(const char * role)
{
    VerifyOrReturnValue(role != nullptr, RoutingRoleEnum::kUnspecified);
    if (strcmp(role, "detached") == 0)
    {
        return RoutingRoleEnum::kUnassigned;
    }
    if (strcmp(role, "child") == 0)
    {
        // The ubus API does not distinguish sleepy end devices; as a border
        // router this device is never one anyway.
        return RoutingRoleEnum::kEndDevice;
    }
    if (strcmp(role, "router") == 0)
    {
        return RoutingRoleEnum::kRouter;
    }
    if (strcmp(role, "leader") == 0)
    {
        return RoutingRoleEnum::kLeader;
    }
    return RoutingRoleEnum::kUnspecified;
}

// Numeric fields of the neighbor list arrive as (space-padded) strings.
long ParseNumber(const char * value, int base = 10)
{
    return value != nullptr ? strtol(value, nullptr, base) : 0;
}

uint64_t ParseHex64(const char * value)
{
    return value != nullptr ? strtoull(value, nullptr, 16) : 0;
}

} // namespace

CHIP_ERROR OtbrThreadNetworkDiagnosticsProvider::Init()
{
    mOtbr.SetResolvedCallback([](UbusWatch & watch, void * appState) {
        auto * self = static_cast<OtbrThreadNetworkDiagnosticsProvider *>(appState);
        ubus_invoke(&self->mUbusManager.Context(), watch.ObjectID(), "status", nullptr,
                    ([](ubus_request * req, int type, blob_attr * msg) {
                        static_cast<OtbrThreadNetworkDiagnosticsProvider *>(req->priv)->OnDataReceived(msg);
                    }),
                    self, kInvokeTimeout);
    });
    mOtbr.SetNotificationCallback(
        [](UbusWatch & watch, void * appState, ubus_request_data * req, const char * notification, blob_attr * msg) {
            static_cast<OtbrThreadNetworkDiagnosticsProvider *>(appState)->OnDataReceived(msg);
        });
    mOtbr.SetLostCallback(
        [](UbusWatch & watch, void * appState) { static_cast<OtbrThreadNetworkDiagnosticsProvider *>(appState)->OnOtbrLost(); });
    mUbusManager.Register(mOtbr);
    return CHIP_NO_ERROR;
}

void OtbrThreadNetworkDiagnosticsProvider::OnOtbrLost()
{
    // Everything this cluster reports describes the network the node is
    // attached to. Without otbr-agent there is no attachment to describe, and
    // the last snapshot only says where it used to be.
    mRole = RoutingRoleEnum::kUnspecified;
    mActiveDataset.Clear();
}

void OtbrThreadNetworkDiagnosticsProvider::OnDataReceived(blob_attr * msg)
{
    BlobMsgField<const char *, CHIP_CTST("DeviceRole")> deviceRole;
    BlobMsgField<ByteSpan, CHIP_CTST("ActiveDataset")> activeDataset;
    BlobMsgParse(msg, deviceRole, activeDataset);

    if (deviceRole.has_value())
    {
        mRole = RoleFromString(deviceRole.value());
    }

    if (activeDataset.has_value())
    {
        Thread::OperationalDatasetView dataset;
        if (dataset.Init(activeDataset.value()) == CHIP_NO_ERROR)
        {
            mActiveDataset = dataset;
        }
    }
}

CHIP_ERROR OtbrThreadNetworkDiagnosticsProvider::ReadAttribute(AttributeId attributeId, app::AttributeValueEncoder & encoder)
{
    switch (attributeId)
    {
    case Attributes::RoutingRole::Id:
        return encoder.Encode(mRole);

    case Attributes::Channel::Id:
    case Attributes::NetworkName::Id:
    case Attributes::PanId::Id:
    case Attributes::ExtendedPanId::Id:
    case Attributes::MeshLocalPrefix::Id:
    case Attributes::ActiveTimestamp::Id:
    case Attributes::OperationalDatasetComponents::Id:
        return EncodeFromDataset(attributeId, encoder);

    case Attributes::PartitionId::Id:
    case Attributes::Weighting::Id:
    case Attributes::DataVersion::Id:
    case Attributes::StableDataVersion::Id:
    case Attributes::LeaderRouterId::Id:
        return EncodeLeaderData(attributeId, encoder);

    case Attributes::Rloc16::Id:
        return EncodeRloc16(encoder);

    case Attributes::NeighborTable::Id:
        return EncodeNeighborTable(encoder);

    case Attributes::RouteTable::Id:
        return EncodeRouteTable(encoder);

    case Attributes::ActiveNetworkFaultsList::Id:
        return encoder.EncodeEmptyList();

    case Attributes::SecurityPolicy::Id:
    case Attributes::ChannelPage0Mask::Id:
        return EncodeFromDataset(attributeId, encoder);

    // Nullable attributes without a ubus source.
    case Attributes::PendingTimestamp::Id:
    case Attributes::Delay::Id:
    case Attributes::ExtAddress::Id:
        return encoder.EncodeNull();

    default:
        // The remaining attributes are counters; the ubus API tracks none.
        return encoder.Encode<uint32_t>(0u);
    }
}

CHIP_ERROR OtbrThreadNetworkDiagnosticsProvider::EncodeFromDataset(AttributeId attributeId, app::AttributeValueEncoder & encoder)
{
    VerifyOrReturnValue(!mActiveDataset.IsEmpty(), encoder.EncodeNull());

    switch (attributeId)
    {
    case Attributes::Channel::Id: {
        uint16_t channel;
        VerifyOrReturnValue(mActiveDataset.GetChannel(channel) == CHIP_NO_ERROR, encoder.EncodeNull());
        return encoder.Encode(channel);
    }
    case Attributes::NetworkName::Id: {
        char name[Thread::kSizeNetworkName + 1];
        VerifyOrReturnValue(mActiveDataset.GetNetworkName(name) == CHIP_NO_ERROR, encoder.EncodeNull());
        return encoder.Encode(CharSpan::fromCharString(name));
    }
    case Attributes::PanId::Id: {
        uint16_t panId;
        VerifyOrReturnValue(mActiveDataset.GetPanId(panId) == CHIP_NO_ERROR, encoder.EncodeNull());
        return encoder.Encode(panId);
    }
    case Attributes::ExtendedPanId::Id: {
        uint64_t extPanId;
        VerifyOrReturnValue(mActiveDataset.GetExtendedPanId(extPanId) == CHIP_NO_ERROR, encoder.EncodeNull());
        return encoder.Encode(extPanId);
    }
    case Attributes::MeshLocalPrefix::Id: {
        uint8_t prefix[Thread::kSizeMeshLocalPrefix];
        VerifyOrReturnValue(mActiveDataset.GetMeshLocalPrefix(prefix) == CHIP_NO_ERROR, encoder.EncodeNull());
        return encoder.Encode(ByteSpan(prefix));
    }
    case Attributes::ActiveTimestamp::Id: {
        uint64_t timestamp;
        VerifyOrReturnValue(mActiveDataset.GetActiveTimestamp(timestamp) == CHIP_NO_ERROR, encoder.EncodeNull());
        return encoder.Encode(timestamp);
    }
    case Attributes::SecurityPolicy::Id: {
        // The dataset holds the TLV value as one 32 bit word: the rotation
        // time in the high half, the policy flags in the low half.
        uint32_t policy;
        VerifyOrReturnValue(mActiveDataset.GetSecurityPolicy(policy) == CHIP_NO_ERROR, encoder.EncodeNull());
        Structs::SecurityPolicy::Type value;
        value.rotationTime = static_cast<uint16_t>(policy >> 16);
        value.flags        = static_cast<uint16_t>(policy & 0xFFFF);
        return encoder.Encode(value);
    }
    case Attributes::ChannelPage0Mask::Id: {
        ByteSpan mask;
        VerifyOrReturnValue(mActiveDataset.GetChannelMask(mask) == CHIP_NO_ERROR, encoder.EncodeNull());
        // The Channel Mask TLV is a sequence of entries, each a page number,
        // a length and that many mask bytes. The attribute wants the mask of
        // page 0 on its own.
        while (mask.size() >= 2)
        {
            const uint8_t page   = mask[0];
            const size_t entries = mask[1];
            VerifyOrReturnValue(mask.size() >= 2 + entries, encoder.EncodeNull());
            if (page == 0)
            {
                return encoder.Encode(mask.SubSpan(2, entries));
            }
            mask = mask.SubSpan(2 + entries);
        }
        return encoder.EncodeNull();
    }
    case Attributes::OperationalDatasetComponents::Id: {
        Structs::OperationalDatasetComponents::Type components;
        uint64_t u64;
        uint32_t u32;
        uint16_t u16;
        char name[Thread::kSizeNetworkName + 1];
        uint8_t extPanId[Thread::kSizeExtendedPanId];
        uint8_t prefix[Thread::kSizeMeshLocalPrefix];
        uint8_t key[Thread::kSizeMasterKey];
        uint8_t pskc[Thread::kSizePSKc];
        ByteSpan mask;

        components.activeTimestampPresent  = (mActiveDataset.GetActiveTimestamp(u64) == CHIP_NO_ERROR);
        components.pendingTimestampPresent = false;
        components.masterKeyPresent        = (mActiveDataset.GetMasterKey(key) == CHIP_NO_ERROR);
        components.networkNamePresent      = (mActiveDataset.GetNetworkName(name) == CHIP_NO_ERROR);
        components.extendedPanIdPresent    = (mActiveDataset.GetExtendedPanId(extPanId) == CHIP_NO_ERROR);
        components.meshLocalPrefixPresent  = (mActiveDataset.GetMeshLocalPrefix(prefix) == CHIP_NO_ERROR);
        components.delayPresent            = (mActiveDataset.GetDelayTimer(u32) == CHIP_NO_ERROR);
        components.panIdPresent            = (mActiveDataset.GetPanId(u16) == CHIP_NO_ERROR);
        components.channelPresent          = (mActiveDataset.GetChannel(u16) == CHIP_NO_ERROR);
        components.pskcPresent             = (mActiveDataset.GetPSKc(pskc) == CHIP_NO_ERROR);
        components.securityPolicyPresent   = (mActiveDataset.GetSecurityPolicy(u32) == CHIP_NO_ERROR);
        components.channelMaskPresent      = (mActiveDataset.GetChannelMask(mask) == CHIP_NO_ERROR);
        return encoder.Encode(components);
    }
    default:
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
}

CHIP_ERROR OtbrThreadNetworkDiagnosticsProvider::EncodeLeaderData(AttributeId attributeId, app::AttributeValueEncoder & encoder)
{
    struct LeaderData
    {
        BlobMsgField<uint32_t, CHIP_CTST("PartitionId")> partitionId;
        BlobMsgField<uint32_t, CHIP_CTST("Weighting")> weighting;
        BlobMsgField<uint32_t, CHIP_CTST("DataVersion")> dataVersion;
        BlobMsgField<uint32_t, CHIP_CTST("StableDataVersion")> stableDataVersion;
        BlobMsgField<uint32_t, CHIP_CTST("LeaderRouterId")> leaderRouterId;
        bool valid = false;
    } data;

    VerifyOrReturnValue(mOtbr.Resolved(), encoder.EncodeNull());
    ubus_invoke(&mUbusManager.Context(), mOtbr.ObjectID(), "leaderdata", nullptr,
                ([](ubus_request * req, int type, blob_attr * msg) {
                    auto * out = static_cast<LeaderData *>(req->priv);
                    // The reply nests the values in a "leaderdata" table.
                    blob_attr * values[1];
                    static constexpr blobmsg_policy policy[] = { { .name = "leaderdata", .type = BLOBMSG_TYPE_TABLE } };
                    VerifyOrReturn(!blobmsg_parse_attr(policy, 1, values, msg) && values[0] != nullptr);
                    out->valid = BlobMsgParse(values[0], out->partitionId, out->weighting, out->dataVersion, out->stableDataVersion,
                                              out->leaderRouterId);
                }),
                &data, kInvokeTimeout);
    VerifyOrReturnValue(data.valid, encoder.EncodeNull());

    switch (attributeId)
    {
    case Attributes::PartitionId::Id:
        return encoder.Encode(data.partitionId.value_or(0));
    case Attributes::Weighting::Id:
        return encoder.Encode(static_cast<uint16_t>(data.weighting.value_or(0)));
    case Attributes::DataVersion::Id:
        return encoder.Encode(static_cast<uint16_t>(data.dataVersion.value_or(0)));
    case Attributes::StableDataVersion::Id:
        return encoder.Encode(static_cast<uint16_t>(data.stableDataVersion.value_or(0)));
    case Attributes::LeaderRouterId::Id:
        return encoder.Encode(static_cast<uint8_t>(data.leaderRouterId.value_or(0)));
    default:
        return CHIP_ERROR_INVALID_ARGUMENT;
    }
}

CHIP_ERROR OtbrThreadNetworkDiagnosticsProvider::EncodeRloc16(app::AttributeValueEncoder & encoder)
{
    struct Rloc
    {
        long value = -1;
    } rloc;

    VerifyOrReturnValue(mOtbr.Resolved(), encoder.EncodeNull());
    ubus_invoke(&mUbusManager.Context(), mOtbr.ObjectID(), "rloc16", nullptr, ([](ubus_request * req, int type, blob_attr * msg) {
        BlobMsgField<const char *, CHIP_CTST("rloc16")> value;
        VerifyOrReturn(BlobMsgParse(msg, value) && value.has_value());
        static_cast<Rloc *>(req->priv)->value = ParseNumber(value.value(), 16);
    }),
                &rloc, kInvokeTimeout);
    VerifyOrReturnValue(rloc.value >= 0, encoder.EncodeNull());
    return encoder.Encode(static_cast<uint16_t>(rloc.value));
}

CHIP_ERROR OtbrThreadNetworkDiagnosticsProvider::EncodeRouteTable(app::AttributeValueEncoder & encoder)
{
    // Fetched up front for the same reason as the neighbor table: the list
    // encoder may run its closure more than once when chunking.
    std::vector<Structs::RouteTableStruct::Type> routers;

    VerifyOrReturnValue(mOtbr.Resolved(), encoder.EncodeEmptyList());
    ubus_invoke(&mUbusManager.Context(), mOtbr.ObjectID(), "routertable", nullptr,
                ([](ubus_request * req, int type, blob_attr * msg) {
                    auto & out = *static_cast<std::vector<Structs::RouteTableStruct::Type> *>(req->priv);
                    blob_attr * values[1];
                    static constexpr blobmsg_policy policy[] = { { .name = "router_list", .type = BLOBMSG_TYPE_ARRAY } };
                    VerifyOrReturn(!blobmsg_parse_attr(policy, 1, values, msg) && values[0] != nullptr);

                    blob_attr * cur;
                    size_t rem;
                    blobmsg_for_each_attr(cur, values[0], rem)
                    {
                        if (blobmsg_type(cur) != BLOBMSG_TYPE_TABLE)
                            continue;
                        BlobMsgField<const char *, CHIP_CTST("ExtAddress")> extAddress;
                        BlobMsgField<const char *, CHIP_CTST("Rloc16")> rloc16;
                        BlobMsgField<uint16_t, CHIP_CTST("RouterId")> routerId;
                        BlobMsgField<uint16_t, CHIP_CTST("NextHop")> nextHop;
                        BlobMsgField<uint16_t, CHIP_CTST("PathCost")> pathCost;
                        BlobMsgField<uint16_t, CHIP_CTST("LinkQualityIn")> lqiIn;
                        BlobMsgField<uint16_t, CHIP_CTST("LinkQualityOut")> lqiOut;
                        BlobMsgField<uint16_t, CHIP_CTST("Age")> age;
                        BlobMsgField<bool, CHIP_CTST("Allocated")> allocated;
                        BlobMsgField<bool, CHIP_CTST("LinkEstablished")> linkEstablished;
                        if (!BlobMsgParse(cur, extAddress, rloc16, routerId, nextHop, pathCost, lqiIn, lqiOut, age, allocated,
                                          linkEstablished))
                            continue;

                        Structs::RouteTableStruct::Type router;
                        router.extAddress      = ParseHex64(extAddress.value_or(nullptr));
                        router.rloc16          = static_cast<uint16_t>(ParseNumber(rloc16.value_or(nullptr), 16));
                        router.routerId        = static_cast<uint8_t>(routerId.value_or(0));
                        router.nextHop         = static_cast<uint8_t>(nextHop.value_or(0));
                        router.pathCost        = static_cast<uint8_t>(pathCost.value_or(0));
                        router.LQIIn           = static_cast<uint8_t>(lqiIn.value_or(0));
                        router.LQIOut          = static_cast<uint8_t>(lqiOut.value_or(0));
                        router.age             = static_cast<uint8_t>(age.value_or(0));
                        router.allocated       = allocated.value_or(false);
                        router.linkEstablished = linkEstablished.value_or(false);
                        out.push_back(router);
                    }
                }),
                &routers, kInvokeTimeout);

    return encoder.EncodeList([&routers](const auto & listEncoder) -> CHIP_ERROR {
        for (const auto & router : routers)
        {
            ReturnErrorOnFailure(listEncoder.Encode(router));
        }
        return CHIP_NO_ERROR;
    });
}

CHIP_ERROR OtbrThreadNetworkDiagnosticsProvider::EncodeNeighborTable(app::AttributeValueEncoder & encoder)
{
    // Fetched up front: the list encoder may run its closure more than once
    // when chunking, so the data must not change between passes.
    std::vector<Structs::NeighborTableStruct::Type> neighbors;

    VerifyOrReturnValue(mOtbr.Resolved(), encoder.EncodeEmptyList());
    ubus_invoke(&mUbusManager.Context(), mOtbr.ObjectID(), "neighbor", nullptr, ([](ubus_request * req, int type, blob_attr * msg) {
        auto & out = *static_cast<std::vector<Structs::NeighborTableStruct::Type> *>(req->priv);
        blob_attr * values[1];
        static constexpr blobmsg_policy policy[] = { { .name = "neighbor_list", .type = BLOBMSG_TYPE_ARRAY } };
        VerifyOrReturn(!blobmsg_parse_attr(policy, 1, values, msg) && values[0] != nullptr);

        blob_attr * cur;
        size_t rem;
        blobmsg_for_each_attr(cur, values[0], rem)
        {
            if (blobmsg_type(cur) != BLOBMSG_TYPE_TABLE)
                continue;
            BlobMsgField<const char *, CHIP_CTST("Role")> role;
            BlobMsgField<const char *, CHIP_CTST("Rloc16")> rloc16;
            BlobMsgField<const char *, CHIP_CTST("Age")> age;
            BlobMsgField<const char *, CHIP_CTST("AvgRssi")> avgRssi;
            BlobMsgField<const char *, CHIP_CTST("LastRssi")> lastRssi;
            BlobMsgField<const char *, CHIP_CTST("Mode")> mode;
            BlobMsgField<const char *, CHIP_CTST("ExtAddress")> extAddress;
            BlobMsgField<uint16_t, CHIP_CTST("LinkQualityIn")> lqi;
            BlobMsgField<uint32_t, CHIP_CTST("LinkFrameCounter")> linkFrameCounter;
            BlobMsgField<uint32_t, CHIP_CTST("MleFrameCounter")> mleFrameCounter;
            if (!BlobMsgParse(cur, role, rloc16, age, avgRssi, lastRssi, mode, extAddress, lqi, linkFrameCounter, mleFrameCounter))
                continue;

            Structs::NeighborTableStruct::Type neighbor;
            neighbor.extAddress = ParseHex64(extAddress.value_or(nullptr));
            neighbor.age        = static_cast<uint32_t>(ParseNumber(age.value_or(nullptr)));
            neighbor.rloc16     = static_cast<uint16_t>(ParseNumber(rloc16.value_or(nullptr), 16));
            neighbor.lqi        = static_cast<uint8_t>(lqi.value_or(0));
            // Zero when otbr predates these fields, which is what
            // the row said before it reported them at all.
            neighbor.linkFrameCounter = linkFrameCounter.value_or(0);
            neighbor.mleFrameCounter  = mleFrameCounter.value_or(0);
            neighbor.averageRssi.SetNonNull(static_cast<int8_t>(ParseNumber(avgRssi.value_or(nullptr))));
            neighbor.lastRssi.SetNonNull(static_cast<int8_t>(ParseNumber(lastRssi.value_or(nullptr))));
            const char * modeFlags    = mode.value_or("");
            neighbor.rxOnWhenIdle     = (strchr(modeFlags, 'r') != nullptr);
            neighbor.fullThreadDevice = (strchr(modeFlags, 'd') != nullptr);
            neighbor.fullNetworkData  = (strchr(modeFlags, 'n') != nullptr);
            neighbor.isChild          = (role.has_value() && strcmp(role.value(), "C") == 0);
            out.push_back(neighbor);
        }
    }),
                &neighbors, kInvokeTimeout);

    return encoder.EncodeList([&neighbors](const auto & listEncoder) -> CHIP_ERROR {
        for (const auto & neighbor : neighbors)
        {
            ReturnErrorOnFailure(listEncoder.Encode(neighbor));
        }
        return CHIP_NO_ERROR;
    });
}

} // namespace chip
