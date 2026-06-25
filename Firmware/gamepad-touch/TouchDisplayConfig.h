/*
 * Select the display/touch stack used by this build.
 *
 * TFP401_FT5206 is the currently implemented touch controller path.
 * ILI9881_GT911 is reserved for the GT911 driver work.
 */
#ifndef TOUCH_DISPLAY_CONFIG_H
#define TOUCH_DISPLAY_CONFIG_H

//#define TFP401_FT5206
#define ILI9881_GT911

#if defined(TFP401_FT5206) && defined(ILI9881_GT911)
#error "Select only one display/touch stack"
#endif

#if !defined(TFP401_FT5206) && !defined(ILI9881_GT911)
#error "Select a display/touch stack"
#endif

#define DISP_SDA 4
#define DISP_SCL 5

#if defined(TFP401_FT5206)
#define TOUCH_CONTROLLER_FT5206
#define TOUCH_CONTROLLER_NAME "FT5206"
#define DISP_RESET 2
#define DISP_INT 3
#define TOUCH_HID_LOGICAL_MAX_X 799
#define TOUCH_HID_LOGICAL_MAX_Y 479
#define GAMEPAD_A 16
#define GAMEPAD_B 17
#elif defined(ILI9881_GT911)
#define TOUCH_CONTROLLER_GT911
#define TOUCH_CONTROLLER_NAME "GT911"
#define DISP_RESET 18
#define DISP_INT 19
// The 720x1280 portrait panel is used rotated into 1280x720 landscape.
#define TOUCH_HID_LOGICAL_MAX_X 1279
#define TOUCH_HID_LOGICAL_MAX_Y 719
#define GAMEPAD_A 6
#define GAMEPAD_B 7
#endif

#endif  // TOUCH_DISPLAY_CONFIG_H
