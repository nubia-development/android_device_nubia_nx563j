#define LOG_TAG "LightService"

#include "Lights.h"

#include <android-base/logging.h>

#include <fstream>

// Display
#define LCD_FILE "/sys/class/leds/lcd-backlight/brightness"

// Nubia LED
#define LED_BRIGHTNESS "/sys/class/leds/nubia_led/brightness"
#define LED_BLINK_MODE "/sys/class/leds/nubia_led/blink_mode"
#define LED_CHANNEL "/sys/class/leds/nubia_led/outn"
#define LED_GRADE "/sys/class/leds/nubia_led/grade_parameter"
#define LED_FADE "/sys/class/leds/nubia_led/fade_parameter"

// Outn channels
#define LED_CHANNEL_HOME 16
#define LED_CHANNEL_BUTTON 8

// Grade values
#define LED_GRADE_HOME_BATTERY 0
#define LED_GRADE_HOME_NOTIFICATION 6
#define LED_GRADE_HOME_ATTENTION 6

#define MAX_LED_BRIGHTNESS 255
#define MAX_LCD_BRIGHTNESS 255

namespace {

/*
 * Write value to path and close file.
 */
static void set(std::string path, std::string value) {
    std::ofstream file(path);

    if (!file.is_open()) {
        LOG(WARNING) << "failed to write " << value.c_str() << " to " << path.c_str();
        return;
    }

    file << value;
}

static void set(std::string path, int value) {
    set(path, std::to_string(value));
}

static void set(std::string path, char *buffer) {
    std::string str(buffer);
    set(path, str);
}

static inline bool isLit(const HwLightState& state) {
    return state.color & 0x00ffffff;
}

static uint32_t getBrightness(const HwLightState& state) {
    uint32_t alpha, red, green, blue;

    /*
     * Extract brightness from AARRGGBB.
     */
    alpha = (state.color >> 24) & 0xFF;
    red = (state.color >> 16) & 0xFF;
    green = (state.color >> 8) & 0xFF;
    blue = state.color & 0xFF;

    /*
     * Scale RGB brightness if Alpha brightness is not 0xFF.
     */
    if (alpha != 0xFF && alpha != 0) {
        red = red * alpha / 0xFF;
        green = green * alpha / 0xFF;
        blue = blue * alpha / 0xFF;
    }

    return (77 * red + 150 * green + 29 * blue) >> 8;
}

static inline uint32_t scaleBrightness(uint32_t brightness, uint32_t maxBrightness) {
    return brightness * maxBrightness / 0xFF;
}

static inline uint32_t getScaledBrightness(const HwLightState& state, uint32_t maxBrightness) {
    return scaleBrightness(getBrightness(state), maxBrightness);
}

static void handleBacklight(const HwLightState& state) {
    uint32_t brightness = getScaledBrightness(state, MAX_LCD_BRIGHTNESS);
    set(LCD_FILE, brightness);
}

static int compareLedStatus(struct led_data *led1, struct led_data *led2)
{
    if (led1 == NULL || led2 == NULL) return false;
    if (led1->blink_mode != led2->blink_mode) return false;
    if (led1->min_grade != led2->min_grade) return false;
    if (led1->max_grade != led2->max_grade) return false;
    if (led1->fade_time != led2->fade_time) return false;
    if (led1->fade_on_time != led2->fade_on_time) return false;
    if (led1->fade_off_time != led2->fade_off_time) return false;
    return true;
}

static void handleNubiaLed(const HwLightState& state, int source)
{
    uint32_t brightness = getBrightness(state);
    bool enable = brightness > 0;
    struct led_data led_param = {BLINK_MODE_OFF, 0, 100, 3, 0, 4};
    int blink = 0;

    char grade[16];
    char fade[16];

    switch (state.flashMode) {
        case FlashMode::HARDWARE:
            led_param.blink_mode = enable ? BLINK_MODE_BREATH_AUTO : BLINK_MODE_OFF;
            led_param.fade_time = enable ? led_param.fade_time : -1;
            led_param.fade_on_time = enable ? led_param.fade_on_time : -1;
            led_param.fade_off_time = enable ? led_param.fade_off_time : -1;
            goto applyChanges;
        case FlashMode::TIMED:
            blink = state.flashOnMs > 0 && state.flashOffMs > 0;
            led_param.fade_time = 1;
            led_param.fade_on_time = state.flashOnMs / 400;
            led_param.fade_off_time = state.flashOffMs / 400;
            break;
        case FlashMode::NONE:
        default:
            break;
    }

    LOG(DEBUG) << "setting led " << source << ": "
        << state.color << ", " << state.flashOnMs << ", " << state.flashOffMs;

    if (enable) {
        g_ongoing |= source;
        LOG(DEBUG) << "ongoing +: " << source << " = " << g_ongoing;

        switch (source) {
            case ONGOING_BUTTONS:
                led_param.blink_mode = BLINK_MODE_ON;
                led_param.min_grade = buttonBrightness;
                led_param.max_grade = buttonBrightness;
                break;
            case ONGOING_ATTENTION:
                led_param.blink_mode = blink ? BLINK_MODE_BREATH_CUSTOM : BLINK_MODE_ON;
                led_param.min_grade = blink ? LED_GRADE_HOME_ATTENTION : attentionBrightness;
                led_param.max_grade = (LED_GRADE_HOME_ATTENTION > attentionBrightness)
                                        ? LED_GRADE_HOME_ATTENTION : attentionBrightness;
                break;
            case ONGOING_NOTIFICATION:
                led_param.blink_mode = blink ? BLINK_MODE_BREATH_CUSTOM : BLINK_MODE_ON;
                led_param.min_grade = blink ? LED_GRADE_HOME_NOTIFICATION : notificationBrightness;
                led_param.max_grade = (LED_GRADE_HOME_NOTIFICATION > notificationBrightness)
                                        ? LED_GRADE_HOME_NOTIFICATION : notificationBrightness;
                break;
            case ONGOING_BATTERY:
                led_param.blink_mode = blink ? BLINK_MODE_BREATH_CUSTOM : BLINK_MODE_ON;
                led_param.min_grade = blink ? LED_GRADE_HOME_BATTERY : batteryBrightness;
                led_param.max_grade = (LED_GRADE_HOME_BATTERY > batteryBrightness)
                                        ? LED_GRADE_HOME_BATTERY : batteryBrightness;
                break;
            default:
                led_param.blink_mode = BLINK_MODE_OFF;
                led_param.fade_time = -1;
                led_param.fade_on_time = -1;
                led_param.fade_off_time = -1;
                break;
        }
        // Store led_param for current source
        mLedDatas[source] = led_param;
    } else {
        g_ongoing &= ~source;
        LOG(DEBUG) << "ongoing -: " << source << " = " << g_ongoing;

        // try to restore previous mode
        if (g_ongoing & ONGOING_BUTTONS) {
            led_param = mLedDatas[ONGOING_BUTTONS];
        } else if (g_ongoing & ONGOING_ATTENTION) {
            led_param = mLedDatas[ONGOING_ATTENTION];
        } else if (g_ongoing & ONGOING_NOTIFICATION) {
            led_param = mLedDatas[ONGOING_NOTIFICATION];
        } else if (g_ongoing & ONGOING_BATTERY) {
            led_param = mLedDatas[ONGOING_BATTERY];
        } else {
            led_param.blink_mode = BLINK_MODE_OFF;
            led_param.fade_time = -1;
            led_param.fade_on_time = -1;
            led_param.fade_off_time = -1;
        }
    }

applyChanges:
    if (g_ongoing & (source - 1))
        return;

    if (compareLedStatus(&led_param, &current_led_param))
        return;

    sprintf(grade, "%d %d\n", led_param.min_grade, led_param.max_grade);
    sprintf(fade, "%d %d %d\n", led_param.fade_time, led_param.fade_on_time, led_param.fade_off_time);

    set(LED_CHANNEL, LED_CHANNEL_HOME);
    set(LED_GRADE, grade);
    set(LED_FADE, fade);
    set(LED_BLINK_MODE, led_param.blink_mode);
    current_led_param = led_param;
}

static void handleButtons(const HwLightState& state) {
    buttonBrightness = getScaledBrightness(state, MAX_LED_BRIGHTNESS);

    set(LED_CHANNEL, LED_CHANNEL_BUTTON);
    if (buttonBrightness > 0) {
        set(LED_GRADE, buttonBrightness);
        set(LED_BLINK_MODE, BLINK_MODE_ON);
    } else {
        set(LED_GRADE, 0);
        set(LED_BLINK_MODE, BLINK_MODE_OFF);
    }

    handleNubiaLed(state, ONGOING_BUTTONS);
}

static void handleNotification(const HwLightState& state) {
    notificationBrightness = getScaledBrightness(state, MAX_LED_BRIGHTNESS);
    handleNubiaLed(state, ONGOING_NOTIFICATION);
}

static void handleBattery(const HwLightState& state){
    batteryBrightness = getScaledBrightness(state, MAX_LED_BRIGHTNESS);
    handleNubiaLed(state, ONGOING_BATTERY);
}

static void handleAttention(const HwLightState& state){
    attentionBrightness = getScaledBrightness(state, MAX_LED_BRIGHTNESS);
    handleNubiaLed(state, ONGOING_ATTENTION);
}
/* Keep sorted in the order of importance. */
static std::vector<LightBackend> backends = {
    { LightType::ATTENTION, handleAttention },
    { LightType::NOTIFICATIONS, handleNotification },
    { LightType::BATTERY, handleBattery },
    { LightType::BACKLIGHT, handleBacklight },
    { LightType::BUTTONS, handleButtons },
};

}  // anonymous namespace

namespace aidl {
namespace android {
namespace hardware {
namespace light {

ndk::ScopedAStatus Lights::setLightState(int id, const HwLightState& state) {
    LightStateHandler handler = nullptr;
    LightType type = static_cast<LightType>(id);

    /* Lock global mutex until light state is updated. */
    std::lock_guard<std::mutex> lock(globalLock);

    /* Update the cached state value for the current type. */
    for (LightBackend& backend : backends) {
        if (backend.type == type) {
            backend.state = state;
            handler = backend.handler;
        }
    }

    /* If no handler has been found, then the type is not supported. */
    if (!handler) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    /* Light up the type with the highest priority that matches the current handler. */
    for (LightBackend& backend : backends) {
        if (handler == backend.handler && isLit(backend.state)) {
            handler(backend.state);
            return ndk::ScopedAStatus::ok();
        }
    }

    /* If no type has been lit up, then turn off the hardware. */
    handler(state);

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Lights::getLights(std::vector<HwLight>* lights) {
    int i = 0;

    for (const LightBackend& backend : backends) {
        HwLight hwLight;
        hwLight.id = (int) backend.type;
        hwLight.type = backend.type;
        hwLight.ordinal = i;
        lights->push_back(hwLight);
        i++;
    }

    return ndk::ScopedAStatus::ok();
}

}  // namespace light
}  // namespace hardware
}  // namespace android
}  // namespace aidl
