/*
 * SPDX-FileCopyrightText: 2018-2024 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/light/BnLights.h>

#include <mutex>
#include <vector>
#include <unordered_map>

// Events
#define ONGOING_NONE             0
#define ONGOING_BUTTONS         (1 << 0)
#define ONGOING_ATTENTION       (1 << 1)
#define ONGOING_NOTIFICATION    (1 << 2)
#define ONGOING_BATTERY         (1 << 3)

// Blink mode
#define BLINK_MODE_ON 1
#define BLINK_MODE_OFF 2
#define BLINK_MODE_BREATH_AUTO 3
#define BLINK_MODE_BREATH_ONCE 6
#define BLINK_MODE_BREATH_CUSTOM 7

using ::aidl::android::hardware::light::BnLights;
using ::aidl::android::hardware::light::FlashMode;
using ::aidl::android::hardware::light::HwLight;
using ::aidl::android::hardware::light::HwLightState;
using ::aidl::android::hardware::light::LightType;

static int g_ongoing = ONGOING_NONE;

static uint32_t attentionBrightness = 0;
static uint32_t batteryBrightness = 0;
static uint32_t buttonBrightness = 0;
static uint32_t notificationBrightness = 0;

struct led_data
{
    int blink_mode;
    int min_grade;
    int max_grade;
    int fade_time;
    int fade_on_time;
    int fade_off_time;
};
static std::unordered_map<int, led_data> mLedDatas {
    { ONGOING_NONE, {BLINK_MODE_OFF, -1, -1, -1, -1, -1} },
    { ONGOING_BUTTONS, {BLINK_MODE_OFF, -1, -1, -1, -1, -1} },
    { ONGOING_ATTENTION, {BLINK_MODE_OFF, -1, -1, -1, -1, -1} },
    { ONGOING_NOTIFICATION, {BLINK_MODE_OFF, -1, -1, -1, -1, -1} },
    { ONGOING_BATTERY, {BLINK_MODE_OFF, -1, -1, -1, -1, -1} },
};
static struct led_data current_led_param = mLedDatas[ONGOING_NONE];

typedef void (*LightStateHandler)(const HwLightState&);

struct LightBackend {
    LightType type;
    HwLightState state;
    LightStateHandler handler;

    LightBackend(LightType type, LightStateHandler handler) : type(type), handler(handler) {
        this->state.color = 0xff000000;
    }
};

namespace aidl {
namespace android {
namespace hardware {
namespace light {

class Lights : public BnLights {
  public:
    ndk::ScopedAStatus setLightState(int id, const HwLightState& state) override;
    ndk::ScopedAStatus getLights(std::vector<HwLight>* types) override;

  private:
    std::mutex globalLock;
};

}  // namespace light
}  // namespace hardware
}  // namespace android
}  // namespace aidl
