#include <Arduino.h>

#include "pins.h"

typedef struct
{
    bool usb_rx_available;
    bool mute_button_pressed;
    bool deaf_button_pressed;
    bool disconnect_button_pressed;
    bool usb_tx_available;
    bool usb_waiting_ack;
    bool ds_muted;
    bool ds_deafen;
    uint8_t led_red_level;
    uint8_t led_green_level;
    uint8_t led_blue_level;
    uint8_t led_brightness;
} DS_STATUS_T;

DS_STATUS_T state;

volatile unsigned long lastInterruptTime = 0;

void set_led_pwm()
{
    if (state.ds_muted)
    {
        analogWrite(MUTE_LED_BLUE, state.led_blue_level * state.led_brightness / 255);
        analogWrite(MUTE_LED_GREEN, state.led_green_level * state.led_brightness / 255);
        analogWrite(MUTE_LED_RED, state.led_red_level * state.led_brightness / 255);
    }
    else
    {
        analogWrite(MUTE_LED_BLUE, 0);
        analogWrite(MUTE_LED_GREEN, 0);
        analogWrite(MUTE_LED_RED, 0);
    }

    if (state.ds_deafen)
    {
        analogWrite(DEAF_LED_BLUE, state.led_blue_level * state.led_brightness / 255);
        analogWrite(DEAF_LED_GREEN, state.led_green_level * state.led_brightness / 255);
        analogWrite(DEAF_LED_RED, state.led_red_level * state.led_brightness / 255);
    }
    else
    {
        analogWrite(DEAF_LED_BLUE, 0);
        analogWrite(DEAF_LED_GREEN, 0);
        analogWrite(DEAF_LED_RED, 0);
    }
}

void handle_serial_input()
{
    static char buf[30];
    static uint8_t i = 0;
    static bool expecting_delimiter = false;

    while (Serial.available())
    {
        uint8_t c = Serial.read();

        // Check for binary ping message (0x00 followed by 0xFF)
        if (!expecting_delimiter && c == 0x00)
        {
            expecting_delimiter = true;
            continue;
        }
        else if (expecting_delimiter)
        {
            if (c == 0xFF)
            {

                // Received ping (0x00 0xFF), respond with pong (0x01 0xFF)
                byte data[] = {0x01, 0xFF}; // Array con los bytes a enviar
                Serial.write(data, sizeof(data));
                Serial.flush();

                // byte data2[] = {0x02, 0x01}; // Array con los bytes a enviar
                // Serial.write(data2, sizeof(data2));
                // Serial.flush();
                // Set LED colors as in original PING response
                // analogWrite(MUTE_LED_BLUE, 0);
                // analogWrite(MUTE_LED_GREEN, 255);
                // analogWrite(MUTE_LED_RED, 255);
            }
            expecting_delimiter = false;
            continue;
        }
    }
}

void muteButtonISR()
{
    unsigned long currentTime = millis();

    if (currentTime - lastInterruptTime > 200)
    {
        lastInterruptTime = currentTime;
        byte data2[] = {0x02, 0x00, 0xFF};
        Serial.write(data2, sizeof(data2));
        Serial.flush();
    }
}

void deafenButtonISR()
{
    unsigned long currentTime = millis();

    if (currentTime - lastInterruptTime > 200)
    {
        lastInterruptTime = currentTime;
        byte data2[] = {0x02, 0x01, 0xFF};
        Serial.write(data2, sizeof(data2));
        Serial.flush();
    }
}

void disconnectButtonISR()
{
    unsigned long currentTime = millis();

    if (currentTime - lastInterruptTime > 200)
    {
        lastInterruptTime = currentTime;
        byte data2[] = {0x02, 0x02, 0xFF};
        Serial.write(data2, sizeof(data2));
        Serial.flush();
    }
}

void setup()
{
    Serial.begin(115200);

    pinMode(MUTE_BUTTON, INPUT_PULLUP);
    pinMode(DEAF_BUTTON, INPUT_PULLUP);
    pinMode(DISCONNECT_BUTTON, INPUT_PULLUP);

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(MUTE_LED_RED, OUTPUT);
    pinMode(MUTE_LED_GREEN, OUTPUT);
    pinMode(MUTE_LED_BLUE, OUTPUT);
    pinMode(DEAF_LED_RED, OUTPUT);
    pinMode(DEAF_LED_GREEN, OUTPUT);
    pinMode(DEAF_LED_BLUE, OUTPUT);
    attachInterrupt(digitalPinToInterrupt(MUTE_BUTTON), muteButtonISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(DEAF_BUTTON), deafenButtonISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(DISCONNECT_BUTTON), disconnectButtonISR, FALLING);

    // state = {false};
    state.led_red_level = 255;
    state.led_green_level = 255;
    state.led_blue_level = 255;
    state.led_brightness = 64;
}

void loop()
{
    handle_serial_input();
}
