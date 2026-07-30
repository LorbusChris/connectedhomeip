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

#include "WiFiCredentialsUbus.h"

#include <lib/core/CHIPSafeCasts.h>
#include <lib/support/CodeUtils.h>

#include <libubus.h>
#include <string.h>

using namespace chip::ubus;

namespace chip {

namespace {

constexpr int kInvokeTimeout = 2000;

// network.wireless status: { "radio0": { "up": true, "disabled": false,
//   "interfaces": [ { "section": "default_radio0", "config": { "mode": "ap",
//   "ssid": ..., "key": ..., "encryption": ..., "network": [ "lan" ] } } ] } }

enum
{
    RADIO_ATTR_UP,
    RADIO_ATTR_DISABLED,
    RADIO_ATTR_INTERFACES,
    __RADIO_ATTR_MAX,
};

const blobmsg_policy kRadioPolicy[__RADIO_ATTR_MAX] = {
    [RADIO_ATTR_UP]         = { .name = "up", .type = BLOBMSG_TYPE_BOOL },
    [RADIO_ATTR_DISABLED]   = { .name = "disabled", .type = BLOBMSG_TYPE_BOOL },
    [RADIO_ATTR_INTERFACES] = { .name = "interfaces", .type = BLOBMSG_TYPE_ARRAY },
};

enum
{
    IFACE_ATTR_SECTION,
    IFACE_ATTR_CONFIG,
    __IFACE_ATTR_MAX,
};

const blobmsg_policy kIfacePolicy[__IFACE_ATTR_MAX] = {
    [IFACE_ATTR_SECTION] = { .name = "section", .type = BLOBMSG_TYPE_STRING },
    [IFACE_ATTR_CONFIG]  = { .name = "config", .type = BLOBMSG_TYPE_TABLE },
};

enum
{
    CONFIG_ATTR_MODE,
    CONFIG_ATTR_SSID,
    CONFIG_ATTR_KEY,
    CONFIG_ATTR_ENCRYPTION,
    CONFIG_ATTR_NETWORK,
    CONFIG_ATTR_DISABLED,
    __CONFIG_ATTR_MAX,
};

const blobmsg_policy kConfigPolicy[__CONFIG_ATTR_MAX] = {
    [CONFIG_ATTR_MODE]       = { .name = "mode", .type = BLOBMSG_TYPE_STRING },
    [CONFIG_ATTR_SSID]       = { .name = "ssid", .type = BLOBMSG_TYPE_STRING },
    [CONFIG_ATTR_KEY]        = { .name = "key", .type = BLOBMSG_TYPE_STRING },
    [CONFIG_ATTR_ENCRYPTION] = { .name = "encryption", .type = BLOBMSG_TYPE_STRING },
    [CONFIG_ATTR_NETWORK]    = { .name = "network", .type = BLOBMSG_TYPE_ARRAY },
    [CONFIG_ATTR_DISABLED]   = { .name = "disabled", .type = BLOBMSG_TYPE_BOOL },
};

bool ArrayContainsString(blob_attr * array, const char * value)
{
    blob_attr * cur;
    size_t rem;
    VerifyOrReturnValue(array != nullptr, false);
    blobmsg_for_each_attr(cur, array, rem)
    {
        if (blobmsg_type(cur) == BLOBMSG_TYPE_STRING && strcmp(blobmsg_get_string(cur), value) == 0)
        {
            return true;
        }
    }
    return false;
}

// The passphrase is only meaningful for WPA-Personal style encryption
// (psk, psk2, psk-mixed, sae, sae-mixed, ...). Open networks and
// WPA-Enterprise have no shareable passphrase.
bool IsPersonalEncryption(const char * encryption)
{
    return encryption != nullptr && (strncmp(encryption, "psk", 3) == 0 || strncmp(encryption, "sae", 3) == 0);
}

} // namespace

CHIP_ERROR WiFiCredentialsUbusProvider::Init(const char * network, const char * section)
{
    mNetworkName  = network;
    mIfaceSection = section;

    mWireless.SetResolvedCallback([](UbusWatch & watch, void * appState) {
        static_cast<WiFiCredentialsUbusProvider *>(appState)->Refresh();
    });
    mWireless.SetLostCallback([](UbusWatch & watch, void * appState) {
        // netifd going away takes the credentials' source of truth with it;
        // keep the last known state rather than clearing a working AP.
    });
    mUbusManager.Register(mWireless);

    return CHIP_NO_ERROR;
}

void WiFiCredentialsUbusProvider::Refresh()
{
    VerifyOrReturn(mWireless.Resolved());
    ubus_invoke(&mUbusManager.Context(), mWireless.ObjectID(), "status", nullptr,
                ([](ubus_request * req, int type, blob_attr * msg) {
                    static_cast<WiFiCredentialsUbusProvider *>(req->priv)->OnStatus(msg);
                }),
                this, kInvokeTimeout);
}

void WiFiCredentialsUbusProvider::OnStatus(blob_attr * msg)
{
    blob_attr * section = nullptr;
    blob_attr * config  = nullptr;
    if (SelectAccessPoint(msg, section, config))
    {
        ChipLogProgress(AppServer, "Sharing Wi-Fi credentials of '%s'", blobmsg_get_string(section));
        Apply(config);
    }
    else
    {
        ChipLogProgress(AppServer, "No shareable Wi-Fi access point on network '%s'", mNetworkName);
        Apply(nullptr);
    }
}

bool WiFiCredentialsUbusProvider::SelectAccessPoint(blob_attr * radios, blob_attr *& section, blob_attr *& config)
{
    blob_attr * radio;
    size_t radioRem;
    VerifyOrReturnValue(radios != nullptr, false);

    blobmsg_for_each_attr(radio, radios, radioRem)
    {
        // Plain ifs: continue inside VerifyOrDo would bind to the macro's
        // own do-while and silently fall through instead.
        if (blobmsg_type(radio) != BLOBMSG_TYPE_TABLE)
            continue;

        blob_attr * radioAttrs[__RADIO_ATTR_MAX];
        if (blobmsg_parse_attr(kRadioPolicy, __RADIO_ATTR_MAX, radioAttrs, radio) != 0)
            continue;
        if (radioAttrs[RADIO_ATTR_INTERFACES] == nullptr)
            continue;
        if (radioAttrs[RADIO_ATTR_UP] == nullptr || !blobmsg_get_u8(radioAttrs[RADIO_ATTR_UP]))
            continue;
        if (radioAttrs[RADIO_ATTR_DISABLED] != nullptr && blobmsg_get_u8(radioAttrs[RADIO_ATTR_DISABLED]))
            continue;

        blob_attr * iface;
        size_t ifaceRem;
        blobmsg_for_each_attr(iface, radioAttrs[RADIO_ATTR_INTERFACES], ifaceRem)
        {
            if (blobmsg_type(iface) != BLOBMSG_TYPE_TABLE)
                continue;

            blob_attr * ifaceAttrs[__IFACE_ATTR_MAX];
            if (blobmsg_parse_attr(kIfacePolicy, __IFACE_ATTR_MAX, ifaceAttrs, iface) != 0)
                continue;
            if (ifaceAttrs[IFACE_ATTR_SECTION] == nullptr || ifaceAttrs[IFACE_ATTR_CONFIG] == nullptr)
                continue;

            blob_attr * configAttrs[__CONFIG_ATTR_MAX];
            if (blobmsg_parse_attr(kConfigPolicy, __CONFIG_ATTR_MAX, configAttrs, ifaceAttrs[IFACE_ATTR_CONFIG]) != 0)
                continue;

            // Only access points have credentials to share; a sta iface's key
            // belongs to somebody else's network.
            if (configAttrs[CONFIG_ATTR_MODE] == nullptr ||
                strcmp(blobmsg_get_string(configAttrs[CONFIG_ATTR_MODE]), "ap") != 0)
                continue;
            if (configAttrs[CONFIG_ATTR_DISABLED] != nullptr && blobmsg_get_u8(configAttrs[CONFIG_ATTR_DISABLED]))
                continue;

            if (mIfaceSection != nullptr)
            {
                if (strcmp(blobmsg_get_string(ifaceAttrs[IFACE_ATTR_SECTION]), mIfaceSection) != 0)
                    continue;
            }
            else if (!ArrayContainsString(configAttrs[CONFIG_ATTR_NETWORK], mNetworkName))
            {
                continue;
            }

            section = ifaceAttrs[IFACE_ATTR_SECTION];
            config  = ifaceAttrs[IFACE_ATTR_CONFIG];
            return true;
        }
    }
    return false;
}

void WiFiCredentialsUbusProvider::Apply(blob_attr * config)
{
    const char * ssid       = nullptr;
    const char * key        = nullptr;
    const char * encryption = nullptr;

    if (config != nullptr)
    {
        blob_attr * configAttrs[__CONFIG_ATTR_MAX];
        if (!blobmsg_parse_attr(kConfigPolicy, __CONFIG_ATTR_MAX, configAttrs, config))
        {
            ssid       = configAttrs[CONFIG_ATTR_SSID] ? blobmsg_get_string(configAttrs[CONFIG_ATTR_SSID]) : nullptr;
            key        = configAttrs[CONFIG_ATTR_KEY] ? blobmsg_get_string(configAttrs[CONFIG_ATTR_KEY]) : nullptr;
            encryption = configAttrs[CONFIG_ATTR_ENCRYPTION] ? blobmsg_get_string(configAttrs[CONFIG_ATTR_ENCRYPTION]) : nullptr;
        }
    }

    CHIP_ERROR err;
    if (ssid != nullptr && key != nullptr && IsPersonalEncryption(encryption))
    {
        err = mCluster.SetNetworkCredentials(ByteSpan(Uint8::from_const_char(ssid), strlen(ssid)),
                                             ByteSpan(Uint8::from_const_char(key), strlen(key)));
    }
    else
    {
        err = mCluster.ClearNetworkCredentials();
    }
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "Updating Wi-Fi credentials failed: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

} // namespace chip
