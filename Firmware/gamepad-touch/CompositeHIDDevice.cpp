#include "CompositeHIDDevice.h"
#include "usb_phy_api.h"

using namespace arduino;

// Combined report descriptor: Mouse (Report ID 1) + Gamepad (Report ID 2) + Touch Screen (Report ID 3)
const uint8_t *CompositeHIDDevice::report_desc() {
  static const uint8_t reportDescriptor[] = {
    // ===== Mouse (Report ID 1) =====
    0x05, 0x01,             // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,             // USAGE (Mouse)
    0xA1, 0x01,             // COLLECTION (Application)
    0x85, REPORT_ID_MOUSE,  //   REPORT_ID (1)
    0x09, 0x01,             //   USAGE (Pointer)
    0xA1, 0x00,             //   COLLECTION (Physical)
    // Buttons (3)
    0x05, 0x09,  //     USAGE_PAGE (Button)
    0x19, 0x01,  //     USAGE_MINIMUM (Button 1)
    0x29, 0x03,  //     USAGE_MAXIMUM (Button 3)
    0x15, 0x00,  //     LOGICAL_MINIMUM (0)
    0x25, 0x01,  //     LOGICAL_MAXIMUM (1)
    0x95, 0x03,  //     REPORT_COUNT (3)
    0x75, 0x01,  //     REPORT_SIZE (1)
    0x81, 0x02,  //     INPUT (Data,Var,Abs) ; 3 buttons
    0x75, 0x05,  //     REPORT_SIZE (5)
    0x95, 0x01,  //     REPORT_COUNT (1)
    0x81, 0x03,  //     INPUT (Cnst,Var,Abs) ; 5 bits padding

    // X, Y (relative)
    0x05, 0x01,  //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,  //     USAGE (X)
    0x09, 0x31,  //     USAGE (Y)
    0x15, 0x81,  //     LOGICAL_MINIMUM (-127)
    0x25, 0x7F,  //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,  //     REPORT_SIZE (8)
    0x95, 0x02,  //     REPORT_COUNT (2)
    0x81, 0x06,  //     INPUT (Data,Var,Rel)

    // Wheel
    0x09, 0x38,  //     USAGE (Wheel)
    0x15, 0x81,  //     LOGICAL_MINIMUM (-127)
    0x25, 0x7F,  //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,  //     REPORT_SIZE (8)
    0x95, 0x01,  //     REPORT_COUNT (1)
    0x81, 0x06,  //     INPUT (Data,Var,Rel)
    0xC0,        //   END_COLLECTION (Physical)
    0xC0,        // END_COLLECTION (Application)

    // ===== Gamepad (Report ID 2) =====
    0x05, 0x01,               // USAGE_PAGE (Generic Desktop)
    0x09, 0x04,               // USAGE (Gamepad)
    0xA1, 0x01,               // COLLECTION (Application)
    0x85, REPORT_ID_GAMEPAD,  //   REPORT_ID (2)

    // BUTTONS (128 buttons)
    0x05, 0x09,  //   USAGE_PAGE (Button)
    0x19, 0x01,  //   USAGE_MINIMUM (Button 1)
    0x29, 0x80,  //   USAGE_MAXIMUM (Button 128)
    0x15, 0x00,  //   LOGICAL_MINIMUM (0)
    0x25, 0x01,  //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,  //   REPORT_SIZE (1)
    0x95, 0x80,  //   REPORT_COUNT (128)
    0x81, 0x02,  //   INPUT (Data,Var,Abs)

    // ANALOG AXES (X, Y only, 16-bit signed)
    0x05, 0x01,        //   USAGE_PAGE (Generic Desktop)
    0x09, 0x30,        //   USAGE (X)
    0x09, 0x31,        //   USAGE (Y)
    0x16, 0x00, 0x80,  // LOGICAL_MINIMUM (-32768)
    0x26, 0xFF, 0x7F,  // LOGICAL_MAXIMUM (32767)
    0x75, 0x10,        // REPORT_SIZE (16)
    0x95, 0x02,        // REPORT_COUNT (2)
    0x81, 0x02,        // INPUT (Data,Var,Abs)

    // HAT SWITCH (1 x 4-bit) + 4-bit padding
    0x09, 0x39,        //   USAGE (Hat switch)
    0x15, 0x00,        //   LOGICAL_MINIMUM (0)
    0x25, 0x07,        //   LOGICAL_MAXIMUM (7)
    0x35, 0x00,        //   PHYSICAL_MINIMUM (0)
    0x46, 0x38, 0x01,  //   PHYSICAL_MAXIMUM (315)
    0x65, 0x14,        //   UNIT (Eng Rot:Angular Pos)
    0x75, 0x04,        //   REPORT_SIZE (4)
    0x95, 0x01,        //   REPORT_COUNT (1)
    0x81, 0x02,        //   INPUT (Data,Var,Abs)
    0x75, 0x04,        //   REPORT_SIZE (4)
    0x95, 0x01,        //   REPORT_COUNT (1)
    0x81, 0x03,        //   INPUT (Cnst,Var,Abs) ; padding
    0xC0,  // END_COLLECTION

    // ===== Touch Screen (Report ID 3) =====
    0x05, 0x0D,              // USAGE_PAGE (Digitizers)
    0x09, 0x04,              // USAGE (Touch Screen)
    0xA1, 0x01,              // COLLECTION (Application)
    0x85, REPORT_ID_TOUCH,   //   REPORT_ID (3)

    // Contact 1
    0x09, 0x22,  //   USAGE (Finger)
    0xA1, 0x02,  //   COLLECTION (Logical)
    0x09, 0x42,  //     USAGE (Tip Switch)
    0x09, 0x32,  //     USAGE (In Range)
    0x15, 0x00,  //     LOGICAL_MINIMUM (0)
    0x25, 0x01,  //     LOGICAL_MAXIMUM (1)
    0x75, 0x01,  //     REPORT_SIZE (1)
    0x95, 0x02,  //     REPORT_COUNT (2)
    0x81, 0x02,  //     INPUT (Data,Var,Abs)
    0x75, 0x06,  //     REPORT_SIZE (6)
    0x95, 0x01,  //     REPORT_COUNT (1)
    0x81, 0x03,  //     INPUT (Cnst,Var,Abs)
    0x09, 0x51,  //     USAGE (Contact Identifier)
    0x75, 0x08,  //     REPORT_SIZE (8)
    0x95, 0x01,  //     REPORT_COUNT (1)
    0x25, 0x7F,  //     LOGICAL_MAXIMUM (127)
    0x81, 0x02,  //     INPUT (Data,Var,Abs)
    0x05, 0x01,  //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,  //     USAGE (X)
    0x26, LSB(TOUCH_LOGICAL_MAX_X), MSB(TOUCH_LOGICAL_MAX_X),  // LOGICAL_MAXIMUM
    0x75, 0x10,                                              // REPORT_SIZE (16)
    0x95, 0x01,                                              // REPORT_COUNT (1)
    0x81, 0x02,                                              // INPUT (Data,Var,Abs)
    0x09, 0x31,                                              //     USAGE (Y)
    0x26, LSB(TOUCH_LOGICAL_MAX_Y), MSB(TOUCH_LOGICAL_MAX_Y),  // LOGICAL_MAXIMUM
    0x81, 0x02,                                              // INPUT (Data,Var,Abs)
    0x05, 0x0D,                                              // USAGE_PAGE (Digitizers)
    0xC0,                                                    //   END_COLLECTION

    // Contact 2
    0x09, 0x22, 0xA1, 0x02, 0x09, 0x42, 0x09, 0x32, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0x75, 0x06, 0x95, 0x01, 0x81, 0x03,
    0x09, 0x51, 0x75, 0x08, 0x95, 0x01, 0x25, 0x7F, 0x81, 0x02, 0x05, 0x01,
    0x09, 0x30, 0x26, LSB(TOUCH_LOGICAL_MAX_X), MSB(TOUCH_LOGICAL_MAX_X), 0x75,
    0x10, 0x95, 0x01, 0x81, 0x02, 0x09, 0x31, 0x26, LSB(TOUCH_LOGICAL_MAX_Y),
    MSB(TOUCH_LOGICAL_MAX_Y), 0x81, 0x02, 0x05, 0x0D, 0xC0,

    // Contact 3
    0x09, 0x22, 0xA1, 0x02, 0x09, 0x42, 0x09, 0x32, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0x75, 0x06, 0x95, 0x01, 0x81, 0x03,
    0x09, 0x51, 0x75, 0x08, 0x95, 0x01, 0x25, 0x7F, 0x81, 0x02, 0x05, 0x01,
    0x09, 0x30, 0x26, LSB(TOUCH_LOGICAL_MAX_X), MSB(TOUCH_LOGICAL_MAX_X), 0x75,
    0x10, 0x95, 0x01, 0x81, 0x02, 0x09, 0x31, 0x26, LSB(TOUCH_LOGICAL_MAX_Y),
    MSB(TOUCH_LOGICAL_MAX_Y), 0x81, 0x02, 0x05, 0x0D, 0xC0,

    // Contact 4
    0x09, 0x22, 0xA1, 0x02, 0x09, 0x42, 0x09, 0x32, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0x75, 0x06, 0x95, 0x01, 0x81, 0x03,
    0x09, 0x51, 0x75, 0x08, 0x95, 0x01, 0x25, 0x7F, 0x81, 0x02, 0x05, 0x01,
    0x09, 0x30, 0x26, LSB(TOUCH_LOGICAL_MAX_X), MSB(TOUCH_LOGICAL_MAX_X), 0x75,
    0x10, 0x95, 0x01, 0x81, 0x02, 0x09, 0x31, 0x26, LSB(TOUCH_LOGICAL_MAX_Y),
    MSB(TOUCH_LOGICAL_MAX_Y), 0x81, 0x02, 0x05, 0x0D, 0xC0,

    // Contact 5
    0x09, 0x22, 0xA1, 0x02, 0x09, 0x42, 0x09, 0x32, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0x75, 0x06, 0x95, 0x01, 0x81, 0x03,
    0x09, 0x51, 0x75, 0x08, 0x95, 0x01, 0x25, 0x7F, 0x81, 0x02, 0x05, 0x01,
    0x09, 0x30, 0x26, LSB(TOUCH_LOGICAL_MAX_X), MSB(TOUCH_LOGICAL_MAX_X), 0x75,
    0x10, 0x95, 0x01, 0x81, 0x02, 0x09, 0x31, 0x26, LSB(TOUCH_LOGICAL_MAX_Y),
    MSB(TOUCH_LOGICAL_MAX_Y), 0x81, 0x02, 0x05, 0x0D, 0xC0,

    0x09, 0x54,  //   USAGE (Contact Count)
    0x25, TOUCH_MAX_CONTACTS,  //   LOGICAL_MAXIMUM (5)
    0x75, 0x08,  //   REPORT_SIZE (8)
    0x95, 0x01,  //   REPORT_COUNT (1)
    0x81, 0x02,  //   INPUT (Data,Var,Abs)
    0xC0         // END_COLLECTION
  };
  reportLength = sizeof(reportDescriptor);
  return reportDescriptor;
}

#define DEFAULT_CONFIGURATION (1)
#define TOTAL_DESCRIPTOR_LENGTH ((1 * CONFIGURATION_DESCRIPTOR_LENGTH) + (1 * INTERFACE_DESCRIPTOR_LENGTH) + (1 * HID_DESCRIPTOR_LENGTH) + (2 * ENDPOINT_DESCRIPTOR_LENGTH))

const uint8_t *CompositeHIDDevice::configuration_desc(uint8_t index) {
  if (index != 0) {
    return NULL;
  }
  uint8_t configuration_descriptor_temp[] = {
    CONFIGURATION_DESCRIPTOR_LENGTH,  // bLength
    CONFIGURATION_DESCRIPTOR,         // bDescriptorType
    LSB(TOTAL_DESCRIPTOR_LENGTH),     // wTotalLength (LSB)
    MSB(TOTAL_DESCRIPTOR_LENGTH),     // wTotalLength (MSB)
    0x01,                             // bNumInterfaces
    DEFAULT_CONFIGURATION,            // bConfigurationValue
    0x00,                             // iConfiguration
    C_RESERVED | C_SELF_POWERED,      // bmAttributes
    C_POWER(0),                       // bMaxPower

    INTERFACE_DESCRIPTOR_LENGTH,  // bLength
    INTERFACE_DESCRIPTOR,         // bDescriptorType
    0x00,                         // bInterfaceNumber
    0x00,                         // bAlternateSetting
    0x02,                         // bNumEndpoints
    HID_CLASS,                    // bInterfaceClass
    HID_SUBCLASS_NONE,            // bInterfaceSubClass
    HID_PROTOCOL_NONE,            // bInterfaceProtocol
    0x00,                         // iInterface

    HID_DESCRIPTOR_LENGTH,                 // bLength
    HID_DESCRIPTOR,                        // bDescriptorType
    LSB(HID_VERSION_1_11),                 // bcdHID (LSB)
    MSB(HID_VERSION_1_11),                 // bcdHID (MSB)
    0x00,                                  // bCountryCode
    0x01,                                  // bNumDescriptors
    REPORT_DESCRIPTOR,                     // bDescriptorType
    (uint8_t)(LSB(report_desc_length())),  // wDescriptorLength (LSB)
    (uint8_t)(MSB(report_desc_length())),  // wDescriptorLength (MSB)

    ENDPOINT_DESCRIPTOR_LENGTH,  // bLength
    ENDPOINT_DESCRIPTOR,         // bDescriptorType
    _int_in,                     // bEndpointAddress
    E_INTERRUPT,                 // bmAttributes
    LSB(MAX_HID_REPORT_SIZE),    // wMaxPacketSize (LSB)
    MSB(MAX_HID_REPORT_SIZE),    // wMaxPacketSize (MSB)
    1,                           // bInterval (milliseconds)

    ENDPOINT_DESCRIPTOR_LENGTH,  // bLength
    ENDPOINT_DESCRIPTOR,         // bDescriptorType
    _int_out,                    // bEndpointAddress
    E_INTERRUPT,                 // bmAttributes
    LSB(MAX_HID_REPORT_SIZE),    // wMaxPacketSize (LSB)
    MSB(MAX_HID_REPORT_SIZE),    // wMaxPacketSize (MSB)
    1,                           // bInterval (milliseconds)
  };
  MBED_ASSERT(sizeof(configuration_descriptor_temp) == sizeof(_configuration_descriptor));
  memcpy(_configuration_descriptor, configuration_descriptor_temp, sizeof(_configuration_descriptor));
  return _configuration_descriptor;
}
