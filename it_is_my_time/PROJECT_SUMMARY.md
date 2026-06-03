# ESP32-S3 WiFi 网络音乐播放器 — 项目总结

## 项目概述

用 ESP32-S3 开发板做了一个能连 WiFi 播放网络音乐的设备，带 OLED 屏幕显示状态，两个实体按键调音量。

```
┌──────────────────────────────────────────┐
│              最终效果                     │
│                                          │
│  ┌──────────────────┐  ┌──────────────┐ │
│  │   SSD1306 OLED   │  │              │ │
│  │ Vol:  50%        │  │   ESP32-S3   │ │
│  │ ########-------- │  │   开发板     │ │
│  │                  │  │              │ │
│  │  Streaming...    │  │  [按键+][按键-]│ │
│  │                  │  │              │ │
│  │  WiFi: Connected │  │  喇叭 ← I2S  │ │
│  └──────────────────┘  └──────────────┘ │
└──────────────────────────────────────────┘
```

---

## 目录结构

```
it_is_my_time/
├── main/
│   ├── CMakeLists.txt        # 主程序编译配置
│   └── main.c                # 入口：调用 audio_player_start()
├── components/
│   ├── audio_player/
│   │   ├── CMakeLists.txt    # 组件编译配置（依赖列表）
│   │   ├── idf_component.yml # ESP-IDF 组件管理器配置
│   │   ├── include/
│   │   │   └── audio_player.h # 公开接口
│   │   └── audio_player.c    # 主逻辑（~600行）
│   └── display/
│       ├── CMakeLists.txt
│       ├── include/
│       │   └── display.h     # 屏幕接口
│       └── display.c         # SSD1306 驱动 + 8x8 字体
├── CMakeLists.txt            # 项目级编译配置
├── sdkconfig                 # ESP-IDF 配置
└── PROJECT_SUMMARY.md        # 你正在看的这个文件
```

---

## 功能清单

| 功能 | 说明 |
|------|------|
| WiFi 联网 | 自动连接，断线重连 |
| HTTP 流媒体 | 从 Navidrome 音乐服务器获取音频流 |
| 音频解码 | 自动识别 MP3 / AAC / FLAC / OGG / WAV / M4A |
| I2S 输出 | 直接驱动 MAX98357 等 I2S 功放模块 |
| OLED 显示 | SSD1306 128x64，显示音量、进度条、状态 |
| 按键调音量 | 短按 ±5%，长按连续调 |

---

## 硬件接线

### I2S 音频输出
| ESP32-S3 | 功放模块 |
|----------|---------|
| GPIO 5 (BCLK) | BCLK |
| GPIO 4 (LRCK) | LRCK / WS |
| GPIO 6 (DOUT) | DIN |
| GND | GND |
| 3.3V / 5V | VIN |

### SSD1306 OLED 屏幕 (I2C)
| ESP32-S3 | SSD1306 |
|----------|---------|
| GPIO 1 (SCL) | SCL |
| GPIO 2 (SDA) | SDA |
| 3.3V | VCC |
| GND | GND |

### 音量按键
| ESP32-S3 | 按键 |
|----------|------|
| GPIO 7 | 上方按键（音量+）→ GND |
| GPIO 13 | 下方按键（音量-）→ GND |

---

## 用到的知识点

### 1. ESP-IDF 项目结构

每个 ESP-IDF 项目就是一个 CMake 项目。组件（components）是代码的组织单元。

**项目级 `CMakeLists.txt`：**
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(00_basic)
```
- `include(...project.cmake)` 是固定写法，引入 ESP-IDF 的构建系统
- `project(名字)` 定义项目名

**组件级 `CMakeLists.txt`（以 audio_player 为例）：**
```cmake
idf_component_register(
    SRCS "audio_player.c"          # 源文件
    INCLUDE_DIRS "include"          # 头文件目录
    PRIV_REQUIRES esp_wifi esp_netif esp_http_client ...  # 依赖的其他组件
)
```
- `SRCS`：这个组件包含哪些 .c 文件
- `INCLUDE_DIRS`：头文件在哪个目录
- `PRIV_REQUIRES`：这个组件内部用到了哪些 ESP-IDF 自带的组件

**组件管理器配置 `idf_component.yml`：**
```yaml
dependencies:
  idf:
    version: '>=4.1.0'
  espressif/esp_audio_codec: ^2.5.0   # 第三方音频解码库
```
- 类似 Node.js 的 `package.json`，自动下载依赖

### 2. C 语言基础语法

**头文件保护（`#pragma once`）：**
```c
#pragma once       // 防止同一个头文件被重复 include
void audio_player_start(void);
void audio_player_set_volume(int volume);
```
等价于传统的：
```c
#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H
// ...
#endif
```

**静态全局变量（`static`）：**
```c
static int s_volume = 5;   // 只在当前 .c 文件内可见，其他文件看不到
```
- `static` 在全局变量前：变量只在这个文件里有效
- `static` 在函数前：函数只在这个文件里有效（私有函数）
- `s_` 前缀是命名习惯，表示 static

**结构体（`typedef struct`）：**
```c
typedef struct {
    char content_type[64];   // 字符数组，存 64 个字符
} http_ctx_t;                // http_ctx_t 是这个结构体的类型名
```
使用：
```c
http_ctx_t ctx = {0};        // 创建变量，{0} 表示全部初始化为 0
ctx.content_type[0] = 'a';   // 访问成员用点号
```

**宏定义（`#define`）：**
```c
#define BTN_UP_GPIO  7       // 编译前会把所有 BTN_UP_GPIO 替换成 7
#define HTTP_READ_SIZE 2048  // 用大写 + 下划线命名
```

**枚举（`enum`）：**
```c
typedef enum {
    ESP_AUDIO_SIMPLE_DEC_TYPE_NONE = 0,  // 可以指定值，也可以不指定
    ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    ESP_AUDIO_SIMPLE_DEC_TYPE_AAC,
} esp_audio_simple_dec_type_t;
```
等价于定义了一组命名常量，比 `#define` 更安全（有类型检查）。

**格式化字符串（`snprintf`）：**
```c
char buf[17];                                 // 字符数组，16 个字符 + 1 个 '\0'
snprintf(buf, sizeof(buf), "Vol: %3d%%", 50); // buf = "Vol:  50%"
//        ↑          ↑         ↑         ↑
//        目标数组    最大长度   格式串     参数
// %3d = 占 3 位的整数
// %%  = 输出一个 % 符号
```

**内存操作：**
```c
memcmp(data, "ID3", 3);  // 比较内存中前 3 个字节是否等于 "ID3"
strncpy(dst, src, 64);   // 复制字符串，最多 64 字节
strcasecmp(a, b);        // 忽略大小写比较字符串
malloc(2048);            // 从堆上分配 2048 字节内存
free(ptr);               // 释放之前分配的内存
realloc(ptr, new_size);  // 调整已分配内存的大小
```

### 3. FreeRTOS 实时操作系统

ESP32 跑的是 FreeRTOS，一个轻量级实时操作系统。核心概念：

**任务（Task）：**
```c
// 创建一个任务
xTaskCreate(
    audio_stream_task,   // 任务函数名
    "audio_stream",      // 任务名称（调试用）
    16384,               // 栈大小（字节），越大越安全但占内存
    NULL,                // 传给任务的参数
    20,                  // 优先级（数字越大优先级越高）
    NULL                 // 任务句柄（不需要可传 NULL）
);
```

任务的本质：一个独立的无限循环函数，FreeRTOS 会在多个任务之间快速切换，看起来像同时运行。
```c
void my_task(void *arg) {
    while (1) {
        // 做事情...
        vTaskDelay(pdMS_TO_TICKS(100));  // 休眠 100 毫秒，让出 CPU
    }
}
```

**任务通知（Task Notification）：**
```c
// 发送通知（在 button_task 中）
xTaskNotifyGive(s_disp_task);

// 等待通知（在 display_update_task 中）
ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
//                 ↑       ↑
//          收到后清零    超时时间（没收到就继续往下走）
```
任务通知是 FreeRTOS 里最快、最轻量的任务间通信方式。

**互斥锁（Mutex）：**
```c
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

xSemaphoreTake(mutex, portMAX_DELAY);    // 上锁（一直等到成功）
// ... 临界区代码（同时只有一个任务能执行）...
xSemaphoreGive(mutex);                   // 解锁
```
Mutex 用来防止多个任务同时操作同一个硬件（比如 I2C 总线）。

**事件组（Event Group）：**
```c
EventGroupHandle_t group = xEventGroupCreate();

// 在别的地方等待某个事件
xEventGroupWaitBits(group, BIT0, pdFALSE, pdTRUE, portMAX_DELAY);

// 在中断回调里设置事件
xEventGroupSetBits(group, BIT0);
```
这里用来等待 WiFi 连接成功。

### 4. I2S 音频输出

I2S（Inter-IC Sound）是传输数字音频的标准协议，3 根线：
- **BCLK**（Bit Clock）：每一位数据的时钟
- **LRCK / WS**（Word Select）：区分左右声道，高低电平交替
- **DOUT**（Data Out）：实际的音频数据

关键配置：
```c
i2s_chan_config_t chan_cfg = {
    .id = I2S_NUM_0,          // 使用 I2S0 控制器
    .role = I2S_ROLE_MASTER,  // ESP32 做主设备（产生时钟）
    .dma_desc_num = 8,        // DMA 描述符数量（越大缓冲区越大）
    .dma_frame_num = 480,     // 每个描述符的采样帧数
    .auto_clear = false,
};
```

DMA（Direct Memory Access）：硬件自动从内存搬运数据到 I2S，不需要 CPU 逐个字节搬运。DMA 缓冲区越大，短时间断流时（比如 WiFi 卡了一下）越不容易出现噪音。

**PCM 数据格式：**
- 16-bit signed integer（`int16_t`），范围 -32768 ~ 32767
- 立体声：L 左声道 R 右声道交替排列 `[L, R, L, R, ...]`
- 单声道：只有 `[M, M, M, ...]`，需要复制一份变成伪立体声

### 5. I2C 通信（SSD1306 OLED）

I2C 是两线串行总线：
- **SCL**（时钟线）
- **SDA**（数据线）

```c
// 初始化 I2C 总线
i2c_master_bus_config_t bus_cfg = {
    .i2c_port = I2C_NUM_1,          // 使用 I2C1 控制器
    .sda_io_num = 2,                // SDA 接 GPIO 2
    .scl_io_num = 1,                // SCL 接 GPIO 1
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .flags = { .enable_internal_pullup = true },  // 启用芯片内部上拉电阻
};

// 发送数据给 SSD1306
i2c_master_transmit(device_handle, data, len, timeout_ms);
```

SSD1306 的控制方式：
- 发送 0x00 开头的字节 = 命令（如"翻到第 3 页"）
- 发送 0x40 开头的字节 = 数据（如"把这一页的 128 列设成以下图案"）
- 屏幕 128×64 被分成 8 个"页"（Page），每页 8 像素高

### 6. GPIO 按键输入

```c
gpio_config_t io_cfg = {
    .pin_bit_mask = (1ULL << 7) | (1ULL << 13),  // GPIO 7 和 GPIO 13
    .mode = GPIO_MODE_INPUT,          // 输入模式
    .pull_up_en = GPIO_PULLUP_ENABLE, // 启用内部上拉（不按时 = 高电平）
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,   // 不用中断，用轮询
};
gpio_config(&io_cfg);

// 读取按键状态
int pressed = (gpio_get_level(7) == 0);  // 低电平 = 按下
```

**消抖（Debounce）：**
机械按键按下时信号会抖动（快速通断多次），需要用软件消抖：
```c
// 每 30ms 检查一次，抖动期间状态变化会被忽略
vTaskDelay(pdMS_TO_TICKS(30));
```

**长按检测：**
```c
if (gpio_get_level(7) == 0) {  // 按下
    hold_counter++;              // 累计按了多久
    if (hold_counter >= 400/30) { // 超过 400ms = 长按
        // 每 80ms 触发一次连续调节
    }
} else {
    hold_counter = 0;  // 松开就清零
}
```

### 7. WiFi + HTTP 流媒体

```c
// HTTP 客户端配置
esp_http_client_config_t config = {
    .url = "https://music.example.com/stream?id=xxx",
    .timeout_ms = 10000,             // 超时 10 秒
    .buffer_size = 2048,             // 接收缓冲区大小
    .crt_bundle_attach = esp_crt_bundle_attach,  // HTTPS 证书验证
};
esp_http_client_handle_t client = esp_http_client_init(&config);
esp_http_client_open(client, 0);     // 发起连接

// 循环读取数据
int read = esp_http_client_read(client, buffer, 2048);
```

### 8. 音频解码流程

```
HTTP 流数据 → 检测格式 → 打开对应解码器 → 解码 → PCM → I2S 输出
```

**格式检测（从 Content-Type 头或文件魔数）：**
- `audio/mpeg` 或前 2 字节 `FF Ex` → MP3
- `audio/aac` → AAC
- `audio/flac` 或前 4 字节 `fLaC` → FLAC
- `OggS` → OGG Vorbis
- `RIFF....WAVE` → WAV
- `....ftyp` → M4A

**解码循环：**
```c
while (还有 HTTP 数据) {
    raw.buffer = 收到的压缩数据;
    raw.len    = 数据长度;
    
    while (raw 还没消费完) {
        esp_audio_simple_dec_process(解码器, &raw, &out);
        // out.buffer 里就是解码后的 PCM 数据
        // out.decoded_size 是字节数
        
        写入 I2S(out.buffer, out.decoded_size);
    }
}
```

---

## 踩过的坑

### 坑 1：ESP32 没有硬件浮点单元，浮点运算是性能杀手

**症状：** 加了音量控制后音质变差、有杂音、像慢速播放。

**原因：**
```c
// 错误做法 —— 每个采样都做浮点乘法
s_stereo_buf[i] = (int16_t)((float)src[i] * s_volume);  // s_volume 是 float
```
ESP32-S3 没有 FPU（浮点运算单元），`float` 乘法要靠软件模拟，非常慢。44.1kHz 立体声每秒 88,200 个采样，每个做浮点运算 → CPU 跟不上 → I2S 缓冲区欠载 → 杂音。

**正确做法：** 用整数运算
```c
// s_volume 是 int，范围 0-100
buf[i] = (int16_t)((int32_t)buf[i] * vol / 100);
```
整数乘除是硬件指令，极快。

**教训：** 嵌入式开发尽量用整数，避免浮点。

### 坑 2：ESP32-S3 没有 APLL 时钟源

**症状：** 编译报错 `I2S_CLK_SRC_APLL undeclared`。

**原因：** APLL（音频专用 PLL）只在 ESP32 和 ESP32-S2 上有。ESP32-S3 没有。

**正确做法：** 使用默认时钟源 `I2S_STD_CLK_DEFAULT_CONFIG(sample_rate)`，ESP-IDF 的 I2S 驱动内部用小数分频器，精度足够。

**教训：** 不同 ESP32 芯片的硬件功能不同，用 `#if` 等条件编译时要确认宏是否存在。

### 坑 3：GPIO 3 不能随便用

**症状：** 一按接在 GPIO 3 上的按键，整个板子就复位。

**原因：** ESP32-S3 开发板上 GPIO 3 直接连到了 USB 转串口芯片（CP2102/CH340）。按键一端接地，按下时 GPIO 3 被短路到 GND → USB 芯片断电 → 整板掉电复位。

**正确做法：** 避开以下 GPIO（都是开发板已经占用的）：
- GPIO 3：USB 串口 RX
- GPIO 0：Boot 按键
- GPIO 46：启动模式选择

用 GPIO 7、13 这类完全空闲的脚。

**教训：** 用 GPIO 之前先确认开发板原理图，不要用串口、Boot 等已占用的引脚。

### 坑 4：按键触发的深层调用链导致栈溢出

**症状：** 按键调音量时系统复位（和坑 3 一样的现象，但原因不同）。

**原因：** 调用链太深，多个函数各自在栈上分配大数组，超出了任务栈的大小限制。

```
button_task（栈 2048 字节）
  → audio_player_set_volume
    → display_set_volume      栈上分配 row[128]
      → display_set_line      栈上分配 row[128]
        → ssd1306_write_data  栈上分配 buf[129]
          → i2c_master_transmit   I2C 驱动内部栈消耗
```
叠加起来超过 2048 字节 → **栈溢出** → 写坏内存 → 芯片异常 → 复位。

**正确做法：架构分离——生产者-通知者模式**

```
button_task              只修改变量 → 发通知
audio_stream_task        只修改变量 → 发通知
                              ↓（任务通知，极轻量，不阻塞）
display_update_task      读变量 → 统一刷新屏幕（唯一做 I2C 的任务）
```

关键点：
- 所有 I2C 操作只在一个独立的低优先级任务里做
- 其他任务只改共享变量 + 发通知（`xTaskNotifyGive`）
- 栈隔离：display 任务的栈足够大（2560），其他任务栈可以小

**教训：** 
1. 一个任务只做一类事情（I2C 操作单独放一个任务）
2. 任务栈宁大勿小，尤其是里面调了需要大数组的函数
3. 不要为了让代码"看起来直接"而在一个函数里串做事——要分层

### 坑 5：C 语言必须先声明再使用

**症状：** 编译报错 `implicit declaration of function 'set_disp_status'`。

**原因：** `static` 函数定义在调用之后，C 编译器读到调用点时不认识这个函数名。

**正确做法：** 在文件顶部加前置声明：
```c
// 前置声明（告诉编译器"后面有这个函数"）
static void notify_display(void);
static void set_disp_status(const char *msg);

// ...其他函数可以调用了...

// 函数定义（具体实现）
static void set_disp_status(const char *msg) {
    strncpy(s_disp_status, msg, 16);
    notify_display();
}
```

**教训：** 在 C 文件顶部把 `static` 函数的声明集中写好，可以按调用顺序来组织函数定义，不用担心顺序问题。

### 坑 6：I2C 总线时序和上拉电阻

**知识点（没出 bug 但很重要）：** SSD1306 的 I2C 需要上拉电阻。代码里启用了芯片内部上拉：
```c
.flags = { .enable_internal_pullup = true }
```
但 ESP32 内部上拉约 45kΩ，比较弱。如果屏幕离得远或线很长，需要在 SCL 和 SDA 上各接一个 4.7kΩ 外接电阻到 3.3V。

**信号规则：**
- I2C 默认高电平（通过上拉电阻拉到 3.3V）
- 设备通信时拉低线路（靠驱动能力）
- 上拉太弱 → 信号边沿太慢 → 高速通信失败

---

## 编译 & 烧录命令

```bash
# 在项目根目录下

# 设置芯片型号（只需做一次）
idf.py set-target esp32s3

# 编译
idf.py build

# 烧录（把 COM7 换成你的实际端口）
idf.py -p COM7 flash

# 查看串口日志
idf.py -p COM7 monitor

# 退出日志：Ctrl + ]
```

---

## 可继续优化的方向

1. **音质**：用外部 I2S DAC（如 PCM5102）代替直接用 GPIO 输出
2. **歌单**：加上选歌功能（从 Navidrome API 获取歌单列表）
3. **网页控制**：ESP32 开一个 HTTP 服务器，手机浏览器控制
4. **NVS 存储**：把音量和 WiFi 密码存到 Flash，断电不丢失
5. **OTA 升级**：通过 WiFi 远程更新固件
6. **低功耗**：不播放时进入 Light Sleep 省电模式
