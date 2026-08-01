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
    RGB_MODE_BREATHING = 0x02,
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

/// The mode the application last asked for.
///
/// This used to be a local inside handleRgb, read only to decide whether six colour bytes
/// followed. Nothing remembered it, so the animated modes had nowhere to live: cycle froze on the
/// last colour it happened to be given and breathing was indistinguishable from fixed.
uint8_t rgbMode = RGB_MODE_FIXED;

/// When the last animation frame was drawn, so the animated modes advance on wall-clock time
/// rather than on however fast loop() happens to spin.
unsigned long lastAnimationFrame = 0;

/// 60 Hz. Smooth to the eye, and still leaves the loop overwhelmingly idle.
const unsigned long ANIMATION_INTERVAL = 16;

/// How long one full hue sweep and one full inhale-exhale take.
const unsigned long CYCLE_PERIOD = 6000;
const unsigned long BREATH_PERIOD = 3000;

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

/// Full-saturation colour for a hue in 0..1535.
///
/// Six 256-wide sectors of the colour wheel, walked with integer arithmetic. A real HSV
/// conversion would need floating point for no visible gain: at 256 steps per sector the
/// staircase is already finer than the LED can resolve.
void hueToRgb(uint16_t hue, uint8_t &red, uint8_t &green, uint8_t &blue)
{
    const uint8_t sector = hue / 256;
    const uint8_t offset = hue % 256;

    switch (sector)
    {
    case 0:  red = 255;          green = offset;       blue = 0;            break;
    case 1:  red = 255 - offset; green = 255;          blue = 0;            break;
    case 2:  red = 0;            green = 255;          blue = offset;       break;
    case 3:  red = 0;            green = 255 - offset; blue = 255;          break;
    case 4:  red = offset;       green = 0;            blue = 255;          break;
    default: red = 255;          green = 0;            blue = 255 - offset; break;
    }
}

/// Where in the breath we are, 0 (dark) to 255 (full).
///
/// The triangle is squared because perceived brightness is nowhere near linear in duty cycle: a
/// bare triangle reads as a hard bounce at the top and a long dead stretch at the bottom, which
/// does not look like breathing.
uint8_t breathLevel(unsigned long now)
{
    const uint16_t phase = (uint32_t)(now % BREATH_PERIOD) * 512 / BREATH_PERIOD;
    const uint8_t triangle = phase < 256 ? phase : 511 - phase;
    return (uint16_t)triangle * triangle / 255;
}

void set_led_pwm()
{
    const unsigned long now = millis();
    const bool animated = (rgbMode == RGB_MODE_CYCLE || rgbMode == RGB_MODE_BREATHING);

    if (animated)
    {
        // Rate-limited rather than dirty-checked: an animation is never done changing, so it
        // drives the PWM on a clock of its own instead of waiting to be told something moved.
        if (now - lastAnimationFrame < ANIMATION_INTERVAL)
        {
            return;
        }
        lastAnimationFrame = now;
    }
    else if (!ledsDirty)
    {
        return;
    }
    ledsDirty = false;

    LED_RGB_T first = muteLed;
    LED_RGB_T second = deafLed;

    if (rgbMode == RGB_MODE_CYCLE)
    {
        // Cycle owns the colour, so whatever was last configured is ignored while it runs. Both
        // LEDs share a hue: with only two of them, offsetting them reads as a fault rather than
        // as an effect.
        const uint16_t hue = (uint32_t)(now % CYCLE_PERIOD) * 1536 / CYCLE_PERIOD;
        hueToRgb(hue, first.red, first.green, first.blue);
        second.red = first.red;
        second.green = first.green;
        second.blue = first.blue;
    }
    else if (rgbMode == RGB_MODE_BREATHING)
    {
        // Breathing keeps the configured colour and modulates only the brightness, on top of the
        // level the application asked for rather than replacing it.
        const uint8_t level = breathLevel(now);
        first.brightness = (uint16_t)first.brightness * level / 255;
        second.brightness = (uint16_t)second.brightness * level / 255;
    }

    // Deafening implies muting, which is what Discord itself enforces, so the mute LED follows
    // both.
    writeLed(MUTE_LED_RED, MUTE_LED_GREEN, MUTE_LED_BLUE, first, mute || deafen);
    writeLed(DEAF_LED_RED, DEAF_LED_GREEN, DEAF_LED_BLUE, second, deafen);
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

    const uint8_t mode = payload[2];
    if (mode != RGB_MODE_CYCLE && mode != RGB_MODE_FIXED && mode != RGB_MODE_BREATHING)
    {
        // Drop the whole frame rather than adopt half of it: a mode byte this side does not know
        // means the desktop application is ahead of this firmware, and guessing would leave the
        // brightness applied under an effect that was never asked for.
        return;
    }

    muteLed.brightness = payload[1];
    deafLed.brightness = payload[1];
    rgbMode = mode;

    if (mode == RGB_MODE_FIXED || mode == RGB_MODE_BREATHING)
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
