#include <Arduino.h>

#include "pins.h"

/// Ignore further edges from the same button for this long after one is accepted.
const unsigned long DEBOUNCE_TIMEOUT = 250;

/// Message codes shared with the desktop application. Changing one breaks every device already
/// flashed, so they are part of the contract rather than an implementation detail.
enum MessageCode : uint8_t
{
    MSG_PING = 0x00,
    MSG_PONG = 0x01,
    MSG_BUTTON = 0x02,
    MSG_VOICE_SETTINGS = 0x03,
    MSG_RGB = 0x04,
};

enum ButtonId : uint8_t
{
    BUTTON_MUTE = 0x00,
    BUTTON_DEAFEN = 0x01,
    BUTTON_DISCONNECT = 0x02,
};

/// Frames are terminated by this byte, which is therefore not available inside a payload. The
/// desktop side lowers any 255 to 254 for the same reason.
const uint8_t FRAME_DELIMITER = 0xFF;

enum RgbMode : uint8_t
{
    RGB_MODE_CYCLE = 0x00,
    RGB_MODE_FIXED = 0x01,
    RGB_MODE_WAVE = 0x02,
};

typedef struct
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
} LED_RGB_T;

LED_RGB_T muteLed = {255, 255, 255, 255};
LED_RGB_T deafLed = {255, 255, 255, 255};

volatile bool mute = false;
volatile bool deafen = false;

/// Set by the interrupt handlers, acted on in loop().
///
/// The handlers used to write to Serial directly, but Serial.flush() blocks until the USB buffer
/// drains and doing that inside an interrupt can stall the core or lose the write entirely. An
/// interrupt should record what happened and return.
volatile bool muteButtonPressed = false;
volatile bool deafenButtonPressed = false;
volatile bool disconnectButtonPressed = false;

/// One timestamp per button. A single shared one meant pressing mute and then deafen within the
/// debounce window silently dropped the second press.
volatile unsigned long lastMuteInterrupt = 0;
volatile unsigned long lastDeafenInterrupt = 0;
volatile unsigned long lastDisconnectInterrupt = 0;

/// Whether the LEDs need rewriting. Without it the PWM registers were rewritten on every pass of
/// loop(), thousands of times a second, to keep showing the same colour.
bool ledsDirty = true;

void writeLed(uint8_t redPin, uint8_t greenPin, uint8_t bluePin, const LED_RGB_T &led, bool on)
{
    if (!on)
    {
        analogWrite(redPin, 0);
        analogWrite(greenPin, 0);
        analogWrite(bluePin, 0);
        return;
    }

    analogWrite(redPin, led.red * led.brightness / 255);
    analogWrite(greenPin, led.green * led.brightness / 255);
    analogWrite(bluePin, led.blue * led.brightness / 255);
}

void set_led_pwm()
{
    if (!ledsDirty)
    {
        return;
    }
    ledsDirty = false;

    // Deafening implies muting, which is what Discord itself enforces, so the mute LED follows
    // both.
    writeLed(MUTE_LED_RED, MUTE_LED_GREEN, MUTE_LED_BLUE, muteLed, mute || deafen);
    writeLed(DEAF_LED_RED, DEAF_LED_GREEN, DEAF_LED_BLUE, deafLed, deafen);
}

void sendFrame(const uint8_t *payload, size_t length)
{
    Serial.write(payload, length);
    Serial.write(FRAME_DELIMITER);
    Serial.flush();
}

void sendPong()
{
    const uint8_t payload[] = {MSG_PONG};
    sendFrame(payload, sizeof(payload));
}

void sendButton(uint8_t button)
{
    const uint8_t payload[] = {MSG_BUTTON, button};
    sendFrame(payload, sizeof(payload));
}

void handleVoiceSettings(uint8_t m, uint8_t d)
{
    bool newMute = (m != 0x00);
    bool newDeafen = (d != 0x00);

    if (newMute != mute || newDeafen != deafen)
    {
        mute = newMute;
        deafen = newDeafen;
        ledsDirty = true;
    }
}

void handleRgb(const uint8_t *payload, uint8_t length)
{
    // brightness and mode.
    if (length < 3)
    {
        return;
    }

    muteLed.brightness = payload[1];
    deafLed.brightness = payload[1];

    uint8_t mode = payload[2];
    if (mode == RGB_MODE_FIXED || mode == RGB_MODE_WAVE)
    {
        // Six colour bytes follow. The previous check only required three bytes in total, so a
        // truncated frame read past what had actually arrived.
        if (length < 9)
        {
            return;
        }

        muteLed.red = payload[3];
        muteLed.green = payload[4];
        muteLed.blue = payload[5];
        deafLed.red = payload[6];
        deafLed.green = payload[7];
        deafLed.blue = payload[8];
    }

    ledsDirty = true;
}

void dispatch(const uint8_t *payload, uint8_t length)
{
    // An empty frame is not a Ping. The buffer is zeroed after each frame and 0x00 happens to be
    // the Ping code, so a stray delimiter used to answer with a Pong nobody asked for — which is
    // exactly what the desktop app produced while it was sending its own delimiter twice.
    if (length == 0)
    {
        return;
    }

    switch (payload[0])
    {
    case MSG_PING:
        sendPong();
        break;
    case MSG_VOICE_SETTINGS:
        if (length >= 3)
        {
            handleVoiceSettings(payload[1], payload[2]);
        }
        break;
    case MSG_RGB:
        handleRgb(payload, length);
        break;
    case MSG_PONG:
    case MSG_BUTTON:
        // Sent by this device, never received by it.
        break;
    default:
        break;
    }
}

void handle_serial_input()
{
    static uint8_t buf[32];
    static uint8_t i = 0;

    while (Serial.available())
    {
        uint8_t c = Serial.read();

        if (c == FRAME_DELIMITER)
        {
            dispatch(buf, i);
            i = 0;
            continue;
        }

        if (i < sizeof(buf))
        {
            buf[i++] = c;
        }
        else
        {
            // Oversized frame: drop it and resynchronise on the next delimiter.
            i = 0;
        }
    }
}

void muteButtonISR()
{
    unsigned long now = millis();
    if (now - lastMuteInterrupt > DEBOUNCE_TIMEOUT)
    {
        lastMuteInterrupt = now;
        muteButtonPressed = true;
    }
}

void deafenButtonISR()
{
    unsigned long now = millis();
    if (now - lastDeafenInterrupt > DEBOUNCE_TIMEOUT)
    {
        lastDeafenInterrupt = now;
        deafenButtonPressed = true;
    }
}

void disconnectButtonISR()
{
    unsigned long now = millis();
    if (now - lastDisconnectInterrupt > DEBOUNCE_TIMEOUT)
    {
        lastDisconnectInterrupt = now;
        disconnectButtonPressed = true;
    }
}

/// Reports the presses the interrupts recorded, and toggles locally so the LED follows the
/// button even with no application connected. The app confirms or corrects it by sending back a
/// voice settings message.
void handle_buttons()
{
    if (muteButtonPressed)
    {
        muteButtonPressed = false;
        mute = !mute;
        ledsDirty = true;
        sendButton(BUTTON_MUTE);
    }

    if (deafenButtonPressed)
    {
        deafenButtonPressed = false;
        deafen = !deafen;
        ledsDirty = true;
        sendButton(BUTTON_DEAFEN);
    }

    if (disconnectButtonPressed)
    {
        disconnectButtonPressed = false;
        sendButton(BUTTON_DISCONNECT);
    }
}

void setup()
{
    pinMode(MUTE_BUTTON, INPUT_PULLUP);
    pinMode(DEAF_BUTTON, INPUT_PULLUP);
    pinMode(DISCONNECT_BUTTON, INPUT_PULLUP);

    // No LED_BUILTIN here: it was configured but never written to, and the board this runs on
    // has a WS2812 on GPIO16 instead of a plain LED, so the constant does not even exist for it.
    pinMode(MUTE_LED_RED, OUTPUT);
    pinMode(MUTE_LED_GREEN, OUTPUT);
    pinMode(MUTE_LED_BLUE, OUTPUT);
    pinMode(DEAF_LED_RED, OUTPUT);
    pinMode(DEAF_LED_GREEN, OUTPUT);
    pinMode(DEAF_LED_BLUE, OUTPUT);

    attachInterrupt(digitalPinToInterrupt(MUTE_BUTTON), muteButtonISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(DEAF_BUTTON), deafenButtonISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(DISCONNECT_BUTTON), disconnectButtonISR, FALLING);

    Serial.begin(115200);
}

void loop()
{
    handle_serial_input();
    handle_buttons();
    set_led_pwm();
}
