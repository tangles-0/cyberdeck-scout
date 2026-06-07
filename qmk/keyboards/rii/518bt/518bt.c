/* Copyright 2017 WoodKeys
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "518bt.h"

#define USER_LED_1 B2
#define USER_LED_2 B10
#define USER_LED_3 B11

void keyboard_pre_init_kb(void) {
    gpio_set_pin_output(USER_LED_1);
    gpio_set_pin_output(USER_LED_2);
    gpio_set_pin_output(USER_LED_3);
    gpio_write_pin_low(USER_LED_1);
    gpio_write_pin_low(USER_LED_2);
    gpio_write_pin_low(USER_LED_3);
    keyboard_pre_init_user();
}

inline void user_led_on(uint8_t led) {
    switch (led) {
        case 0:
            gpio_write_pin_high(USER_LED_1);
            break;
        case 1:
            gpio_write_pin_high(USER_LED_2);
            break;
        case 2:
            gpio_write_pin_high(USER_LED_3);
            break;
    }
}

inline void user_led_off(uint8_t led) {
    switch (led) {
        case 0:
            gpio_write_pin_low(USER_LED_1);
            break;
        case 1:
            gpio_write_pin_low(USER_LED_2);
            break;
        case 2:
            gpio_write_pin_low(USER_LED_3);
            break;
    }
}
