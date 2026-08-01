/*
 *    Copyright (c) 2025 Project CHIP Authors
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
#include <app/clusters/thread-border-router-management-server/thread-br-delegate.h>

struct blob_attr;

namespace chip {

class OpenThreadUbusBorderRouterDelegate final : public app::Clusters::ThreadBorderRouterManagement::Delegate
{
public:
    using ActiveDatasetObserver = void (*)(void * context, const Thread::OperationalDataset & dataset);

    OpenThreadUbusBorderRouterDelegate(ubus::UbusManager & ubusManager) : mUbusManager(ubusManager) {}

    CHIP_ERROR Init(AttributeChangeCallback * attributeChangeCallback) override;

    // Called whenever otbr states the active dataset, both for the initial
    // snapshot and for later changes, and including the empty dataset that
    // means the node is no longer on a network. A snapshot that arrived
    // before the observer was set is replayed immediately; nothing is
    // reported before otbr has said anything at all.
    void SetActiveDatasetObserver(ActiveDatasetObserver observer, void * context)
    {
        mDatasetObserver        = observer;
        mDatasetObserverContext = context;
        if (observer != nullptr && mActiveDatasetKnown)
        {
            observer(context, mActiveDataset);
        }
    }

    void GetBorderRouterName(MutableCharSpan & borderRouterName) override;
    CHIP_ERROR GetBorderAgentId(MutableByteSpan & borderAgentId) override;
    uint16_t GetThreadVersion() override;
    bool GetInterfaceEnabled() override;
    CHIP_ERROR GetDataset(Thread::OperationalDataset & dataset, DatasetType type) override;
    void SetActiveDataset(const Thread::OperationalDataset & activeDataset, uint32_t sequenceNum,
                          ActivateDatasetCallback * callback) override;

    // otbr implements MGMT_PENDING_SET via its set_pending method, so a running
    // network can be migrated rather than only formed.
    bool GetPanChangeSupported() override { return true; }
    CHIP_ERROR CommitActiveDataset() override
    {
        mActivationPending = false;
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR RevertActiveDataset() override;
    CHIP_ERROR SetPendingDataset(const chip::Thread::OperationalDataset & pendingDataset) override;

private:
    void OnDataReceived(blob_attr * msg, bool notification);
    CHIP_ERROR SubmitDeprovision();
    void ResyncFromOtbr();
    // otbr-agent went away: nothing cached from it describes the present any
    // more, so it is dropped rather than kept being reported as current.
    void OnOtbrLost();
    // Reads the Thread version otbr was built against. Asked once, when otbr
    // appears; the answer cannot change while it is running.
    void FetchThreadVersion();

    // Invokes an otbr method that takes a hex encoded dataset argument.
    CHIP_ERROR InvokeWithDataset(const char * method, const Thread::OperationalDataset & dataset);

    AttributeChangeCallback * mAttributeChangeCallback;
    ActiveDatasetObserver mDatasetObserver = nullptr;
    void * mDatasetObserverContext         = nullptr;

    ubus::UbusManager & mUbusManager;
    ubus::UbusWatch mOtbr{ "otbr", this };

    bool mBorderAgentIDValid = false;
    uint8_t mBorderAgentID[app::Clusters::ThreadBorderRouterManagement::kBorderAgentIdLength];

    // The IEEE 802.15.4 interface is up unless otbr reports the disabled
    // role, which is what threadstop leaves behind. Without otbr there is no
    // interface to speak of.
    bool mInterfaceEnabled = false;
    // Thread version code as otbr reports it; 0 until it has been asked.
    uint16_t mThreadVersion = 0;

    // Whether otbr has stated the active dataset at all. An empty dataset is
    // a statement ("no network"); never having heard one is not.
    bool mActiveDatasetKnown = false;

    Thread::OperationalDataset mActiveDataset;
    Thread::OperationalDataset mPendingDataset;
    ActivateDatasetCallback * mActivateDatasetCallback = nullptr;
    uint32_t mActivateDatasetSequence;
    // An activation that has not been committed yet. Only such an activation
    // may be reverted: the fail-safe expiry handler reverts unconditionally,
    // including for fail-safes that never touched the dataset, and reverting
    // then would wipe a network provisioned outside Matter.
    bool mActivationPending = false;
    // A revert whose deprovision could not be delivered. The fail-safe fires
    // once, so without this the dataset would stay on the router forever if
    // otbr happened to be away at that moment; retried when it comes back.
    bool mRevertPending = false;
};

} // namespace chip
