# ESP32-S3 WiFi 网络音乐播放器 —— 从零开始完整指南

> 本文假设你从未接触过嵌入式开发。只需要跟着一步步做，就能做出一台能连 WiFi 播放网络音乐的小播放器。

---

## 目录

1. [你需要准备什么](#1-你需要准备什么)
2. [安装开发环境](#2-安装开发环境)
3. [创建项目](#3-创建项目)
4. [编写代码](#4-编写代码)
5. [接线](#5-接线)
6. [编译与烧录](#6-编译与烧录)
7. [测试与使用](#7-测试与使用)

---

## 1. 你需要准备什么

### 硬件清单

| 物品 | 数量 | 大约价格 | 备注 |
|------|------|---------|------|
| ESP32-S3 开发板 | 1 | ¥25-40 | 推荐 N16R8 版本（16MB Flash） |
| MAX98357 I2S 功放模块 | 1 | ¥5-8 | 3W 小喇叭用的数字功放 |
| 小喇叭（3W / 4Ω 或 8Ω） | 1 | ¥3-5 | 或者接普通耳机座 |
| SSD1306 128x64 OLED 屏幕 | 1 | ¥8-12 | 必须是 I2C 接口（4 个引脚那种） |
| 两脚微动按键 | 2 | ¥0.5 | 就是普通的小按键 |
| 杜邦线（公对母） | 20 根 | ¥3 | 用来连线 |
| 面包板 | 1 | ¥5 | 方便接线，不用焊接 |
| USB-C 数据线 | 1 | ¥5 | 连接电脑和开发板（要能传数据） |

全部加起来约 **¥60-80**。

### 软件清单（全部免费）

| 软件 | 用途 |
|------|------|
| VS Code | 写代码 |
| ESP-IDF 5.5 | ESP32 的 SDK（软件开发工具包） |
| Git | ESP-IDF 安装器需要 |
| Python 3 | ESP-IDF 需要 |

---

## 2. 安装开发环境

### 2.1 安装 ESP-IDF

ESP-IDF 是 Espressif 公司提供的 ESP32 官方开发框架。

**Windows 用户（推荐离线安装器）：**

1. 打开浏览器，搜索 "ESP-IDF Windows Installer"
2. 进入 Espressif 官网下载页，选 **ESP-IDF v5.5.4** 的离线安装器（约 300MB）
3. 运行安装器，一路点 Next，用默认选项即可
4. 安装路径建议保持默认 `D:\Espressif`（或 `C:\Espressif`）
5. 安装完成后，桌面上会出现 **"ESP-IDF 5.5 PowerShell"** 快捷方式

> **验证安装：** 双击打开 "ESP-IDF 5.5 PowerShell"，输入：
> ```
> idf.py --version
> ```
> 如果显示版本号，说明装好了。

**macOS / Linux 用户：**

参考官方文档：https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/index.html

### 2.2 安装 VS Code 扩展

1. 打开 VS Code
2. 点击左侧扩展图标（或按 `Ctrl+Shift+X`）
3. 搜索 "Espressif IDF" 并安装
4. 安装后按 `Ctrl+Shift+P`，输入 "ESP-IDF: Configure ESP-IDF Extension"
5. 选择 "Use Existing Setup"，指向你的 ESP-IDF 安装目录

### 2.3 安装 USB 驱动

大多数 ESP32-S3 开发板自带 USB 转串口芯片（CP2102 或 CH340）。

1. 搜索 "CP2102 driver" 和 "CH340 driver"，两个都下载安装
2. 安装后用 USB 线把开发板连到电脑
3. 打开"设备管理器" → "端口(COM 和 LPT)"
4. 应该能看到类似 "Silicon Labs CP210x USB to UART Bridge (COM7)" 这样的设备
5. **记住 COM 端口号**（这里是 COM7），后面烧录要用

---

## 3. 创建项目

### 3.1 打开 ESP-IDF 终端

双击桌面上的 "ESP-IDF 5.5 PowerShell"，会打开一个命令行窗口。

### 3.2 进入你的工作目录

```bash
# 假设你的项目放在桌面
cd C:\Users\你的用户名\Desktop
mkdir esp32
cd esp32
```

### 3.3 创建项目骨架

```bash
# 用 ESP-IDF 自带模板创建项目
idf.py create-project it_is_my_time
cd it_is_my_time

# 设置芯片型号（这一步很重要！）
idf.py set-target esp32s3
```

运行完后，你会看到自动生成了这些文件：
```
it_is_my_time/
├── main/
│   ├── CMakeLists.txt
│   └── main.c
├── CMakeLists.txt
└── sdkconfig
```

### 3.4 创建组件目录

组件（Component）是 ESP-IDF 组织代码的方式。每个功能模块一个文件夹：

```bash
# 创建音频播放器组件
mkdir -p components/audio_player/include
# 创建屏幕驱动组件
mkdir -p components/display/include
```

现在的目录结构：
```
it_is_my_time/
├── main/
│   ├── CMakeLists.txt
│   └── main.c
├── components/
│   ├── audio_player/
│   │   └── include/
│   └── display/
│       └── include/
├── CMakeLists.txt
└── sdkconfig
```

---

## 4. 编写代码

> 下面按顺序创建每个文件。**先创建文件，再复制代码进去。**

### 4.1 项目级 CMakeLists.txt

这个文件告诉 ESP-IDF："这是一个 ESP32 项目"。

**文件路径：** `it_is_my_time/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS components/Middlewares)
add_compile_options(-fdiagnostics-color=always)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(00_basic)
```

**解释：**
- `cmake_minimum_required` — CMake 最低版本
- `include(...project.cmake)` — **固定写法**，引入 ESP-IDF 的构建系统
- `project(00_basic)` — 项目名称（可以改成你想叫的名字）

---

### 4.2 屏幕驱动组件

屏幕驱动是最底层、最独立的模块，我们先写它。

#### 4.2.1 组件编译配置

**文件路径：** `components/display/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "display.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver
)
```

**解释：**
- `SRCS` — 这个组件包含 `display.c` 这一个源文件
- `INCLUDE_DIRS` — 头文件在 `include` 目录下
- `PRIV_REQUIRES driver` — 要用到 ESP-IDF 的驱动层（I2C 在里面）

#### 4.2.2 头文件（公开接口）

**文件路径：** `components/display/include/display.h`

```c
#pragma once

#include <stdint.h>

void display_init(void);
void display_clear(void);

// line: 0-7（共 8 行），每行最多 16 个英文字符（8x8 字体）
void display_set_line(int line, const char *text);
```

**解释：**
- `#pragma once` — 防止这个文件被重复包含
- 声明了 3 个函数，其他组件可以用它们来控制屏幕
- `const char *text` — C 语言里字符串的写法

#### 4.2.3 驱动实现（核心代码）

**文件路径：** `components/display/display.c`

这个文件比较长（约 280 行），但不用全部理解，复制过去就行。我分段解释关键部分。

---

**第 1 段：头文件包含和常量定义**

```c
#include "display.h"

#include <string.h>
#include <stdio.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define I2C_SCL_GPIO  1     // 屏幕的 SCL 接 GPIO 1
#define I2C_SDA_GPIO  2     // 屏幕的 SDA 接 GPIO 2
#define SSD1306_ADDR  0x3C  // SSD1306 的 I2C 地址（固定值）

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define SCREEN_PAGES  8     // 64÷8=8 页

static const char *TAG = "display";  // 日志标签，方便调试
static i2c_master_dev_handle_t s_dev; // I2C 设备句柄
static SemaphoreHandle_t s_mutex;     // 互斥锁，防止多任务同时操作屏幕
```

**解释：**
- `#include` — 引入别人写好的代码（库），尖括号 `<>` 表示系统库，双引号 `""` 表示自己写的
- `#define` — 宏定义，给数字起个名字，代码里写 `I2C_SCL_GPIO` 比写 `1` 更容易懂
- `static` — 这个变量/函数只在当前文件里用，外面看不到
- `SemaphoreHandle_t` — FreeRTOS 的互斥锁类型

---

**第 2 段：8×8 字体表**

```c
// ASCII 32-127 每个字符的 8x8 像素数据
static const uint8_t font8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 空格 (32)
    {0x00,0x00,0x5F,0x00,0x00,0x00,0x00,0x00}, // ! (33)
    {0x00,0x00,0x07,0x00,0x07,0x00,0x00,0x00}, // " (34)
    {0x00,0x14,0x7F,0x14,0x7F,0x14,0x00,0x00}, // # (35)
    {0x00,0x24,0x2A,0x7F,0x2A,0x12,0x00,0x00}, // $ (36)
    {0x00,0x23,0x13,0x08,0x64,0x62,0x00,0x00}, // % (37)
    {0x00,0x36,0x49,0x55,0x22,0x50,0x00,0x00}, // & (38)
    {0x00,0x00,0x05,0x03,0x00,0x00,0x00,0x00}, // ' (39)
    {0x00,0x1C,0x22,0x41,0x00,0x00,0x00,0x00}, // ( (40)
    {0x00,0x41,0x22,0x1C,0x00,0x00,0x00,0x00}, // ) (41)
    {0x00,0x08,0x2A,0x1C,0x2A,0x08,0x00,0x00}, // * (42)
    {0x00,0x08,0x08,0x3E,0x08,0x08,0x00,0x00}, // + (43)
    {0x00,0xA0,0x60,0x00,0x00,0x00,0x00,0x00}, // , (44)
    {0x00,0x08,0x08,0x08,0x08,0x08,0x00,0x00}, // - (45)
    {0x00,0x60,0x60,0x00,0x00,0x00,0x00,0x00}, // . (46)
    {0x00,0x20,0x10,0x08,0x04,0x02,0x00,0x00}, // / (47)
    {0x00,0x3E,0x51,0x49,0x45,0x3E,0x00,0x00}, // 0 (48)
    {0x00,0x00,0x42,0x7F,0x40,0x00,0x00,0x00}, // 1 (49)
    {0x00,0x62,0x51,0x49,0x49,0x46,0x00,0x00}, // 2 (50)
    {0x00,0x22,0x41,0x49,0x49,0x36,0x00,0x00}, // 3 (51)
    {0x00,0x18,0x14,0x12,0x7F,0x10,0x00,0x00}, // 4 (52)
    {0x00,0x27,0x45,0x45,0x45,0x39,0x00,0x00}, // 5 (53)
    {0x00,0x3C,0x4A,0x49,0x49,0x30,0x00,0x00}, // 6 (54)
    {0x00,0x01,0x71,0x09,0x05,0x03,0x00,0x00}, // 7 (55)
    {0x00,0x36,0x49,0x49,0x49,0x36,0x00,0x00}, // 8 (56)
    {0x00,0x06,0x49,0x49,0x29,0x1E,0x00,0x00}, // 9 (57)
    {0x00,0x00,0x36,0x36,0x00,0x00,0x00,0x00}, // : (58)
    {0x00,0x00,0xAC,0x6C,0x00,0x00,0x00,0x00}, // ; (59)
    {0x00,0x08,0x14,0x22,0x41,0x00,0x00,0x00}, // < (60)
    {0x00,0x14,0x14,0x14,0x14,0x14,0x00,0x00}, // = (61)
    {0x00,0x41,0x22,0x14,0x08,0x00,0x00,0x00}, // > (62)
    {0x00,0x02,0x01,0x51,0x09,0x06,0x00,0x00}, // ? (63)
    {0x00,0x32,0x49,0x79,0x41,0x3E,0x00,0x00}, // @ (64)
    {0x00,0x7E,0x09,0x09,0x09,0x7E,0x00,0x00}, // A (65)
    {0x00,0x7F,0x49,0x49,0x49,0x36,0x00,0x00}, // B (66)
    {0x00,0x3E,0x41,0x41,0x41,0x22,0x00,0x00}, // C (67)
    {0x00,0x7F,0x41,0x41,0x22,0x1C,0x00,0x00}, // D (68)
    {0x00,0x7F,0x49,0x49,0x49,0x41,0x00,0x00}, // E (69)
    {0x00,0x7F,0x09,0x09,0x09,0x01,0x00,0x00}, // F (70)
    {0x00,0x3E,0x41,0x41,0x51,0x72,0x00,0x00}, // G (71)
    {0x00,0x7F,0x08,0x08,0x08,0x7F,0x00,0x00}, // H (72)
    {0x00,0x41,0x7F,0x41,0x00,0x00,0x00,0x00}, // I (73)
    {0x00,0x20,0x40,0x41,0x3F,0x01,0x00,0x00}, // J (74)
    {0x00,0x7F,0x08,0x14,0x22,0x41,0x00,0x00}, // K (75)
    {0x00,0x7F,0x40,0x40,0x40,0x40,0x00,0x00}, // L (76)
    {0x00,0x7F,0x02,0x0C,0x02,0x7F,0x00,0x00}, // M (77)
    {0x00,0x7F,0x04,0x08,0x10,0x7F,0x00,0x00}, // N (78)
    {0x00,0x3E,0x41,0x41,0x41,0x3E,0x00,0x00}, // O (79)
    {0x00,0x7F,0x09,0x09,0x09,0x06,0x00,0x00}, // P (80)
    {0x00,0x3E,0x41,0x51,0x21,0x5E,0x00,0x00}, // Q (81)
    {0x00,0x7F,0x09,0x19,0x29,0x46,0x00,0x00}, // R (82)
    {0x00,0x26,0x49,0x49,0x49,0x32,0x00,0x00}, // S (83)
    {0x00,0x01,0x01,0x7F,0x01,0x01,0x00,0x00}, // T (84)
    {0x00,0x3F,0x40,0x40,0x40,0x3F,0x00,0x00}, // U (85)
    {0x00,0x1F,0x20,0x40,0x20,0x1F,0x00,0x00}, // V (86)
    {0x00,0x3F,0x40,0x38,0x40,0x3F,0x00,0x00}, // W (87)
    {0x00,0x63,0x14,0x08,0x14,0x63,0x00,0x00}, // X (88)
    {0x00,0x03,0x04,0x78,0x04,0x03,0x00,0x00}, // Y (89)
    {0x00,0x61,0x51,0x49,0x45,0x43,0x00,0x00}, // Z (90)
    {0x00,0x7F,0x41,0x41,0x00,0x00,0x00,0x00}, // [ (91)
    {0x00,0x02,0x04,0x08,0x10,0x20,0x00,0x00}, // \ (92)
    {0x00,0x41,0x41,0x7F,0x00,0x00,0x00,0x00}, // ] (93)
    {0x00,0x04,0x02,0x01,0x02,0x04,0x00,0x00}, // ^ (94)
    {0x00,0x80,0x80,0x80,0x80,0x80,0x00,0x00}, // _ (95)
    {0x00,0x01,0x02,0x04,0x00,0x00,0x00,0x00}, // ` (96)
    {0x00,0x20,0x54,0x54,0x54,0x78,0x00,0x00}, // a (97)
    {0x00,0x7F,0x48,0x44,0x44,0x38,0x00,0x00}, // b (98)
    {0x00,0x38,0x44,0x44,0x44,0x28,0x00,0x00}, // c (99)
    {0x00,0x38,0x44,0x44,0x48,0x7F,0x00,0x00}, // d (100)
    {0x00,0x38,0x54,0x54,0x54,0x18,0x00,0x00}, // e (101)
    {0x00,0x08,0x7E,0x09,0x09,0x00,0x00,0x00}, // f (102)
    {0x00,0x18,0xA4,0xA4,0xA4,0x7C,0x00,0x00}, // g (103)
    {0x00,0x7F,0x08,0x04,0x04,0x78,0x00,0x00}, // h (104)
    {0x00,0x00,0x7D,0x00,0x00,0x00,0x00,0x00}, // i (105)
    {0x00,0x80,0x84,0x7D,0x00,0x00,0x00,0x00}, // j (106)
    {0x00,0x7F,0x10,0x28,0x44,0x00,0x00,0x00}, // k (107)
    {0x00,0x41,0x7F,0x40,0x00,0x00,0x00,0x00}, // l (108)
    {0x00,0x7C,0x04,0x78,0x04,0x78,0x00,0x00}, // m (109)
    {0x00,0x7C,0x08,0x04,0x04,0x78,0x00,0x00}, // n (110)
    {0x00,0x38,0x44,0x44,0x44,0x38,0x00,0x00}, // o (111)
    {0x00,0xFC,0x24,0x24,0x24,0x18,0x00,0x00}, // p (112)
    {0x00,0x18,0x24,0x24,0x18,0xFC,0x00,0x00}, // q (113)
    {0x00,0x7C,0x08,0x04,0x04,0x00,0x00,0x00}, // r (114)
    {0x00,0x48,0x54,0x54,0x54,0x20,0x00,0x00}, // s (115)
    {0x00,0x04,0x3F,0x44,0x40,0x20,0x00,0x00}, // t (116)
    {0x00,0x3C,0x40,0x40,0x20,0x7C,0x00,0x00}, // u (117)
    {0x00,0x1C,0x20,0x40,0x20,0x1C,0x00,0x00}, // v (118)
    {0x00,0x3C,0x40,0x30,0x40,0x3C,0x00,0x00}, // w (119)
    {0x00,0x44,0x28,0x10,0x28,0x44,0x00,0x00}, // x (120)
    {0x00,0x1C,0xA0,0xA0,0xA0,0x7C,0x00,0x00}, // y (121)
    {0x00,0x44,0x64,0x54,0x4C,0x44,0x00,0x00}, // z (122)
    {0x00,0x08,0x36,0x41,0x41,0x00,0x00,0x00}, // { (123)
    {0x00,0x00,0x7F,0x00,0x00,0x00,0x00,0x00}, // | (124)
    {0x00,0x41,0x41,0x36,0x08,0x00,0x00,0x00}, // } (125)
    {0x00,0x08,0x04,0x08,0x10,0x08,0x00,0x00}, // ~ (126)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // DEL (127)
};
```

**解释：**
- 这是一个 96×8 的字节数组（ASCII 码 32-127，共 96 个可打印字符）
- 每个字符 8 个字节，每字节代表一列 8 个像素（SSD1306 的存储方式）
- 比如 `A` 的 8 个字节 `{0x7E,0x09,0x09,0x09,0x7E}` 在屏幕上显示为：
  ```
   ######
   #    #
   #    #
   ######
   #    #
   #    #
  ```
- `0x7E` 是十六进制，二进制是 `01111110`
- `const` 表示这些数据不会变，编译器会把它放到 Flash 而不是 RAM 里，省内存

---

**第 3 段：I2C 底层通信**

```c
// 发送一个命令字节给 SSD1306
static void ssd1306_write_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};  // 0x00 表示"这是命令"
    i2c_master_transmit(s_dev, buf, 2, pdMS_TO_TICKS(50));
    // pdMS_TO_TICKS(50) = 50 毫秒超时
}

// 发送一坨数据给 SSD1306（比如一整行像素）
static void ssd1306_write_data(const uint8_t *data, size_t len)
{
    uint8_t *buf = malloc(len + 1);  // 在堆上分配内存（栈不够 129 字节）
    if (buf == NULL) return;          // 分配失败就放弃
    buf[0] = 0x40;                    // 0x40 表示"这是数据"
    memcpy(buf + 1, data, len);       // 把数据复制到 buf 的第 2 个位置开始
    i2c_master_transmit(s_dev, buf, len + 1, pdMS_TO_TICKS(100));
    free(buf);                        // 释放内存
}
```

**解释：**
- I2C 通信中，每个操作都是主机（ESP32）发起，从机（SSD1306）响应
- 发送的第一个字节决定了对方把后面的内容当命令还是数据
  - `0x00` = 命令（告诉屏幕"翻页"、"设对比度"等）
  - `0x40` = 数据（直接写入屏幕的显示内存）
- `malloc` 和 `free` 是 C 语言在堆上分配内存的函数。为什么不用栈上的局部变量？因为 `len` 可能很大（128 字节），局部变量在栈上，栈空间很宝贵

---

**第 4 段：屏幕初始化**

```c
void display_init(void)
{
    s_mutex = xSemaphoreCreateMutex();  // 创建互斥锁

    // 第一步：初始化 I2C 总线
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_1,           // 用 I2C1（I2C0 可能被其他设备占用）
        .sda_io_num = I2C_SDA_GPIO,      // SDA = GPIO 2
        .scl_io_num = I2C_SCL_GPIO,      // SCL = GPIO 1
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },  // 启用芯片内部上拉
    };

    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    // 第二步：添加 SSD1306 设备到 I2C 总线上
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,   // 7 位地址
        .device_address = SSD1306_ADDR,           // 0x3C
        .scl_speed_hz = 400000,                   // 400kHz 通信速率
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev));

    // 第三步：发送 SSD1306 初始化序列（照着手册写的，不需要完全理解）
    ssd1306_write_cmd(0xAE); // 关闭显示
    ssd1306_write_cmd(0xD5); ssd1306_write_cmd(0x80); // 设置时钟
    ssd1306_write_cmd(0xA8); ssd1306_write_cmd(0x3F); // 多路复用 64
    ssd1306_write_cmd(0xD3); ssd1306_write_cmd(0x00); // 显示偏移 0
    ssd1306_write_cmd(0x40); // 起始行 0
    ssd1306_write_cmd(0x8D); ssd1306_write_cmd(0x14); // 开启电荷泵
    ssd1306_write_cmd(0x20); ssd1306_write_cmd(0x00); // 水平寻址模式
    ssd1306_write_cmd(0xA1); // 段重映射
    ssd1306_write_cmd(0xC8); // COM 扫描方向
    ssd1306_write_cmd(0xDA); ssd1306_write_cmd(0x12); // COM 引脚配置
    ssd1306_write_cmd(0x81); ssd1306_write_cmd(0xCF); // 对比度
    ssd1306_write_cmd(0xD9); ssd1306_write_cmd(0xF1); // 预充电周期
    ssd1306_write_cmd(0xDB); ssd1306_write_cmd(0x40); // VCOMH 电压
    ssd1306_write_cmd(0xA4); // 恢复 RAM 内容显示
    ssd1306_write_cmd(0xA6); // 正常显示（不反转）
    ssd1306_write_cmd(0xAF); // 开启显示

    display_clear();  // 清屏
    ESP_LOGI(TAG, "SSD1306 initialized");
}
```

**解释：**
- `ESP_ERROR_CHECK(...)` 是一个宏，如果括号里的函数返回错误，就立刻终止程序并打印错误信息
- SSD1306 的初始化序列是芯片手册规定的固定流程，不需要记，照抄就行
- 每行 `ssd1306_write_cmd(XX)` 都是在发送一个命令。有些命令后面需要跟一个参数
- `ESP_LOGI(TAG, "...")` 是 ESP-IDF 的日志函数，`I` 表示 Info 级别，会在串口监视器里打印

---

**第 5 段：清屏和写文字**

```c
void display_clear(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);  // 上锁（防止其他任务同时用屏幕）

    uint8_t zero[128] = {0};  // 128 个 0，等于一页空白

    for (int page = 0; page < SCREEN_PAGES; page++) {
        ssd1306_write_cmd(0xB0 + page);  // 翻到第 page 页
        ssd1306_write_cmd(0x00);          // 列地址低字节 = 0
        ssd1306_write_cmd(0x10);          // 列地址高字节 = 0
        ssd1306_write_data(zero, 128);    // 写入 128 字节空白
    }

    xSemaphoreGive(s_mutex);  // 解锁
}

void display_set_line(int line, const char *text)
{
    if (line < 0 || line >= SCREEN_PAGES) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // 构建这一页的像素数据：128 列 × 8 像素高
    uint8_t row[128] = {0};
    size_t len = strlen(text);  // 字符串长度

    // 把每个字符的 8 字节像素数据复制到 row 数组的对应位置
    for (size_t i = 0; i < len && i < 16; i++) {
        uint8_t c = (uint8_t)text[i];
        if (c < 32 || c > 127) c = 32;  // 不是可打印字符就用空格代替
        memcpy(&row[i * 8], font8x8[c - 32], 8);
        // c - 32: font8x8 数组的下标（空间是第 0 个，A 是第 33 个）
        // i * 8: 每个字符占 8 列，第 i 个字符从第 i*8 列开始
    }

    // 发送到屏幕
    ssd1306_write_cmd(0xB0 + line);  // 选页
    ssd1306_write_cmd(0x00);          // 列 = 0
    ssd1306_write_cmd(0x10);
    ssd1306_write_data(row, 128);

    xSemaphoreGive(s_mutex);
}
```

**解释：**
- SSD1306 的 128×64 像素被分成 8 个"页"（Page），每页 8 像素高 × 128 列宽
- 第 0 页 = 屏幕最上面 8 行像素（Y 坐标 0-7）
- 第 1 页 = 屏幕第 8-15 行像素
- 以此类推
- `0xB0` 是"设置当前页为 0"的命令，`0xB1` = 页 1，以此类推
- `memcpy(目标, 来源, 长度)` = 内存复制函数
- `xSemaphoreTake` / `xSemaphoreGive` 配对使用。上锁期间其他任务如果也想操作屏幕，会被阻塞直到解锁

**到这里，`display.c` 就写完了。**

---

### 4.3 音频播放器组件

#### 4.3.1 组件管理器配置

**文件路径：** `components/audio_player/idf_component.yml`

```yaml
dependencies:
  idf:
    version: '>=4.1.0'
  espressif/esp_audio_codec: ^2.5.0
```

**解释：**
- 告诉 ESP-IDF 组件管理器：这个项目需要 `esp_audio_codec` 这个第三方库
- `^2.5.0` 表示 2.5.0 及以上版本，但不超过 3.0.0
- 类似于 Node.js 的 package.json，pip 的 requirements.txt

#### 4.3.2 组件编译配置

**文件路径：** `components/audio_player/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "audio_player.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_netif esp_http_client esp_event nvs_flash driver esp_audio_codec esp-tls display
)
```

**解释：**
- `PRIV_REQUIRES` 列出了这个组件用到的所有 ESP-IDF 组件
- `esp_wifi` = WiFi 功能
- `esp_http_client` = HTTP 客户端
- `esp_audio_codec` = 音频解码库
- `display` = 我们刚写的屏幕组件

#### 4.3.3 头文件（公开接口）

**文件路径：** `components/audio_player/include/audio_player.h`

```c
#pragma once

void audio_player_start(void);

// volume: 0-100（百分比）
void audio_player_set_volume(int volume);
```

**解释：**
- 对外只暴露两个函数：启动播放器和设置音量
- 所有内部实现细节都藏在 `audio_player.c` 里

#### 4.3.4 主逻辑文件

**文件路径：** `components/audio_player/audio_player.c`

这是整个项目最核心的文件（约 600 行）。我分段写，边写边解释。

---

**第 1 段：头文件包含**

```c
#include "audio_player.h"
#include "display.h"

#include <ctype.h>      // isspace()
#include <string.h>     // memcmp, strlen, strncpy
#include <strings.h>    // strcasecmp, strncasecmp
#include <stdlib.h>     // malloc, free, realloc

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_crt_bundle.h"        // HTTPS 证书
#include "esp_event.h"             // 事件系统
#include "esp_http_client.h"       // HTTP 客户端
#include "esp_log.h"               // 日志
#include "esp_netif.h"             // 网络接口
#include "esp_wifi.h"              // WiFi
#include "nvs_flash.h"             // 非易失性存储（WiFi 要用）

#include "driver/gpio.h"           // GPIO（按键）
#include "driver/i2s_std.h"        // I2S（音频输出）

#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
```

**解释：**
- `#include` 语句分三组：
  1. 自己写的头文件（用 `""`）
  2. C 标准库（用 `<>`）
  3. ESP-IDF 的头文件（用 `<>`）
- 每个库提供不同功能，用到了才 include

---

**第 2 段：配置常量**

```c
// WiFi 信息 —— 改成你自己的！
#define WIFI_SSID "你的WiFi名"
#define WIFI_PASS "你的WiFi密码"

// 音乐流地址（Navidrome 服务器）
// 这个地址需要替换成你自己的服务器地址
#define STREAM_URL "https://你的服务器地址/rest/stream?id=歌曲ID&u=用户名&t=token&s=salt&v=1.16.1&c=meting&f=json"

// I2S 引脚定义
#define I2S_BCLK_GPIO 5    // BCLK 接 GPIO 5
#define I2S_LRCK_GPIO 4    // LRCK 接 GPIO 4
#define I2S_DOUT_GPIO 6    // DOUT 接 GPIO 6

// 按键引脚定义
#define BTN_UP_GPIO  7     // 音量+ 接 GPIO 7
#define BTN_DN_GPIO  13    // 音量- 接 GPIO 13

// 按键参数
#define BTN_DEBOUNCE_MS  30    // 消抖时间 30 毫秒
#define BTN_HOLD_MS      400   // 长按阈值 400 毫秒
#define BTN_REPEAT_MS    80    // 长按连续触发间隔 80 毫秒

// 网络和缓冲区
#define HTTP_READ_SIZE 2048     // 每次 HTTP 读取 2048 字节
#define OUT_PCM_INIT_SIZE 8192  // PCM 输出缓冲区初始大小

static const char *TAG = "audio";  // 日志标签
```

**解释：**
- 所有引脚号都集中在文件顶部定义，改接线时只需要改这里
- `#define` 宏定义在编译前会被替换成实际值，不占用内存
- `static const char *TAG` — 定义一个不会变的字符串指针，日志输出时会带上这个标签，方便在串口监视器里过滤

---

**第 3 段：全局状态变量**

```c
// WiFi 事件组：用来通知"WiFi 连上了"
static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;

// I2S 音频输出相关
static i2s_chan_handle_t s_i2s_tx;     // I2S 通道句柄
static int s_i2s_sample_rate;          // 当前采样率
static int16_t *s_stereo_buf;          // 立体声缓冲（单声道转立体声时用）
static size_t s_stereo_buf_samples;    // 缓冲区大小（采样数）

// 音量和显示状态（被多个任务读写）
static int s_volume = 15;              // 当前音量（0-100），默认 15%
static char s_disp_status[17] = "";    // 屏幕状态行文字
static char s_disp_wifi[17] = "";      // 屏幕 WiFi 行文字
static TaskHandle_t s_disp_task;       // 屏幕刷新任务的句柄

// 前置声明 —— 告诉编译器"这些函数定义在后面，但前面要先用到"
static void notify_display(void);
static void set_disp_status(const char *msg);
static void set_disp_wifi(const char *msg);
```

**解释：**
- `s_` 前缀是命名习惯，表示 static 变量
- `EventGroupHandle_t` — FreeRTOS 的事件组，一种任务间通信方式。一个任务等待，另一个任务发送信号
- `TaskHandle_t` — 任务句柄，用来给指定任务发通知
- 前置声明是 C 语言的规则：函数必须先声明后使用。如果 `audio_stream_task` 里要调用 `set_disp_status`，而 `set_disp_status` 写在 `audio_stream_task` 后面，就需要在前面先声明一下

---

**第 4 段：WiFi 初始化**

```c
// HTTP 响应头结构体（用来获取 Content-Type）
typedef struct {
    char content_type[64];  // 存 Content-Type 头的值，比如 "audio/mpeg"
} http_ctx_t;

// HTTP 事件回调：收到响应头时记录 Content-Type
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt == NULL || evt->user_data == NULL) {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key && evt->header_value) {
        http_ctx_t *ctx = (http_ctx_t *)evt->user_data;
        if (strcasecmp(evt->header_key, "Content-Type") == 0) {
            strncpy(ctx->content_type, evt->header_value, sizeof(ctx->content_type) - 1);
            ctx->content_type[sizeof(ctx->content_type) - 1] = '\0';
        }
    }

    return ESP_OK;
}

// 不区分大小写的子串查找
static bool string_contains_ci(const char *haystack, const char *needle)
{
    if (haystack == NULL || needle == NULL) return false;

    size_t needle_len = strlen(needle);
    if (needle_len == 0) return false;

    for (const char *p = haystack; *p; p++) {
        if (strncasecmp(p, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}
```

**解释：**
- `http_event_handler` 是 HTTP 客户端的回调函数。每收到一个响应头（如 `Content-Type: audio/mpeg`），ESP-IDF 就会调用这个函数
- `string_contains_ci` 检查一个字符串是否包含另一个（不区分大小写）。比如 `string_contains_ci("audio/mpeg", "mp3")` 返回 false，但 `string_contains_ci("audio/mp3", "mp3")` 返回 true
- `strncasecmp` = 比较 N 个字符，忽略大小写
- `strncpy` = 复制字符串，指定最大长度，防止溢出

---

**第 5 段：音频格式检测**

```c
// 根据 Content-Type 头和数据的前几个字节（魔数）判断音频格式
static esp_audio_simple_dec_type_t detect_decoder_type(
    const char *content_type, const uint8_t *data, size_t len)
{
    // 方法 1：看 Content-Type 头
    if (content_type && content_type[0] != '\0') {
        if (string_contains_ci(content_type, "application/json") ||
            string_contains_ci(content_type, "text/")) {
            return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;  // 不是音频数据
        }
        if (string_contains_ci(content_type, "audio/mpeg") ||
            string_contains_ci(content_type, "audio/mp3")) {
            return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
        }
        if (string_contains_ci(content_type, "audio/aac") ||
            string_contains_ci(content_type, "audio/aacp")) {
            return ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
        }
        if (string_contains_ci(content_type, "audio/flac")) {
            return ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC;
        }
        if (string_contains_ci(content_type, "audio/ogg") ||
            string_contains_ci(content_type, "application/ogg")) {
            return ESP_AUDIO_SIMPLE_DEC_TYPE_OGG;
        }
        if (string_contains_ci(content_type, "audio/wav") ||
            string_contains_ci(content_type, "audio/x-wav")) {
            return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
        }
        if (string_contains_ci(content_type, "audio/mp4") ||
            string_contains_ci(content_type, "audio/m4a") ||
            string_contains_ci(content_type, "video/mp4")) {
            return ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
        }
    }

    // 方法 2：看文件头魔数（Magic Number）
    // 如果返回的是 JSON 数据
    for (size_t i = 0; i < len; i++) {
        if (!isspace((int)data[i])) {
            if (data[i] == '{' || data[i] == '[') {
                return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
            }
            break;
        }
    }

    // 每种格式的前几个字节都有固定特征
    if (len >= 3 && memcmp(data, "ID3", 3) == 0)     return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;   // MP3 ID3 标签
    if (len >= 4 && memcmp(data, "fLaC", 4) == 0)     return ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC;
    if (len >= 4 && memcmp(data, "OggS", 4) == 0)     return ESP_AUDIO_SIMPLE_DEC_TYPE_OGG;
    if (len >= 12 && memcmp(data, "RIFF", 4) == 0 &&
                     memcmp(data + 8, "WAVE", 4) == 0)  return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    if (len >= 8 && memcmp(data + 4, "ftyp", 4) == 0) return ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
    if (len >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0)
                                                     return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;   // MP3 帧同步字

    return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
}
```

**解释：**
- 每种文件格式的前几个字节都有自己的"魔数"（Magic Number），就像身份证号的前几位代表省份
  - MP3：前 3 字节是 `ID3`（ID3 标签），或者前 2 字节的二进制是 `11111111 111xxxxx`
  - FLAC：前 4 字节是 `fLaC`
  - OGG：前 4 字节是 `OggS`
  - WAV：前 4 字节是 `RIFF`，第 8-11 字节是 `WAVE`
  - M4A/MP4：第 5-8 字节是 `ftyp`
- `memcmp(a, b, n)` 比较 `a` 和 `b` 前 n 个字节是否相等
- `data + 4` 表示"从 data 地址向后偏移 4 个字节的位置"
- `(data[1] & 0xE0)` 是位运算，`0xE0` 的二进制是 `11100000`，`&` 操作只保留高 3 位

---

**第 6 段：WiFi 事件处理**

```c
// WiFi 事件回调：STA 模式启动时自动连接，断开时自动重连
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();      // WiFi 驱动启动好了，开始连接
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();      // 断开了就重连
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        // ↑ 拿到了 IP = WiFi 连接成功，设置事件组通知
    }
}
```

**解释：**
- ESP-IDF 使用事件驱动模型。WiFi 状态变化时会自动调用这个函数
- 三个关键事件：
  1. `WIFI_EVENT_STA_START` — WiFi 硬件初始化好了，可以调 `esp_wifi_connect` 去连接
  2. `WIFI_EVENT_STA_DISCONNECTED` — 断连了，重启连接
  3. `IP_EVENT_STA_GOT_IP` — 拿到 IP 地址了，说明连上了。设置事件组的标志位，通知等待中的任务

---

**第 7 段：WiFi 初始化函数**

```c
static esp_err_t wifi_init_sta(void)
{
    // NVS（Non-Volatile Storage）：WiFi 驱动用它在 Flash 里保存数据
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return ret;

    // 初始化网络协议栈
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // 初始化 WiFi 硬件
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 创建事件组
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) return ESP_ERR_NO_MEM;

    // 注册事件回调（任何 WiFi 事件和 IP 事件都调 wifi_event_handler）
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    // 设置 WiFi 名称和密码
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    // 设为 STA 模式（连接路由器）并启动
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}
```

**解释：**
- 这个函数在应用启动时调用一次
- NVS = Non-Volatile Storage，就是 ESP32 闪存里划出的一小块区域，WiFi 驱动会往里存上次的连接信息
- `STA` = Station，即"客户端模式"（连别人的 WiFi），和 AP 模式（自己当热点）相对
- `ESP_ERROR_CHECK` 包裹的函数如果返回错误，会自动打印错误信息并让程序终止

---

**第 8 段：I2S 音频输出初始化**

```c
static esp_err_t i2s_setup(uint32_t sample_rate)
{
    if (s_i2s_tx == NULL) {  // 第一次初始化
        // I2S 通道配置：主模式 + 大 DMA 缓冲区
        i2s_chan_config_t chan_cfg = {
            .id = I2S_NUM_0,
            .role = I2S_ROLE_MASTER,   // ESP32 做主设备（产生时钟信号）
            .dma_desc_num = 16,         // 16 个 DMA 描述符（~174ms 缓冲）
            .dma_frame_num = 480,       // 每个描述符 480 帧
            .auto_clear = true,         // 欠载时输出静音，不重放旧数据
        };
        ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_i2s_tx, NULL));

        // I2S 标准模式配置：16 位立体声、Philips 格式
        i2s_std_config_t std_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,   // 不需要主时钟
                .bclk = I2S_BCLK_GPIO,     // BCLK = GPIO 5
                .ws   = I2S_LRCK_GPIO,     // LRCK = GPIO 4
                .dout = I2S_DOUT_GPIO,     // DOUT = GPIO 6
                .din  = I2S_GPIO_UNUSED,   // 不输入
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv   = false,
                },
            },
        };
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_tx, &std_cfg));
        ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_tx));  // 开启 I2S
        s_i2s_sample_rate = (int)sample_rate;
        return ESP_OK;
    }

    // 如果采样率变了，重新配置时钟
    if (s_i2s_sample_rate != (int)sample_rate) {
        i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
        ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(s_i2s_tx, &clk_cfg));
        s_i2s_sample_rate = (int)sample_rate;
    }

    return ESP_OK;
}
```

**解释：**
- I2S 用 DMA 搬运数据：硬件自动从内存把 PCM 数据送到 I2S 引脚，不占 CPU
- `dma_desc_num = 16` 和 `dma_frame_num = 480` 决定了缓冲区大小：
  - 16 × 480 = 7680 帧的缓冲区
  - 1 帧 = 左右声道各 1 个 16 位采样 = 4 字节
  - 总缓冲 = 7680 × 4 = 30720 字节 ≈ 174 毫秒（在 44.1kHz 下）
  - 这意味着即便 WiFi 有短暂延迟，音频也有足够缓冲不会断流
- `auto_clear = true`：DMA 缓冲区欠载时输出静音，防止重放旧数据
- `I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG` 是标准 I2S 格式，几乎所有音频 DAC 都支持

---

**第 9 段：PCM 数据写入 I2S**

```c
// 把解码后的 PCM 数据写入 I2S，同时做音量调节
static void i2s_write_pcm(uint8_t *pcm, size_t size, uint8_t channels)
{
    size_t bytes_written = 0;
    size_t samples = size / sizeof(int16_t);  // 16 位 = 2 字节一个采样
    int16_t *buf = (int16_t *)pcm;
    int vol = s_volume;  // 当前音量 0-100

    if (channels == 1) {
        // 单声道 → 扩成立体声（把每个采样复制一份给左右声道）
        if (samples * 2 > s_stereo_buf_samples) {
            int16_t *new_buf = realloc(s_stereo_buf, samples * 2 * sizeof(int16_t));
            if (new_buf == NULL) return;
            s_stereo_buf = new_buf;
            s_stereo_buf_samples = samples * 2;
        }

        for (size_t i = 0; i < samples; i++) {
            int16_t s = (int16_t)((int32_t)buf[i] * vol / 100);  // 整数音量调节
            s_stereo_buf[i * 2]     = s;  // 左声道
            s_stereo_buf[i * 2 + 1] = s;  // 右声道
        }
        i2s_channel_write(s_i2s_tx, s_stereo_buf,
                          samples * 2 * sizeof(int16_t),
                          &bytes_written, portMAX_DELAY);
        return;
    }

    // 立体声：原地修改（直接在原缓冲上乘音量系数）
    for (size_t i = 0; i < samples; i++) {
        buf[i] = (int16_t)((int32_t)buf[i] * vol / 100);
    }
    i2s_channel_write(s_i2s_tx, buf, size, &bytes_written, portMAX_DELAY);
}
```

**解释：**
- 这段代码做了两件事：**音量调节** 和 **写入 I2S**
- 音量调节的原理：把每个音频采样值乘以音量百分比
  - 比如音量 50%，采样值 10000 → 10000 × 50 ÷ 100 = 5000（幅度减半，声音变小）
- 为什么用 `(int32_t)buf[i] * vol / 100` 而不是浮点数？
  - **ESP32-S3 没有硬件浮点单元！** 浮点运算靠软件模拟，极慢
  - 44.1kHz 立体声 = 每秒 88200 个采样，每个做浮点运算 → CPU 跟不上 → 卡顿和杂音
  - 整数乘除法是硬件指令，非常快
- 单声道处理：音频流只有一个声道，但 I2S 配置成立体声，所以要把每个采样复制一份（左=右）
- `realloc(ptr, new_size)` = 调整之前分配的内存大小

---

**第 10 段：音频流播放任务（核心！）**

这是整个项目的核心逻辑。它会：
1. 注册解码器
2. 无限循环：连服务器 → 获取音频流 → 检测格式 → 打开解码器 → 解码 → 播放

```c
static void audio_stream_task(void *arg)
{
    ESP_LOGI(TAG, "Registering decoders");
    ESP_ERROR_CHECK(esp_audio_dec_register_default());           // 注册标准解码器
    ESP_ERROR_CHECK(esp_audio_simple_dec_register_default());    // 注册简易解码器

    // 分配输入输出缓冲
    uint8_t *in_buf = malloc(HTTP_READ_SIZE);     // HTTP 接收缓冲（2048 字节）
    uint8_t *out_buf = malloc(OUT_PCM_INIT_SIZE); // 解码输出缓冲（8192 字节）
    size_t out_size = OUT_PCM_INIT_SIZE;

    if (in_buf == NULL || out_buf == NULL) {
        ESP_LOGE(TAG, "Buffer alloc failed");
        free(in_buf);
        free(out_buf);
        vTaskDelete(NULL);  // 分配失败就删除自己这个任务
        return;
    }

    // 主循环：断线重连
    for (;;) {
        // === 建立 HTTP 连接 ===
        http_ctx_t http_ctx = {0};
        esp_http_client_config_t config = {
            .url = STREAM_URL,
            .timeout_ms = 10000,
            .buffer_size = HTTP_READ_SIZE,
            .user_agent = "esp32-mp3",
            .crt_bundle_attach = esp_crt_bundle_attach,   // HTTPS 证书
            .event_handler = http_event_handler,           // 响应头回调
            .user_data = &http_ctx,                        // 传给回调的数据
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            esp_http_client_cleanup(client);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // 获取响应头
        esp_http_client_fetch_headers(client);
        set_disp_status("  Streaming...");

        // 检查 HTTP 状态码（200 = 成功，其他 = 失败）
        int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            ESP_LOGE(TAG, "HTTP status %d", status);
            goto stream_end;
        }

        ESP_LOGI(TAG, "Content-Type: %s",
                 http_ctx.content_type[0] ? http_ctx.content_type : "unknown");

        // 读取第一块数据，用来检测音频格式
        int read = esp_http_client_read(client, (char *)in_buf, HTTP_READ_SIZE);
        if (read <= 0) goto stream_end;

        // === 检测音频格式 ===
        esp_audio_simple_dec_type_t dec_type =
            detect_decoder_type(http_ctx.content_type, in_buf, (size_t)read);

        if (dec_type == ESP_AUDIO_SIMPLE_DEC_TYPE_NONE) {
            ESP_LOGE(TAG, "Unsupported or non-audio response");
            goto stream_end;
        }
        ESP_LOGI(TAG, "Decoder: %s", esp_audio_simple_dec_get_name(dec_type));

        // === 打开解码器 ===
        esp_audio_simple_dec_cfg_t dec_cfg = {
            .dec_type = dec_type,
            .dec_cfg = NULL,
            .cfg_size = 0,
            .use_frame_dec = false,
        };

        esp_audio_simple_dec_handle_t dec_handle = NULL;
        esp_audio_err_t dec_ret = esp_audio_simple_dec_open(&dec_cfg, &dec_handle);
        if (dec_ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "Decoder open failed: %d", dec_ret);
            goto stream_end;
        }

        // === 解码循环 ===
        esp_audio_simple_dec_info_t dec_info = {0};  // 音频信息（采样率、声道数等）
        bool info_ready = false;  // 是否已经获取到音频信息
        bool use_prefetch = true;  // 第一块数据已经读好了，不需要再读

        while (1) {
            // 读取下一块 HTTP 数据
            if (!use_prefetch) {
                read = esp_http_client_read(client, (char *)in_buf, HTTP_READ_SIZE);
            } else {
                use_prefetch = false;  // 第一次用 prefetch 的数据
            }
            if (read <= 0) break;  // 读取失败或 EOF = 结束

            esp_audio_simple_dec_raw_t raw = {
                .buffer = in_buf,
                .len = (uint32_t)read,
                .eos = false,
            };

            // 解码循环（一个 raw 数据可能包含多个音频帧）
            while (raw.len > 0) {
                esp_audio_simple_dec_out_t out = {
                    .buffer = out_buf,
                    .len = (uint32_t)out_size,
                };

                dec_ret = esp_audio_simple_dec_process(dec_handle, &raw, &out);

                // 输出缓冲区不够大，重新分配
                if (dec_ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                    uint8_t *new_buf = realloc(out_buf, out.needed_size);
                    if (new_buf == NULL) goto stream_end;
                    out_buf = new_buf;
                    out_size = out.needed_size;
                    continue;  // 重试
                }

                if (dec_ret != ESP_AUDIO_ERR_OK) {
                    ESP_LOGE(TAG, "Decode error: %d", dec_ret);
                    goto stream_end;
                }

                // 解码成功，有 PCM 数据
                if (out.decoded_size > 0) {
                    // 第一次解码成功时，获取音频信息并初始化 I2S
                    if (!info_ready) {
                        if (esp_audio_simple_dec_get_info(dec_handle, &dec_info) == ESP_AUDIO_ERR_OK) {
                            if (dec_info.bits_per_sample != 16) {
                                ESP_LOGE(TAG, "Unsupported bits per sample: %u",
                                         dec_info.bits_per_sample);
                                goto stream_end;
                            }
                            ESP_ERROR_CHECK(i2s_setup(dec_info.sample_rate));
                            info_ready = true;
                            ESP_LOGI(TAG, "Audio info: %u Hz, %u ch",
                                     dec_info.sample_rate, dec_info.channel);
                        }
                    }

                    // 写入 I2S 播放
                    if (info_ready) {
                        i2s_write_pcm(out_buf, out.decoded_size, dec_info.channel);
                    }
                }

                // 更新消费进度
                raw.len -= raw.consumed;
                raw.buffer += raw.consumed;
            }
        }

    // === 清理（断线后到这里）===
    stream_end:
        if (dec_handle != NULL) {
            esp_audio_simple_dec_close(dec_handle);
            dec_handle = NULL;
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        ESP_LOGI(TAG, "Stream ended, reconnecting...");
        set_disp_status("  Reconnecting...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
```

**解释：**
- 这是一个 FreeRTOS 任务函数，必须是无限循环（`for(;;)`）并且永不返回
- **解码流程总结：**
  ```
  HTTP 读数据 → 放进 raw 缓冲 → 解码器处理 → 输出 PCM 数据 → 写 I2S
  ```
- `goto stream_end` — C 语言的跳转语句。当出错或流结束时，跳转到清理代码。不是 goto 有害，而是无序的 goto 有害。这里用在一个函数里向前跳转做统一清理，是 C 语言中的标准做法
- `vTaskDelay(pdMS_TO_TICKS(2000))` — 让任务休眠 2000 毫秒（2 秒），把 CPU 让给其他任务
- 断线后等 2 秒再重连，避免疯狂重试

---

**第 11 段：屏幕刷新任务**

这是架构的关键：**所有 I2C 操作（屏幕刷新）只在这个任务里做**，其他任务只改变量 + 发通知。

```c
// 屏幕刷新任务 —— 唯一操作 I2C 的任务
static void display_update_task(void *arg)
{
    while (1) {
        // 等待通知（有通知就刷新，最多等 1 秒也自动刷新一次）
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));

        int vol = s_volume;

        // 第 0 行：音量百分比
        char buf[17];
        snprintf(buf, sizeof(buf), "Vol: %3d%%", vol);
        display_set_line(0, buf);

        // 第 1 行：音量进度条（用 # 表示已填充，- 表示未填充）
        char bar_text[17];
        int filled = vol * 16 / 100;  // 16 个字符宽的进度条
        for (int i = 0; i < 16; i++) {
            bar_text[i] = (i < filled) ? '#' : '-';
        }
        bar_text[16] = '\0';  // C 字符串必须以 '\0' 结尾
        display_set_line(1, bar_text);

        // 第 3 行：播放状态（Streaming... / Reconnecting...）
        display_set_line(3, s_disp_status);

        // 第 6 行：WiFi 状态
        display_set_line(6, s_disp_wifi);
    }
}

// 通知屏幕刷新任务
static void notify_display(void)
{
    if (s_disp_task) {
        xTaskNotifyGive(s_disp_task);
    }
}

// 设置状态文字（线程安全，无阻塞）
static void set_disp_status(const char *msg)
{
    strncpy(s_disp_status, msg, 16);
    s_disp_status[16] = '\0';
    notify_display();
}

static void set_disp_wifi(const char *msg)
{
    strncpy(s_disp_wifi, msg, 16);
    s_disp_wifi[16] = '\0';
    notify_display();
}
```

**解释：**
- `ulTaskNotifyTake` — 等待任务通知。收到通知时返回，超时也会返回。返回后刷新屏幕
- `xTaskNotifyGive` — 发任务通知。比互斥锁和队列都快，适合"嘿，该刷新了"这种场景
- 架构模式：**生产者-通知者**
  - 按钮任务和音频任务只改变量（`s_volume`、`s_disp_status`），然后发通知
  - 屏幕任务收到通知后读变量、刷新屏幕
  - 这样 I2C 操作永远不会在按钮任务或音频任务的调用栈上发生

---

**第 12 段：按键处理任务**

```c
static void button_task(void *arg)
{
    int last_up = 1, last_dn = 1;    // 上一次的电平状态（1 = 未按下）
    int hold_up = 0, hold_dn = 0;    // 按住计数器

    // 配置 GPIO
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << BTN_UP_GPIO) | (1ULL << BTN_DN_GPIO),
        .mode = GPIO_MODE_INPUT,           // 输入模式
        .pull_up_en = GPIO_PULLUP_ENABLE,  // 启用内部上拉（不按时 = 高电平）
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,    // 不用中断，用轮询
    };
    gpio_config(&io_cfg);

    while (1) {
        int up = gpio_get_level(BTN_UP_GPIO);  // 读 GPIO 7
        int dn = gpio_get_level(BTN_DN_GPIO);  // 读 GPIO 13

        // === 音量+ ===
        if (up == 0) {  // 按下 = 低电平
            if (last_up == 1) {  // 首次按下（下降沿）
                s_volume = (s_volume + 5 > 100) ? 100 : s_volume + 5;
                notify_display();
                ESP_LOGI(TAG, "Volume: %d%%", s_volume);
                hold_up = 0;
            } else if (hold_up >= BTN_HOLD_MS / BTN_DEBOUNCE_MS) {
                // 长按：每 80ms 调节一次
                if (hold_up % (BTN_REPEAT_MS / BTN_DEBOUNCE_MS) == 0) {
                    s_volume = (s_volume + 5 > 100) ? 100 : s_volume + 5;
                    notify_display();
                }
            }
            hold_up++;
        } else {
            hold_up = 0;  // 松开了，清零
        }

        // === 音量- ===
        if (dn == 0) {
            if (last_dn == 1) {
                s_volume = (s_volume - 5 < 0) ? 0 : s_volume - 5;
                notify_display();
                ESP_LOGI(TAG, "Volume: %d%%", s_volume);
                hold_dn = 0;
            } else if (hold_dn >= BTN_HOLD_MS / BTN_DEBOUNCE_MS) {
                if (hold_dn % (BTN_REPEAT_MS / BTN_DEBOUNCE_MS) == 0) {
                    s_volume = (s_volume - 5 < 0) ? 0 : s_volume - 5;
                    notify_display();
                }
            }
            hold_dn++;
        } else {
            hold_dn = 0;
        }

        last_up = up;
        last_dn = dn;
        vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_MS));  // 30ms 的消抖周期
    }
}
```

**解释：**
- `(1ULL << BTN_UP_GPIO)` — 位运算，`1ULL` 是 64 位无符号整数 `1`，左移 7 位变成第 7 位为 1 的位掩码
- 内部上拉电阻：不按按键时 GPIO 通过内部电阻接到 3.3V，读出来是高电平（1）。按下时被拉到 GND，读出来是低电平（0）
- **消抖原理**：每 30ms 检查一次，机械抖动通常 < 20ms，所以等 30ms 后再读，抖动已经过去了
- **长按检测**：用计数器累计按了多久，超过 400ms 就开始连续触发
- **三元运算符**：`(条件) ? 真时的值 : 假时的值`，相当于一个简短的 if-else
  - `s_volume = (s_volume + 5 > 100) ? 100 : s_volume + 5;` 意思是：如果加 5 后超过 100，就保持 100，否则加 5

---

**第 13 段：公开接口**

```c
// 公开的设音量函数（main.c 也可以调用）
void audio_player_set_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    s_volume = volume;
    notify_display();
    ESP_LOGI(TAG, "Volume: %d%%", volume);
}

// 启动一切！
void audio_player_start(void)
{
    // 1. 初始化 WiFi
    esp_err_t ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(ret));
        return;
    }

    // 2. 初始化屏幕并启动屏幕刷新任务
    display_init();
    set_disp_wifi("  WiFi: Connecting");
    xTaskCreate(display_update_task, "display_upd", 2560, NULL, 5, &s_disp_task);

    // 3. 等待 WiFi 连接
    ESP_LOGI(TAG, "Connecting Wi-Fi...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Wi-Fi connected");
    esp_wifi_set_ps(WIFI_PS_NONE);   // 关闭 WiFi 省电，避免延迟尖峰导致音频卡顿
    set_disp_wifi("  WiFi: Connected ");

    // 4. 启动音频流任务和按键任务
    xTaskCreate(audio_stream_task, "audio_stream", 16384, NULL, 20, NULL);
    xTaskCreate(button_task, "button", 2560, NULL, 10, NULL);
}
```

**解释：**
- `xTaskCreate` 的参数：`(任务函数, 名称, 栈大小, 参数, 优先级, 句柄输出)`
- **优先级数字越大越高**。音频任务 20 > 按键任务 10 > 屏幕任务 5
  - 音频优先级最高，因为音频断流影响最大
  - 屏幕优先级最低，卡一下无所谓
- `xEventGroupWaitBits` — 阻塞等待，直到 WiFi 连接成功（`WIFI_CONNECTED_BIT` 被设置）
- `portMAX_DELAY` — 永远等待

**到这里，`audio_player.c` 就写完了。**

---

### 4.4 主入口文件

**文件路径：** `main/CMakeLists.txt`

```cmake
idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES audio_player)
```

**文件路径：** `main/main.c`

```c
#include "audio_player.h"

void app_main(void)
{
    audio_player_start();
}
```

**解释：**
- `app_main` 是 ESP-IDF 程序的入口函数，相当于普通 C 程序的 `int main()`
- 整个程序只做一件事：调用 `audio_player_start()`，然后由 FreeRTOS 调度各个任务
- `app_main` 函数返回后，主任务被删除，但其他任务继续运行

---

### 4.5 修改 WiFi 和流地址

在 `components/audio_player/audio_player.c` 顶部找到这两行，改成你自己的：

```c
#define WIFI_SSID "你的WiFi名字"
#define WIFI_PASS "你的WiFi密码"

#define STREAM_URL "https://你的音乐服务器地址/rest/stream?id=..."
```

---

## 5. 接线

> **开始接线前，把 USB 线从开发板上拔掉！**

### 5.1 I2S 功放模块

| MAX98357 模块 | ESP32-S3 |
|--------------|----------|
| VIN | 3.3V 或 5V（看模块规格） |
| GND | GND |
| BCLK | GPIO 5 |
| LRC | GPIO 4 |
| DIN | GPIO 6 |

模块上的 SD 引脚不接或者接 GND（关闭休眠）。

喇叭的两根线接到功放模块的喇叭输出端（通常标着 SPK+ 和 SPK-）。

### 5.2 SSD1306 OLED 屏幕

| SSD1306 | ESP32-S3 |
|---------|----------|
| VCC | 3.3V |
| GND | GND |
| SCL | GPIO 1 |
| SDA | GPIO 2 |

### 5.3 按键

| 按键 | 引脚 1 | 引脚 2 |
|------|--------|--------|
| 上方（音量+） | GPIO 7 | GND |
| 下方（音量-） | GPIO 13 | GND |
| BOOT（播放/暂停） | GPIO 0 | GND |

> **注意：** GPIO 0 是开发板上的 BOOT 按键，板上已有一个按键连接 GPIO 0 到 GND，无需另外接线。如果要用外接按键，需要和板上 BOOT 按键共用 GPIO 0。

> **接线检查清单：**
> - [ ] GND 全部连通（开发板、屏幕、功放、按键共地）
> - [ ] 所有 3.3V 正确连接
> - [ ] 没有短路（可以用万用表的蜂鸣档测 3.3V 和 GND 之间）
> - [ ] 按键两端不要直接都接 GPIO！

---

## 6. 编译与烧录

### 6.1 编译

打开 "ESP-IDF 5.5 PowerShell"，进入项目目录：

```bash
cd D:\Users\你的用户名\Desktop\it_is_my_time
idf.py build
```

第一次编译会很慢（5-10 分钟），因为要下载和编译所有依赖库。之后会快很多。

如果编译报错 `Implicit declaration of function` 之类的，检查：
- 文件是否放在正确的目录
- `#include` 的路径是否正确
- 是否有拼写错误

### 6.2 烧录

```bash
idf.py -p COM7 flash
```

把 `COM7` 换成你实际的 COM 端口号（在设备管理器里看）。

如果显示 "A fatal error occurred: Could not open COM7"：
1. 检查 USB 线是否插好
2. 检查设备管理器里的 COM 端口号
3. 检查是否打开了串口监视器（只能一个程序占用串口）

### 6.3 查看日志

```bash
idf.py -p COM7 monitor
```

烧录成功后会看到类似输出：
```
I (1234) audio: Connecting Wi-Fi...
I (2345) audio: Wi-Fi connected
I (2456) display: SSD1306 initialized
I (3456) audio: Registering decoders
I (4567) audio: Streaming from https://...
I (5678) audio: Content-Type: audio/mpeg
I (6789) audio: Decoder: MP3
I (7890) audio: Audio info: 44100 Hz, 2 ch
```

按 `Ctrl + ]` 退出监视器。

---

## 7. 测试与使用

### 7.1 首次启动

1. 插上 USB 供电（或直接接 5V 电源）
2. 等几秒，屏幕会依次显示：
   - `WiFi: Connecting`
   - `WiFi: Connected`
   - `Streaming...`
3. 听到声音 = 成功！

### 7.2 按键操作

- **短按上方按键**：音量 +5%
- **短按下方按键**：音量 -5%
- **长按任意按键**：连续调节音量

屏幕上的进度条 `######-----------` 会实时反映音量变化。

### 7.3 常见问题

| 问题 | 可能原因 | 解决方法 |
|------|---------|---------|
| 屏幕字符只显示一半（后面被截断） | SSD1306 寻址模式配置错误 | 用 `0x20, 0x02`（Page Mode）替代 `0x20, 0x00`（Horizontal），并添加 `0x21`/`0x22` 地址范围命令 |
| 屏幕不亮 | I2C 接错、供电不足 | 检查 SCL/SDA 接线，量 3.3V 电压 |
| 音频时不时卡顿、或"一个字唱两遍" | WiFi 省电导致延迟尖峰、DMA 缓冲区欠载重放 | 1. `esp_wifi_set_ps(WIFI_PS_NONE)` 关 WiFi 省电；2. I2S 设 `auto_clear = true`；3. `dma_desc_num` 增大到 16 |
| 有声音但有杂音 | I2S 时钟不准 | 这是 ESP32-S3 的正常现象，可以考虑外加 I2S DAC（如 PCM5102） |
| WiFi 连不上 | 密码错误、信号差 | 核对 SSID 和密码，把开发板放近路由器 |
| 流获取失败 | URL 过期 | Navidrome 的 token 有时效，需要重新生成 URL |
| 按键没反应 | GPIO 接错、内部上拉未启用 | 检查接线，用万用表量按键两端 |
| 编译报错 | 组件路径不对 | 检查 `components/` 下的目录结构是否正确 |

---

## 附录 A：如何获取 Navidrome 流地址

如果你用的是 Navidrome 音乐服务器：

1. 浏览器打开 `https://你的Navidrome地址`
2. 登录后选一首歌播放
3. 按 F12 打开浏览器开发者工具 → Network（网络）标签
4. 找到 `stream?id=...` 请求
5. 右键 → Copy → Copy link address
6. 粘贴到代码里替换 `STREAM_URL`

> 注意：URL 里的 `t=` 参数是时效性的 token，如果过期了需要重新获取。可以在 Navidrome 设置里找找有没有生成长期 API key 的方法。

---

## 附录 B：代码中 C 语言语法的速查表

| 语法 | 含义 | 示例 |
|------|------|------|
| `int x = 0;` | 定义整型变量 | |
| `int16_t x;` | 定义 16 位有符号整数 | |
| `uint8_t x;` | 定义 8 位无符号整数（0-255） | |
| `size_t x;` | 定义大小类型（用于数组长度等） | |
| `char s[17];` | 定义 17 个字符的数组 | `char name[] = "hello";` |
| `const char *s;` | 指向不可变字符串的指针 | |
| `if (x > 0) { ... } else { ... }` | 条件判断 | |
| `for (int i=0; i<10; i++) { ... }` | 循环 10 次 | |
| `while (1) { ... }` | 无限循环 | |
| `func(a, b);` | 调用函数 | |
| `return x;` | 函数返回值 | |
| `{0}` | 全部初始化成 0 | `int arr[10] = {0};` |
| `struct { int a; } x;` | 定义结构体 | `x.a = 5;` |
| `X ? A : B` | 三元运算符（如果 X 真则 A，否则 B） | |
| `&` `\|` `~` `<<` `>>` | 位运算（与/或/非/左移/右移） | |
| `&&` `\|\|` `!` | 逻辑运算（且/或/非） | |
| `==` `!=` `<` `>` `<=` `>=` | 比较运算 | |
| `++` `--` | 自增/自减 | `i++` 等同于 `i = i + 1` |
| `+=` `-=` `*=` `/=` | 复合赋值 | `x += 5` 等同于 `x = x + 5` |

---

> **恭喜！** 如果你一路跟着做下来，你已经完成了自己的第一个嵌入式项目。虽然中间可能遇到了一些问题，但这就是嵌入式开发的日常——遇到问题、排查、解决。你学到的这些概念（FreeRTOS 任务、I2S、I2C、GPIO、HTTP、音频解码）是大多数嵌入式项目的基础，以后做其他项目也能用上。
