# 关于使用esp-iot-solution中的LVGL例程更改为SSD1306 SPI程序的过程

## 1、创建新项目、添加组件

` F1 ESP-DIF New Project`

### esp-iot-solution组件 

可以这个时候就添加组件，但是别直接把esp-iot-solution文件夹选中，应该选择的是esp-iot-solution中的components里面的组件。选择之后就会自动把刚刚选中的文件夹复制到项目中的components文件夹里。这样不需要在根目录的CMakeLists.txt中添加了，他会自动检测components中的组件里的CMakeLists.txt和Kconfig文件

> Kconfig文件是在使用小齿轮图标时添加关于组件的选项用的

如果后面添加组件，建议直接添加esp-iot-solution中的所有组件。[官方教程](https://docs.espressif.com/projects/espressif-esp-iot-solution/zh_CN/latest/gettingstarted.html#id8) 。因为有些组件可能会依赖于其他组件。

在使用` include($ENV{IOT_SOLUTION_PATH}/component.cmake) `要注意`component.cmake`中的内容也是使用的`$ENV{IOT_SOLUTION_PATH}`，所以如果需要更改其中的代码，别搞错路径！要不然编译的不是你修改的文件。

> 所以在这个项目中，我把esp-iot-solution拷贝到了components中，我想使用的是项目中components拷贝的Iot组件，所以根目录中的CMakeLists.txt文件里添加`include(${CMAKE_CURRENT_LIST_DIR}/components/esp-iot-solution/component.cmake)`，在component.cmake中的路径也需要更改为`${CMAKE_CURRENT_LIST_DIR}/components`

### lv_examples组件

这个组件就是一些lvgl的示例程序，方便直接调用。

## 2、移植

### 配置

首先打开小齿轮启动SSD1306的驱动就是Select Screen Controller，后面的格式全部选择UNSCII 8，选择单色主题。

还有一点：我在CMakeLists.txt中添加了`CONFIG_LVGL_TFT_DISPLAY_MONOCHROME`这个宏定义，因为没在小齿轮中找到，这个宏定义是定义色彩深度的：

```c
#if defined CONFIG_LVGL_TFT_DISPLAY_MONOCHROME
/* For the monochrome display driver controller, e.g. SSD1306 and SH1107, use a color depth of 1. */
#define LV_COLOR_DEPTH     1
#else
#define LV_COLOR_DEPTH     16
#endif
```

### 初始化

app_main()配置好基本的SSD1306 SPI的初始化程序和清屏。

> swap_data 设置为 false

### lvgl_init

然后调用`lvgl_init(scr_driver_t **lcd_drv*, touch_panel_driver_t **touch_drv*)`,这个应该是乐鑫自己写的一个lvgl初始化的程序，里面包含了`lv_init()`，咱没有触摸所以第二个参数就是NULL。`lvgl_init(&g_lcd, NULL);`

### 添加两个回调函数

转到`gui_task()`中的`lvgl_display_init(lvgl_driver->lcd_drv)`，因为使用的是**单色模式**，需要把下面的回调函数添加进去

```c
disp_drv.rounder_cb = ex_disp_rounder;
disp_drv.set_px_cb = ex_disp_set_px;
```

```c
#define BIT_SET(a,b) ((a) |= (1U<<(b)))
#define BIT_CLEAR(a,b) ((a) &= ~(1U<<(b)))

void ex_disp_rounder(lv_disp_drv_t * disp_drv, lv_area_t *area)
{
    uint8_t hor_max = disp_drv->hor_res;
    uint8_t ver_max = disp_drv->ver_res;

    area->x1 = 0;
    area->y1 = 0;
    area->x2 = hor_max - 1;
    area->y2 = ver_max - 1;
}

void ex_disp_set_px(lv_disp_drv_t * disp_drv, uint8_t * buf, lv_coord_t buf_w, lv_coord_t x, lv_coord_t y,
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
```

而且不能使用两个数组，就是把buf2的地方改为NULL，这里只需要改变宏定义就行`#define BUFFER_NUMBER (1)`

### 改成8的倍数

然后进入到`ex_disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)`回调函数中，我是改成了如下代码：

```c
/*Write the internal buffer (VDB) to the display. 'lv_flush_ready()' has to be called when finished*/
static void ex_disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    /* For SSD1306 Divide by 8 根据LV_PORT_ESP32 更改  变成8的倍数 */
    uint8_t row1 = area->y1 >> 3;
    uint8_t row2 = area->y2 >> 3;
    row1 = row1 << 3;
    row2 = (row2 << 3) - 1;	// 不减1的话后面会因为不是8的倍数报错，这里也不确定到底对不对，反正目前能用
    lcd_obj.draw_bitmap(area->x1, row1, (uint16_t)(area->x2 - area->x1 + 1), (uint16_t)(row2 - row1 + 1), (uint16_t *)color_map);
    
    /* IMPORTANT!!!
     * Inform the graphics library that you are ready with the flushing*/
    lv_disp_flush_ready(drv);
}
```

### 修改缓冲大小

在`lvgl_adapter.c`中`DISP_BUF_SIZE`的宏定义改为:

```c
#define DISP_BUF_SIZE  (g_screen_width * (g_screen_height / 8))     // 参考了LV_PORT_ESP32的I2C代码
```

应该是因为在SSD1306中一个像素点其实1bit，而且每次只能对一列的8个像素点进行操作，所以相当于实际上只有64/8行。（而且发现lvgl中的颜色有些是16bit的，在发送的时候按照8bit的读取来发）

还需要在`lv_disp_buf_init(&disp_buf, buf1, NULL, alloc_pixel)`的上面添加一行，这个不太完全理解，lvgl_port_esp32上的注释是*Actual size in pixels*

```c
alloc_pixel *= 8;
```

### 修改一个小bug

然后进入到SSD1306.c文件中，在`components\esp-iot-solution\components\display\screen\controller_driver\ssd1306.c`在239行有个bug，减1去掉：

```c
ret |= LCD_WRITE_CMD(x1);   // 这里不应该减1
```



不保证没漏写，因为这个移植过程也研究好几天，每天改一点点，也可能有地方忘记写了，以代码为准。



