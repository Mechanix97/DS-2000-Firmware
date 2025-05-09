#include <Arduino.h>

#define MUTE_BUTTON         4
#define DEAF_BUTTON         3
#define DISCONNECT_BUTTON   2

#define MUTE_LED_RED        7
#define MUTE_LED_GREEN      6
#define MUTE_LED_BLUE       5

#define DEAF_LED_RED        10
#define DEAF_LED_GREEN      9
#define DEAF_LED_BLUE       8

typedef struct {
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

unsigned long last_ack_time = 0;

void set_led_pwm() {
    if (state.ds_muted) {
        analogWrite(MUTE_LED_BLUE,  state.led_blue_level   * state.led_brightness / 255);
        analogWrite(MUTE_LED_GREEN, state.led_green_level  * state.led_brightness / 255);
        analogWrite(MUTE_LED_RED,   state.led_red_level    * state.led_brightness / 255);
    } else {
        analogWrite(MUTE_LED_BLUE,  0);
        analogWrite(MUTE_LED_GREEN, 0);
        analogWrite(MUTE_LED_RED,   0);
    }

    if (state.ds_deafen) {
        analogWrite(DEAF_LED_BLUE,  state.led_blue_level   * state.led_brightness / 255);
        analogWrite(DEAF_LED_GREEN, state.led_green_level  * state.led_brightness / 255);
        analogWrite(DEAF_LED_RED,   state.led_red_level    * state.led_brightness / 255);
    } else {
        analogWrite(DEAF_LED_BLUE,  0);
        analogWrite(DEAF_LED_GREEN, 0);
        analogWrite(DEAF_LED_RED,   0);
    }
}

void get_led_pwm(char* buf) {
    char* r = strtok(buf, "|");
    char* g = strtok(NULL, "|");
    char* b = strtok(NULL, "|");
    char* br = strtok(NULL, "|\n");

    if (r && g && b && br) {
        uint8_t ir = atoi(r);
        uint8_t ig = atoi(g);
        uint8_t ib = atoi(b);
        uint8_t ibri = atoi(br);
        state.led_red_level = ir;
        state.led_green_level = ig;
        state.led_blue_level = ib;
        state.led_brightness = ibri;
        Serial.println("ACK");
    } else {
        Serial.println("NACK");
    }
}

void get_ds_status(char* buf) {
    if (buf[0] == '0') state.ds_muted = false;
    else if (buf[0] == '1') state.ds_muted = true;
    else { Serial.println("NACK"); return; }

    if (buf[1] == '0') state.ds_deafen = false;
    else if (buf[1] == '1') state.ds_deafen = true;
    else { Serial.println("NACK"); return; }

    Serial.println("ACK");
}

void handle_serial_input() {
    static char buf[30];
    static uint8_t i = 0;

    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || i >= sizeof(buf) - 1) {
            buf[i] = '\0';
            if (strncmp(buf, "HWLS-", 5) == 0) {
                get_led_pwm(buf + 5);
            } else if (strcmp(buf, "PING") == 0) {
                Serial.println("PONG");
            } else if (strncmp(buf, "DSST-", 5) == 0) {
                get_ds_status(buf + 5);
            } else if (strcmp(buf, "ACK") == 0) {
                state.usb_waiting_ack = false;
            } else {
                Serial.println("NACK");
            }
            i = 0;
            break;
        } else {
            buf[i++] = c;
        }
    }
}

void io_poll() {
    if (!state.usb_tx_available && !state.usb_waiting_ack) {
        if (digitalRead(MUTE_BUTTON) == LOW) {
            delay(100);
            if (digitalRead(MUTE_BUTTON) == LOW) {
                state.ds_muted = !state.ds_muted;
                state.usb_tx_available = true;
            }
        }
        if (digitalRead(DEAF_BUTTON) == LOW) {
            delay(100);
            if (digitalRead(DEAF_BUTTON) == LOW) {
                state.ds_deafen = !state.ds_deafen;
                state.usb_tx_available = true;
            }
        }
        if (!state.disconnect_button_pressed && digitalRead(DISCONNECT_BUTTON) == LOW) {
            delay(100);
            if (digitalRead(DISCONNECT_BUTTON) == LOW) {
                state.disconnect_button_pressed = true;
                state.usb_tx_available = true;
            }
        }
    }

    handle_serial_input();

    if (state.usb_tx_available) {
        state.usb_tx_available = false;
        Serial.print("HWST-");
        Serial.print(state.ds_muted ? "1" : "0");
        Serial.print(state.ds_deafen ? "1" : "0");
        Serial.println(state.disconnect_button_pressed ? "1" : "0");

        if (state.disconnect_button_pressed) {
            state.disconnect_button_pressed = false;
            state.ds_muted = false;
            state.ds_deafen = false;
        }

        state.usb_waiting_ack = true;
        last_ack_time = millis();
    }

    if (state.usb_waiting_ack && (millis() - last_ack_time > 1000)) {
        Serial.println("DSST");
        state.usb_waiting_ack = false;
    }

    set_led_pwm();
}

void setup() {
    Serial.begin(115200);

    pinMode(MUTE_BUTTON, INPUT_PULLUP);
    pinMode(DEAF_BUTTON, INPUT_PULLUP);
    pinMode(DISCONNECT_BUTTON, INPUT_PULLUP);

    pinMode(MUTE_LED_RED, OUTPUT);
    pinMode(MUTE_LED_GREEN, OUTPUT);
    pinMode(MUTE_LED_BLUE, OUTPUT);
    pinMode(DEAF_LED_RED, OUTPUT);
    pinMode(DEAF_LED_GREEN, OUTPUT);
    pinMode(DEAF_LED_BLUE, OUTPUT);

    state = { false };
    state.led_red_level = 255;
    state.led_green_level = 255;
    state.led_blue_level = 255;
    state.led_brightness = 64;
}

void loop() {
    io_poll();
}
