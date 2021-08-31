// Copyright 2015-2020 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "lvgl_adapter.h"
#include "sdkconfig.h"

static const char *TAG = "lvgl adapter";

static scr_driver_t lcd_obj;
static touch_panel_driver_t touch_obj;

static uint16_t g_screen_width = 240;
static uint16_t g_screen_height = 320;

/*Write the internal buffer (VDB) to the display. 'lv_flush_ready()' has to be called when finished*/
static void ex_disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    /* For SSD1306 Divide by 8 根据LV_PORT_ESP32 更改 变成8的倍数*/
    uint8_t row1 = area->y1 >> 3;
    uint8_t row2 = (area->y2 >> 3) + 1;
    row1 = row1 << 3;
    row2 = (row2 << 3) - 1;     // 不减1的话后面会因为不是8的倍数报错，这里也不确定到底对不对，反正目前能用
    lcd_obj.draw_bitmap(area->x1, row1, (uint16_t)(area->x2 - area->x1 + 1), (uint16_t)(row2 - row1 + 1), (uint16_t *)color_map);
    
    /* IMPORTANT!!!
     * Inform the graphics library that you are ready with the flushing*/
    lv_disp_flush_ready(drv);
}

// #define DISP_BUF_SIZE  (g_screen_width * 64)
/* When using the mono theme, the display pixels can be represented in one bit,
 * so the buffer size can be divided by 8, e.g. see SSD1306 display size */
#define DISP_BUF_SIZE  (g_screen_width * (g_screen_height / 8))     // 参考了LV_PORT_ESP32的I2C代码
#define SIZE_TO_PIXEL(v) ((v) / sizeof(lv_color_t))
#define PIXEL_TO_SIZE(v) ((v) * sizeof(lv_color_t))
#define BUFFER_NUMBER (1)

#define BIT_SET(a,b) ((a) |= (1U<<(b)))
#define BIT_CLEAR(a,b) ((a) &= ~(1U<<(b)))

static void ex_disp_rounder(lv_disp_drv_t * disp_drv, lv_area_t *area)
{
    uint8_t hor_max = disp_drv->hor_res;
    uint8_t ver_max = disp_drv->ver_res;

    area->x1 = 0;
    area->y1 = 0;
    area->x2 = hor_max - 1;
    area->y2 = ver_max - 1;
}

static void ex_disp_set_px(lv_disp_drv_t * disp_drv, uint8_t * buf, lv_coord_t buf_w, lv_coord_t x, lv_coord_t y,
        lv_color_t color, lv_opa_t opa)
{
    uint16_t byte_index = x + (( y>>3 ) * buf_w);
    uint8_t  bit_index  = y & 0x7;

    if ((color.full == 0) && (LV_OPA_TRANSP != opa)) {
        BIT_SET(buf[byte_index], bit_index);
    } else {
        BIT_CLEAR(buf[byte_index], bit_index);
    }
}

esp_err_t lvgl_display_init(scr_driver_t *driver)
{
    if (NULL == driver) {
        ESP_LOGE(TAG, "Pointer of lcd driver is invalid");
        return ESP_ERR_INVALID_ARG;
    }
    lcd_obj = *driver;      // 传进来的参数是显示屏对象 保存到全局变量
    scr_info_t info;
    lcd_obj.get_info(&info);
    g_screen_width = info.width;    // 宽度保存到全局变量
    g_screen_height = info.height;  // 高度保存到全局变量

    lv_disp_drv_t disp_drv;      /*Descriptor of a display driver*/
    lv_disp_drv_init(&disp_drv); /*Basic initialization*/
    disp_drv.hor_res = g_screen_width;
    disp_drv.ver_res = g_screen_height;

    disp_drv.flush_cb = ex_disp_flush; /*Used in buffered mode (LV_VDB_SIZE != 0  in lv_conf.h)*/
#if defined CONFIG_LVGL_TFT_DISPLAY_MONOCHROME  // 在CMakeLists.txt中定义了
    disp_drv.rounder_cb = ex_disp_rounder;
    disp_drv.set_px_cb = ex_disp_set_px;
#endif
    size_t free_size = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t remain_size = 60 * 1024; /**< Remain for other functions */
    size_t alloc_pixel = DISP_BUF_SIZE;
    if (((BUFFER_NUMBER * PIXEL_TO_SIZE(alloc_pixel)) + remain_size) > free_size) {
        size_t allow_size = (free_size - remain_size) & 0xfffffffc;
        alloc_pixel = SIZE_TO_PIXEL(allow_size / BUFFER_NUMBER);
        ESP_LOGW(TAG, "Exceeded max free size, force shrink to %u Byte", allow_size);
    }

    lv_color_t *buf1 = heap_caps_malloc(PIXEL_TO_SIZE(alloc_pixel), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (NULL == buf1) {
        ESP_LOGE(TAG, "Display buffer memory not enough");
        return ESP_ERR_NO_MEM;
    }
#if (BUFFER_NUMBER == 2)
    lv_color_t *buf2 = heap_caps_malloc(PIXEL_TO_SIZE(alloc_pixel), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (NULL == buf2) {
        heap_caps_free(buf1);
        ESP_LOGE(TAG, "Display buffer memory not enough");
        return ESP_ERR_NO_MEM;
    }
#endif

    ESP_LOGI(TAG, "Alloc memory total size: %u Byte", BUFFER_NUMBER * PIXEL_TO_SIZE(alloc_pixel));

    static lv_disp_buf_t disp_buf;

    /* Actual size in pixels */
    alloc_pixel *= 8;
#if (BUFFER_NUMBER == 2)
    lv_disp_buf_init(&disp_buf, buf1, buf2, alloc_pixel);
#else
    lv_disp_buf_init(&disp_buf, buf1, NULL, alloc_pixel);
#endif

    disp_drv.buffer = &disp_buf;

    /* Finally register the driver */
    lv_disp_drv_register(&disp_drv);
    return ESP_OK;
}

/*Function pointer to read data. Return 'true' if there is still data to be read (buffered)*/
static bool ex_tp_read(struct _lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    data->state = LV_INDEV_STATE_REL;
    touch_panel_points_t points;
    touch_obj.read_point_data(&points);
    // please be sure that your touch driver every time return old (last clcked) value.
    if (TOUCH_EVT_PRESS == points.event) {
        int32_t x = points.curx[0];
        int32_t y = points.cury[0];
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
    }
    return false;
}

/* Input device interface，Initialize your touchpad */
esp_err_t lvgl_indev_init(touch_panel_driver_t *driver)
{
    if (NULL == driver) {
        ESP_LOGE(TAG, "Pointer of touch driver is invalid");
        return ESP_ERR_INVALID_ARG;
    }

    touch_obj = *driver;

    lv_indev_drv_t indev_drv;               /*Descriptor of an input device driver*/
    lv_indev_drv_init(&indev_drv);          /*Basic initialization*/

    indev_drv.type = LV_INDEV_TYPE_POINTER; /*The touchpad is pointer type device*/
    indev_drv.read_cb = ex_tp_read;            /*Library ready your touchpad via this function*/

    lv_indev_drv_register(&indev_drv);      /*Finally register the driver*/
    return ESP_OK;
}
