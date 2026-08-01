#ifndef _PINS_H_
#define _PINS_H_

// GP0 and GP1 carry UART0 by default on this part, but that alternate function only comes alive
// if something calls Serial1.begin(). The link to the desktop application is `Serial`, which is
// USB CDC on the RP2350, so nothing contends for them.
#define MUTE_BUTTON 0
#define DEAF_BUTTON 1
#define DISCONNECT_BUTTON 2

#define MUTE_LED_RED 5
#define MUTE_LED_GREEN 6
#define MUTE_LED_BLUE 7

#define DEAF_LED_RED 8
#define DEAF_LED_GREEN 9
#define DEAF_LED_BLUE 10

#endif
