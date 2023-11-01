// Copyright 2023 Zachary J. Fields. All rights reserved.
//
// Use of this source code is governed by licenses granted by the
// copyright holder including that found in the LICENSE file.

// Include the Standard libraries
#include <stdint.h>
#include <stdlib.h>

// Include the Arduino libraries
#include <Arduino.h>
#include <Notecard.h>
#include <NotecardEnvVarManager.h>

#define STREAM_SIGNALS

#define PIN_HORN 5
#define PIN_BATT A0
#define txRxSerial Serial1

// Singleton(s)
Notecard notecard;
NotecardEnvVarManager *env_var_mgr;
HardwareSerial stlink_serial(PIN_VCP_RX, PIN_VCP_TX);

#define PRODUCT_UID "com.blues.ces"

// This is the unique Product Identifier for your device
#ifndef PRODUCT_UID
#define PRODUCT_UID "" // "com.my-company.my-name:my-project"
#pragma message "PRODUCT_UID is not defined in this example. Please ensure your Notecard has a product identifier set before running this example or define it in code here. More details at https://dev.blues.io/tools-and-sdks/samples/product-uid"
#endif
#define my_product_id PRODUCT_UID

// Wi-Fi credentials
static char *wifi_ssid = nullptr;
static char *wifi_password = nullptr;

static const char * ENV_VAR_LIST[] = {"wifi_ssid", "wifi_password"};

struct SensorReadings {
    size_t raw_battery_reading;  // ADC reading of the battery voltage
    float battery_percentage;    // Percentage of battery life remaining
};

size_t last_sample_ms = 0;

int configureNotecard (void) {
    // Set basic Notecard configuration and behavior
    if (J *req = notecard.newRequest("hub.set")) {
        if (my_product_id[0])
        {
            JAddStringToObject(req, "product", my_product_id);
        }
        JAddStringToObject(req, "sn", "Blues Scooter");
        notecard.sendRequestWithRetry(req, 5); // 5 seconds
    }

    // Configure the Notecard v2 to use the built-in DFU mode
    if (J *req = notecard.newRequest("card.dfu")) {
        JAddStringToObject(req, "mode", "altdfu");
        JAddStringToObject(req, "name", "stm32");
        JAddBoolToObject(req, "on", true);
        notecard.sendRequest(req);
    }

    // Configure Notecard to report orientation change
    // Upright: {"orientation":"landscape-left"}
    if (J *req = notecard.newRequest("card.motion.sync")) {
        JAddBoolToObject(req, "start", true);
        JAddBoolToObject(req, "sync", true);
        notecard.sendRequest(req);
    }

    // Configure Notecard to forward signals over AUX_TX/RX
    if (J *req = notecard.newRequest("card.aux.serial")) {
#ifdef STREAM_SIGNALS
        JAddStringToObject(req, "mode", "notify,signals");
#else
        JAddStringToObject(req, "mode", "req");
#endif
        notecard.sendRequest(req);
    }

    // Set a template for the data
    if (J *req = notecard.newRequest("note.template")) {
        JAddIntToObject(req, "raw_battery_reading", TUINT16);
        JAddIntToObject(req, "battery_percentage", TFLOAT16);
        notecard.sendRequest(req);
    }

    return 0;
}

void envVarManagerCb(const char *var, const char *val, void *user_ctx) {
    (void)user_ctx;
    bool update_wifi = false;

    if (strcmp(var, "wifi_ssid") == 0) {
        if (!wifi_ssid || strncmp(wifi_ssid, val, 255) != 0) {
            delete(wifi_ssid);
            if (256 < strnlen(val, 255)) {
                notecard.logDebugf("[ERROR] Wi-Fi SSID is too long! (max: 255)\n");
            } else {
                const size_t ssid_len = (strnlen(val, 255) + 1);
                wifi_ssid = new char[256]; // Allocate as fixed chunk to preserve heap
                strlcpy(wifi_ssid, val, ssid_len);
                notecard.logDebugf("[APP] Updating Wi-Fi SSID to %s.\n", val);
                update_wifi = true;
            }
        }
    } else if (strcmp(var, "wifi_password") == 0) {
        if (!wifi_password || strncmp(wifi_password, val, 255) != 0) {
            delete(wifi_password);
            if (256 < strnlen(val, 255)) {
                notecard.logDebugf("[ERROR] Wi-Fi password is too long! (max: 255)\n");
            } else {
                const size_t password_len = (strnlen(val, 255) + 1);
                wifi_password = new char[256]; // Allocate as fixed chunk to preserve heap
                strlcpy(wifi_password, val, password_len);
                notecard.logDebug("[APP] Updating Wi-Fi password to ");
                for (size_t i = 0; i < password_len; ++i) {
                    notecard.logDebug("*");
                }
                notecard.logDebug(".\n");
                update_wifi = true;
            }
        }
    } else {
        notecard.logDebugf("[APP] Ignoring unknown environment variable: %s\n", var);
    }

    // Update Wi-Fi credentials (if necessary)
    if (update_wifi) {
        if (J *req = notecard.newRequest("card.wifi")) {
            JAddItemReferenceToObject(req, "ssid", JCreateString(wifi_ssid));
            JAddItemReferenceToObject(req, "password", JCreateString(wifi_password));
            notecard.sendRequest(req);
        }
    }
}

bool ignition (void) {
    bool ignition_state = false;

    if (J *req = notecard.newRequest("card.aux")) {
        JAddStringToObject(req, "mode", "gpio");
        if (J *pins = JAddArrayToObject(req, "usage")) {
            JAddItemToArray(pins, JCreateString("input")); // AUX1
            JAddItemToArray(pins, JCreateString("off"));   // AUX2
            JAddItemToArray(pins, JCreateString("off"));   // AUX3
            JAddItemToArray(pins, JCreateString("off"));   // AUX4
            if(J *rsp = notecard.requestAndResponse(req)) {
                if (J *state_array = JGetObjectItem(rsp,"state")) {
                    if (JIsArray(state_array)) {
                        if (J *aux1 = JGetArrayItem(state_array, 0)) {
                            if (J *state = JGetObjectItem(aux1, "high")) {
                                ignition_state = JIsTrue(state);
                            }
                        }
                    }
                }
                JDelete(rsp);
            }
        }
    }

    return ignition_state;
}

void mcuSleep (void) {
    // Configure low-power state

    // Sleep the MCU to conserve power
    if (J *req = notecard.newRequest("hub.set")) {
       JAddStringToObject(req, "mode", "periodic");
       JAddIntToObject(req, "outbound", 15);
       JAddIntToObject(req, "inbound", 15);
       notecard.sendRequestWithRetry(req, 5); // 5 seconds
    }

    // Set tracking mode to periodic
    // DISABLED: Indoor tracking at a convention center is impractical
    // if (J *req = notecard.newRequest("card.location.mode")) {
    //     JAddStringToObject(req, "mode", "periodic");
    //     JAddStringToObject(req, "vseconds","usb:360;high:3600;normal:7200;low:43200;dead:0");

    //     if (!notecard.sendRequest(req)) {
    //         notecard.logDebugf("[APP] Failed to adjust tracking.\n");
    //     }
    // }

    // Manually issue a hub.sync to ensure all tracking data is sent
    if (J *req = notecard.newRequest("hub.sync")) {
        JAddBoolToObject(req, "allow", true);
        notecard.sendRequest(req);
    }

    // Configure Notecard to wake on ignition
    if (J *req = notecard.newCommand("card.attn")) {
        JAddStringToObject(req, "mode", "rearm,auxgpio");
        notecard.sendRequest(req);
    }
}

int mcuWake (void) {
    // Configure normal-power state

    // Enables the Twilio integration to be responsive
    if (J *req = notecard.newRequest("hub.set")) {
       JAddStringToObject(req, "mode", "continuous");
       JAddBoolToObject(req, "sync", true);
       notecard.sendRequest(req);
    }

    // Set tracking mode to continuous
    // DISABLED: Indoor tracking at a convention center is impractical
    // if (J *req = notecard.newRequest("card.location.mode")) {
    //     JAddStringToObject(req, "mode", "continuous");

    //     if (!notecard.sendRequest(req)) {
    //         notecard.logDebugf("[APP] Failed to adjust tracking.\n");
    //     }
    // }

    return 0;
}

size_t processSignals (void) {
    size_t result = 0;
    char signal_buffer[256] = {0};

    // Process any Notecard signals
    if ((result = txRxSerial.readBytesUntil('\n', signal_buffer, sizeof(signal_buffer)))) {
        notecard.logDebugf("[APP] Received signal: %s\n", signal_buffer);
    }

    return result;
}

int queueReadingsToNotecard (SensorReadings &readings_) {
    J *req = notecard.newRequest("note.add");
    if (req != NULL)
    {
        J *body = JAddObjectToObject(req, "body");
        if (body != NULL)
        {
            JAddIntToObject(body, "raw_battery_reading", readings_.raw_battery_reading);
            JAddNumberToObject(body, "battery_percentage", readings_.battery_percentage);
        }
        if (!notecard.sendRequest(req)) {
            notecard.logDebugf("[APP] Failed to send sensor readings to Notecard.\n");
        }
    }

    return 0;
}

void sampleSensors (SensorReadings &readings_) {
    // Read the battery voltage
    readings_.raw_battery_reading = ::analogRead(PIN_BATT);

    // Clamp the raw battery reading to a fixed range
    if (readings_.raw_battery_reading < 500) {
        readings_.raw_battery_reading = 500;
    } else if (readings_.raw_battery_reading > 800) {
        readings_.raw_battery_reading = 800;
    }

    // A value of 800 represents a fully-charged battery on the scooter (~6.40V)
    // A value of 500 represents a dead battery on the scooter (~3.75V)
    readings_.battery_percentage = (((readings_.raw_battery_reading - 500) / 300.0f) * 100.0f);

    // Reset the last sample time
    last_sample_ms = millis();
}

// One-time Arduino initialization
void setup()
{
    // Initialize the built-in LED pin as an output
    ::pinMode(LED_BUILTIN, OUTPUT);
    ::digitalWrite(LED_BUILTIN, HIGH);

    // Initialize the horn pin as an output
    ::pinMode(PIN_HORN, OUTPUT);

    // Initialize debug output
#ifndef RELEASE
    stlink_serial.begin(115200);
    const size_t usb_timeout_ms = 3000;
    for (const size_t start_ms = millis(); !stlink_serial && (millis() - start_ms) < usb_timeout_ms;)
        ;
    notecard.setDebugOutputStream(stlink_serial);
#endif

    // Initialize the Notecard serial port
    txRxSerial.begin(115200);

    // Initialize the physical I/O channel to the Notecard
    notecard.begin();

    // Configure the Notecard
    configureNotecard();

    // Configure the Notecard Environment Variable Manager
    env_var_mgr = NotecardEnvVarManager_alloc();
    NotecardEnvVarManager_setEnvVarCb(env_var_mgr, envVarManagerCb, nullptr);

    notecard.logDebugf("[APP] Ignition ON.\n");
    mcuWake();
}

// In the Arduino main loop which is called repeatedly
void loop()
{
    // Only sample sensors once every 15 seconds
    if ((millis() - last_sample_ms) > 15000) {
        NotecardEnvVarManager_fetch(env_var_mgr, ENV_VAR_LIST, (sizeof(ENV_VAR_LIST) / sizeof(ENV_VAR_LIST[0])));

        // Capture diagnostic information
        SensorReadings readings{0, 0.0f};
        sampleSensors(readings);
        queueReadingsToNotecard(readings);

        // Check ignition state
        if (!ignition()) {
            notecard.logDebugf("[APP] Ignition OFF.\n");
            mcuSleep();
        }
    }

#ifdef STREAM_SIGNALS
    // Process any Notecard signals
    while (txRxSerial.available()) {
        if (processSignals()) {
            ::digitalWrite(PIN_HORN, HIGH);
            ::delay(250);
            ::digitalWrite(PIN_HORN, LOW);
        }
    }
#else
    J *rsp = notecard.requestAndResponse(notecard.newRequest("hub.signal"));
    if (rsp) {
        J *body = JGetObject(rsp, "body");
        if (body) {
            const char *body_str = JConvertToJSONString(body);
            notecard.logDebugf("[APP] Received signal: %s\n", body_str);
            free((void *)body_str);
        }
        JDelete(rsp);
    }
    ::delay(5000);
#endif
}
