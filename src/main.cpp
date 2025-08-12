#include <Arduino.h>

#include "pins.h"

const uint DEBOUNCE_TIMEOUT = 200;

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
volatile unsigned long lastInterruptTime = 0;

void set_led_pwm()
{
    if (mute)
    {
        analogWrite(MUTE_LED_BLUE, muteLed.blue * muteLed.brightness / 255);
        analogWrite(MUTE_LED_GREEN, muteLed.green * muteLed.brightness / 255);
        analogWrite(MUTE_LED_RED, muteLed.red * muteLed.brightness / 255);
    }
    else
    {
        analogWrite(MUTE_LED_BLUE, 0);
        analogWrite(MUTE_LED_GREEN, 0);
        analogWrite(MUTE_LED_RED, 0);
    }

    if (deafen)
    {
        analogWrite(DEAF_LED_BLUE, deafLed.blue * deafLed.brightness / 255);
        analogWrite(DEAF_LED_GREEN, deafLed.green * deafLed.brightness / 255);
        analogWrite(DEAF_LED_RED, deafLed.red * deafLed.brightness / 255);
    }
    else
    {
        analogWrite(DEAF_LED_BLUE, 0);
        analogWrite(DEAF_LED_GREEN, 0);
        analogWrite(DEAF_LED_RED, 0);
    }
}

void handlePing()
{
    byte data[] = {0x01, 0xFF}; // Pong response
    Serial.write(data, sizeof(data));
    Serial.flush();
}

void handleVoiceSettings(char m, char d)
{
    // Implement command 1 functionality
    mute = (m != 0x00);
    deafen = (d != 0x00);
    digitalWrite(PICO_DEFAULT_LED_PIN, mute);
}

void handleUnknown()
{
    // Handle unknown command
}

void handle_serial_input()
{
    static char buf[30];
    static uint8_t i = 0;

    while (Serial.available())
    {
        uint8_t c = Serial.read();

        if (c == 0xFF)
        {
            buf[i] = 0xFF;
            switch (buf[0])
            {
            case 0x00: // Ping
                handlePing();
                break;
            case 0x01: // Pong
                // Do nothing
                break;
            case 0x02: // Button
                // Do nothing
                break;
            case 0x03: // Voice Settings
                if (i > 2)
                {
                    handleVoiceSettings(buf[1], buf[2]);
                }

                break;
            default:
                handleUnknown();
                break;
            }

            i = 0;
            memset(buf, 0, sizeof(buf));
        }
        else
        {
            if (i < sizeof(buf) - 1)
            {
                buf[i++] = c;
            }
            else
            {
                i = 0;
                memset(buf, 0, sizeof(buf));
            }
        }
    }
}

void muteButtonISR()
{
    unsigned long currentTime = millis();

    if (currentTime - lastInterruptTime > DEBOUNCE_TIMEOUT)
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

    if (currentTime - lastInterruptTime > DEBOUNCE_TIMEOUT)
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

    if (currentTime - lastInterruptTime > DEBOUNCE_TIMEOUT)
    {
        lastInterruptTime = currentTime;
        byte data2[] = {0x02, 0x02, 0xFF};
        Serial.write(data2, sizeof(data2));
        Serial.flush();
    }
}

void setup()
{
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

    Serial.begin(115200);
}

void loop()
{
    handle_serial_input();
    set_led_pwm();
}
