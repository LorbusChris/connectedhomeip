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
#include <app/clusters/thread-network-diagnostics-server/ThreadNetworkDiagnosticsProvider.h>
#include <clusters/ThreadNetworkDiagnostics/Enums.h>
#include <lib/support/ThreadOperationalDataset.h>

struct blob_attr;

namespace chip {

// Serves the Thread Network Diagnostics cluster from otbr-agent's ubus API:
// this application runs no in-process Thread stack, so the direct provider
// would report an unprovisioned device. Dataset-derived attributes and the
// routing role are cached from otbr's status snapshot and notifications;
// volatile values (leader data, RLOC16, the neighbor table) are fetched at
// read time. Attributes without a ubus source encode null or empty.
class OtbrThreadNetworkDiagnosticsProvider final : public app::Clusters::ThreadNetworkDiagnostics::ThreadNetworkDiagnosticsProvider
{
public:
    OtbrThreadNetworkDiagnosticsProvider(ubus::UbusManager & ubusManager) : mUbusManager(ubusManager) {}

    CHIP_ERROR Init();

    CHIP_ERROR ReadAttribute(AttributeId attributeId, app::AttributeValueEncoder & encoder) override;
    // The ubus API tracks no diagnostic counters, so there is nothing to reset.
    void ResetCounts() override {}

private:
    void OnDataReceived(blob_attr * msg);
    // otbr-agent went away: what was cached describes a network this node is
    // no longer known to be on, so it is dropped rather than kept reporting.
    void OnOtbrLost();

    CHIP_ERROR EncodeFromDataset(AttributeId attributeId, app::AttributeValueEncoder & encoder);
    CHIP_ERROR EncodeLeaderData(AttributeId attributeId, app::AttributeValueEncoder & encoder);
    CHIP_ERROR EncodeRloc16(app::AttributeValueEncoder & encoder);
    CHIP_ERROR EncodeNeighborTable(app::AttributeValueEncoder & encoder);
    CHIP_ERROR EncodeRouteTable(app::AttributeValueEncoder & encoder);

    ubus::UbusManager & mUbusManager;
    ubus::UbusWatch mOtbr{ "otbr", this };

    app::Clusters::ThreadNetworkDiagnostics::RoutingRoleEnum mRole =
        app::Clusters::ThreadNetworkDiagnostics::RoutingRoleEnum::kUnspecified;
    Thread::OperationalDataset mActiveDataset;
};

} // namespace chip
