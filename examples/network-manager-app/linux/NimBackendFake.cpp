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

#include "NimBackend.h"
#include "ThreadBRFake.h"

#include <app/clusters/wifi-network-management-server/WiFiNetworkManagementCluster.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>

#include <optional>

namespace chip {

namespace {

// No operating system behind it: demo access point credentials and a border
// router delegate that keeps its datasets in memory, for standalone testing.
class FakeNimBackend final : public NimBackend
{
public:
    const char * Name() const override { return "standalone (no OS integration)"; }

    CHIP_ERROR EarlyInit() override { return CHIP_NO_ERROR; }

    app::Clusters::ThreadBorderRouterManagement::Delegate & BorderRouterDelegate() override
    {
        if (!mDelegate.has_value())
        {
            mDelegate.emplace();
        }
        return *mDelegate;
    }

    // The fake delegate never reports a dataset of its own: there is no
    // border router whose network could be recorded in the directory.
    void SetActiveDatasetObserver(ActiveDatasetObserver observer, void * context) override {}

    CHIP_ERROR StartWiFiCredentialSharing(app::Clusters::WiFiNetworkManagementCluster & cluster) override
    {
        return cluster.SetNetworkCredentials(ByteSpan::fromCharSpan("MatterAP"_span),
                                             ByteSpan::fromCharSpan("Setec Astronomy"_span));
    }

    CHIP_ERROR Start(app::Clusters::WiFiNetworkManagementCluster * wifi,
                     app::ThreadNetworkDirectoryStorage * threadDirectory) override
    {
        return CHIP_NO_ERROR;
    }

    void Shutdown() override {}

private:
    std::optional<FakeBorderRouterDelegate> mDelegate;
};

FakeNimBackend sBackend;

} // namespace

NimBackend & GetNimBackend()
{
    return sBackend;
}

} // namespace chip
