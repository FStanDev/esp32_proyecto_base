#include <LovyanGFX.hpp>
#include <class/hid/hid_device.h>
#include <cstdint>
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <tinyusb.h>
// Definition of hid
//
//
#define APP_BUTTON (GPIO_NUM_0) // Use BOOT signal by default
static const char *TAG = "example";

/************* TinyUSB descriptors ****************/

#define TUSB_DESC_TOTAL_LEN                                                    \
  (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

/**
 * @brief HID report descriptor
 *
 * In this example we implement Keyboard + Mouse HID device,
 * so we must define both report descriptors
 */
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE))};

/**
 * @brief String descriptor
 */
const char *hid_string_descriptor[5] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},    // 0: is supported language is English (0x0409)
    "TinyUSB",               // 1: Manufacturer
    "TinyUSB Device",        // 2: Product
    "123456",                // 3: Serials, should use chip ID
    "Example HID interface", // 4: HID
};

/**
 * @brief Configuration descriptor
 *
 * This is a simple configuration descriptor that defines 1 configuration and 1
 * HID interface
 */
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length,
    // attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP
    // In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16,
                       10),
};

/********* TinyUSB HID callbacks ***************/

// Invoked when received GET HID REPORT DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long
// enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
  // We use only one interface and one HID report descriptor, so we can ignore
  // parameter 'instance'
  return hid_report_descriptor;
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;

  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {}

/********* Application ***************/

typedef enum {
  MOUSE_DIR_RIGHT,
  MOUSE_DIR_DOWN,
  MOUSE_DIR_LEFT,
  MOUSE_DIR_UP,
  MOUSE_DIR_MAX,
} mouse_dir_t;

#define DISTANCE_MAX 125
#define DELTA_SCALAR 5

static void mouse_draw_square_next_delta(int8_t *delta_x_ret,
                                         int8_t *delta_y_ret) {
  mouse_dir_t cur_dir = MOUSE_DIR_RIGHT;
  uint32_t distance = 0;

  // Calculate next delta
  if (cur_dir == MOUSE_DIR_RIGHT) {
    *delta_x_ret = DELTA_SCALAR;
    *delta_y_ret = 0;
  } else if (cur_dir == MOUSE_DIR_DOWN) {
    *delta_x_ret = 0;
    *delta_y_ret = DELTA_SCALAR;
  } else if (cur_dir == MOUSE_DIR_LEFT) {
    *delta_x_ret = -DELTA_SCALAR;
    *delta_y_ret = 0;
  } else if (cur_dir == MOUSE_DIR_UP) {
    *delta_x_ret = 0;
    *delta_y_ret = -DELTA_SCALAR;
  }

  // Update cumulative distance for current direction
  distance += DELTA_SCALAR;
  // Check if we need to change direction
  if (distance >= DISTANCE_MAX) {
    distance = 0;
    cur_dir = static_cast<mouse_dir_t>(static_cast<int>(cur_dir) + 1);
    ;
    if (cur_dir == MOUSE_DIR_MAX) {
      cur_dir = static_cast<mouse_dir_t>(0);
    }
  }
}

static void app_send_hid_demo(void) {
  // Keyboard output: Send key 'a/A' pressed and released
  ESP_LOGI(TAG, "Sending Keyboard report");
  uint8_t keycode[6] = {HID_KEY_C};
  tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycode);
  vTaskDelay(pdMS_TO_TICKS(50));
  uint8_t keycodeA[6] = {HID_KEY_A};
  tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycodeA);
  vTaskDelay(pdMS_TO_TICKS(50));

  tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycode);
  vTaskDelay(pdMS_TO_TICKS(50));
  tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycodeA);
  vTaskDelay(pdMS_TO_TICKS(50));

  tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);

  // Mouse output: Move mouse cursor in square trajectory
  ESP_LOGI(TAG, "Sending Mouse report");
  int8_t delta_x;
  int8_t delta_y;
  for (int i = 0; i < (DISTANCE_MAX / DELTA_SCALAR) * 4; i++) {
    // Get the next x and y delta in the draw square pattern
    mouse_draw_square_next_delta(&delta_x, &delta_y);
    tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x00, delta_x, delta_y, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

#define LGFX_USE_V1

// Display (ST7735s) hardware configuration:
#define DISPLAY_RST 1
#define DISPLAY_DC 2
#define DISPLAY_MOSI 3
#define DISPLAY_CS 4
#define DISPLAY_SCLK 5
#define DISPLAY_LEDA 38
#define DISPLAY_MISO -1
#define DISPLAY_BUSY -1
#define DISPLAY_WIDTH 160
#define DISPLAY_HEIGHT 80

class LGFX_LiLyGo_TDongleS3 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7735S _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Light_PWM _light_instance;

public:
  LGFX_LiLyGo_TDongleS3(void) {
    {
      auto cfg = _bus_instance.config();

      cfg.spi_host = SPI3_HOST;  // SPI2_HOST is in use by the RGB led
      cfg.spi_mode = 0;          // Set SPI communication mode (0 ~ 3)
      cfg.freq_write = 27000000; // SPI clock when sending (max 80MHz, rounded
                                 // to 80MHz divided by an integer)
      cfg.freq_read = 16000000;  // SPI clock when receiving
      cfg.spi_3wire = true;      // Set true when receiving on the MOSI pin
      cfg.use_lock = false;      // Set true when using transaction lock
      cfg.dma_channel =
          SPI_DMA_CH_AUTO; // Set the DMA channel to use (0=not use DMA / 1=1ch
                           // / 2=ch / SPI_DMA_CH_AUTO=auto setting)

      cfg.pin_sclk = DISPLAY_SCLK; // set SPI SCLK pin number
      cfg.pin_mosi = DISPLAY_MOSI; // Set MOSI pin number for SPI
      cfg.pin_miso = DISPLAY_MISO; // Set MISO pin for SPI (-1 = disable)
      cfg.pin_dc = DISPLAY_DC;     // Set SPI D/C pin number (-1 = disable)
      _bus_instance.config(cfg);   // Apply the setting value to the bus.
      _panel_instance.setBus(&_bus_instance); // Sets the bus to the panel.
    }

    {
      auto cfg =
          _panel_instance
              .config(); // Obtain the structure for display panel settings.

      cfg.pin_cs =
          DISPLAY_CS; // Pin number to which CS is connected (-1 = disable)
      cfg.pin_rst =
          DISPLAY_RST; // pin number where RST is connected (-1 = disable)
      cfg.pin_busy =
          DISPLAY_BUSY; // pin number to which BUSY is connected (-1 = disable)

      cfg.panel_width =
          DISPLAY_HEIGHT; // actual displayable width. Note: width/height
                          // swapped due to the rotation
      cfg.panel_height =
          DISPLAY_WIDTH; // Actual displayable height Note: width/height swapped
                         // due to the rotation
      cfg.offset_x = 26; // Panel offset in X direction
      cfg.offset_y = 1;  // Y direction offset amount of the panel
      cfg.offset_rotation =
          1; // Rotation direction value offset 0~7 (4~7 are upside down)
      cfg.dummy_read_pixel =
          8; // Number of bits for dummy read before pixel read
      cfg.dummy_read_bits =
          1; // Number of dummy read bits before non-pixel data read
      cfg.readable = true; // set to true if data can be read
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit =
          false; // Set to true for panels that transmit data length in 16-bit
                 // units with 16-bit parallel or SPI
      cfg.bus_shared = true; // If the bus is shared with the SD card, set to
                             // true (bus control with drawJpgFile etc.)

      // Please set the following only when the display is shifted with a driver
      // with a variable number of pixels such as ST7735 or ILI9163.
      cfg.memory_width = 132;  // Maximum width supported by driver IC
      cfg.memory_height = 160; // Maximum height supported by driver IC

      _panel_instance.config(cfg);
    }

    {
      auto cfg = _light_instance.config();

      cfg.pin_bl =
          DISPLAY_LEDA;    // pin number to which the backlight is connected
      cfg.invert = true;   // true to invert backlight brightness
      cfg.freq = 12000;    // Backlight PWM frequency
      cfg.pwm_channel = 7; // PWM channel number to use

      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    setPanel(&_panel_instance);
  }
};

static LGFX_LiLyGo_TDongleS3 lcd;

extern "C" void app_main(void) {
  vTaskDelay(10);
  printf("Hello, ESP32-S3!\n");
  if (!lcd.init()) {
    printf("No Hello, ESP32-S3!\n");
    return;
  }

  lcd.setBrightness(128);

  lcd.clear(0x000080u);
  lcd.setColor(0xff0000u);
  lcd.drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  lcd.setCursor(2, 2);
  lcd.setTextColor(0xffffffu, 0x000080u);
  lcd.println("Loaded LovyanGFX");
  lcd.setCursor(10, 10);
  lcd.setTextColor(0x00ff80u, 0x000080u);
  lcd.println("Green");
  printf("La Pantalla deberia brillar");
  lcd.setCursor(2, 12);
  lcd.setTextColor(0x000000u, 0x000080u);
  lcd.println("Hola Mundo");
  lcd.println("Suscribete a Stantech");
  uint8_t size = sizeof(hid_report_descriptor);
  lcd.println(size);

  ESP_LOGI(TAG, "USB initialization");
  const tinyusb_config_t tusb_cfg = {
      .device_descriptor = NULL,
      .string_descriptor = hid_string_descriptor,
      .string_descriptor_count =
          sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
      .external_phy = false,
      .configuration_descriptor = hid_configuration_descriptor,
  };

  ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
  ESP_LOGI(TAG, "USB initialization DONE");
  while (1) {
    if (tud_mounted()) {
      static bool send_hid_data = true;
      if (send_hid_data) {
        app_send_hid_demo();
      }
      send_hid_data = !gpio_get_level(APP_BUTTON);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
