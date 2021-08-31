/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#include "screen_driver.h"
#include "spi_bus.h"
#include "esp_log.h"
#include "ssd1306.h"

#include "lvgl_gui.h"
#include "lv_examples/src/lv_demo_printer/lv_demo_printer.h"
#include "lv_examples/src/lv_demo_widgets/lv_demo_widgets.h"
#include "lv_examples/src/lv_ex_get_started/lv_ex_get_started.h"
#include "lv_examples/src/lv_demo_benchmark/lv_demo_benchmark.h"
#include "lv_examples/src/lv_demo_keypad_encoder/lv_demo_keypad_encoder.h"
#include "lv_examples/src/lv_demo_stress/lv_demo_stress.h"
#include "lv_examples/src/lv_ex_style/lv_ex_style.h"

static const char *TAG = "NO1";


static void screen_clear(scr_driver_t *lcd, int color)
{
    scr_info_t lcd_info;
    lcd->get_info(&lcd_info);
    uint16_t *buffer = malloc(lcd_info.width * sizeof(uint16_t));
    if (NULL == buffer) {
        for (size_t y = 0; y < lcd_info.height; y++) {
            for (size_t x = 0; x < lcd_info.width; x++) {
                
                lcd->draw_pixel(x, y, color);
            }
        }
    } else {
        for (size_t i = 0; i < lcd_info.width; i++) {
            buffer[i] = color;
        }

        for (int y = 0; y < lcd_info.height / 8; y++) {
            lcd->draw_bitmap(0, y*8, lcd_info.width, 8, buffer);
        }
        
        free(buffer);
    }
}

void app_main(void)
{
    scr_driver_t g_lcd; // A screen driver
    esp_err_t ret = ESP_OK;

    spi_bus_handle_t bus_handle = NULL;
    spi_config_t bus_conf = {
        .miso_io_num = 27,
        .mosi_io_num = 21,
        .sclk_io_num = 22,
    }; // spi_bus configurations
    bus_handle = spi_bus_create(SPI2_HOST, &bus_conf);
    scr_interface_spi_config_t spi_ssd1306_cfg = {
        .spi_bus = bus_handle,    /*!< Handle of spi bus */
        .pin_num_cs = 5,           /*!< SPI Chip Select Pin*/
        .pin_num_dc = 19,           /*!< Pin to select Data or Command for LCD */
        .clk_freq = 20*1000*1000,                /*!< SPI clock frequency */
        .swap_data = false,             /*!< Whether to swap data */
    };

    scr_interface_driver_t *iface_drv;
    scr_interface_create(SCREEN_IFACE_SPI, &spi_ssd1306_cfg, &iface_drv);

    /** Find screen driver for SSD1306 */
    ret = scr_find_driver(SCREEN_CONTROLLER_SSD1306, &g_lcd);
    if (ESP_OK != ret) {
        return;
        ESP_LOGE(TAG, "screen find failed");
    }

    /** Configure screen controller */
    scr_controller_config_t lcd_cfg = {
        .interface_drv = iface_drv,
        .pin_num_rst = 18,      // The reset pin is not connected
        .pin_num_bckl = -1,     // The backlight pin is not connected
        .rst_active_level = 0,
        .bckl_active_level = 1,
        .offset_hor = 0,
        .offset_ver = 0,
        .width = 128,
        .height = 64,
        .rotate = SCR_DIR_RLBT,
    };

    /** Initialize SSD1306 screen */
    g_lcd.init(&lcd_cfg);
    screen_clear(&g_lcd, COLOR_BLACK);

    lvgl_init(&g_lcd, NULL);    /* Initialize LittlevGL */

    lv_obj_t * scr = lv_disp_get_scr_act(NULL);

    // /*Create a Label on the currently active screen*/
    lv_obj_t * label1 =  lv_label_create(scr, NULL);

    // /*Modify the Label's text*/
    lv_label_set_text(label1, "ssd1306\nSPI MONO");

    /* Align the Label to the center
     * NULL means align on parent (which is the screen now)
     * 0, 0 at the end means an x, y offset after alignment*/
    lv_obj_align(label1, NULL, LV_ALIGN_CENTER, 0, 0);
}
