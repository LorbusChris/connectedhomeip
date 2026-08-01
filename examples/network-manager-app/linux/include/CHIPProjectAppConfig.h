/*
 *
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

#pragma once

#define CHIP_DEVICE_CONFIG_DEVICE_TYPE 144 // 0x0090 Network Infrastructure Manager
#define CHIP_DEVICE_CONFIG_DEVICE_NAME "Network Infrastructure Manager"
// Advertise the device name and type while commissionable; without
// them, commissioners fall back to a generic accessory placeholder.
#define CHIP_DEVICE_CONFIG_ENABLE_COMMISSIONABLE_DEVICE_NAME 1
#define CHIP_DEVICE_CONFIG_ENABLE_COMMISSIONABLE_DEVICE_TYPE 1
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_ID 0x8013

// Sufficient space for ArlReviewEvent of several fabrics.
#define CHIP_DEVICE_CONFIG_EVENT_LOGGING_INFO_BUFFER_SIZE (32 * 1024)

// The passcode is generated once and kept for the life of the device, and the
// system log outlives commissioning by a long way. The operator reads the code
// from the administration interface instead, which the matter ubus object
// serves from its status method.
#define CHIP_DEVICE_CONFIG_LOG_ONBOARDING_PAYLOAD 0

// config/standalone turns this on for every standalone build, on the reasoning
// that standalone means a host and not a device. This one runs on a router, so
// take the device behaviour: among other things the flag drops the range check
// in Encode() for Nullable<>, which would let an out-of-range value go out on
// the wire where the specification requires CONSTRAINT_ERROR.
#define CONFIG_BUILD_FOR_HOST_UNIT_TEST 0

// Inherit defaults from config/standalone/CHIPProjectConfig.h
#include <CHIPProjectConfig.h>
