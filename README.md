<!--
 * @Author: lingxiao
 * @Date: 2026-06-04 20:55:28
 * @LastEditTime: 2026-06-06 15:08:04
 * @FilePath: \awesome-esp32-s31\README.md
 * @Description: 
 * 
 * Copyright (c) 2026 by ${git_name_email}, All Rights Reserved. 
-->
# Awesome-ESP32-S31 🚀
> 乐鑫ESP32-S31-Korvo开发板资源仓库｜保姆级入门教程 + 持续更新趣味嵌入式小项目
[![MIT License](https://img.shields.io/badge/license-MIT-green.svg)](./LICENSE)

## 📌 仓库介绍
本仓库基于 **ESP32-S31-Korvo V1.1多媒体开发板** 打造，主打**零基础友好+实用趣味项目**：从环境搭建入门教程，到灯光、屏幕、语音、摄像头、物联网各类创意小Demo持续更新；依托ESP-IDF  + 官方BSP开发，所有代码均可一键编译烧录。

硬件基础：ESP32-S31(RISC-V双核320MHz)+16MB PSRAM+8~32MB Flash，板载4.3寸RGB触摸屏、OV3660摄像头、双麦克风、扬声器、可编程RGB灯、SD卡槽，天然适配多媒体、语音AI、机器视觉类创意项目。

## 📂 仓库目录结构（提前规划，方便后续不断新增项目）
awesome-esp32-s31
├── assets/
│ └── tutorial-images/ # 教程、各项目配图、硬件实拍图
├── docs/
│ └── 01-esp32s31-korvo-guide.md # 入门保姆教程（环境 + 屏幕点亮基础）
├── examples/ # 所有小项目源码分区存放
│ ├── 01_basic_demo/ # 基础入门小例程（灯光、按键、屏幕基础）
│ ├── 02_lcd_lvgl_demo/ # LVGL 屏幕交互类项目（动图、UI 控制面板）
│ ├── 
├── docs/extra_project_doc/ # 每个新项目配套独立说明文档
├── LICENSE
└── README.md


## 📖 入门文档快速跳转
👉 [零基础入坑指南｜ESP-IDF+VSCode+AI编程保姆教程](./docs/01-esp32s31-korvo-guide.md)
> 教程包含：环境一键部署、RGB屏幕点亮、触摸驱动、LVGL开发、AI辅助编程全流程，新手必看。

## 🎯 已上线项目清单
> 点击项目名称跳转对应源码目录，附带项目效果图与使用说明
### ✅ 基础&屏幕类
1. **RGB三色呼吸灯**｜路径：`examples/01_basic_demo/rgb_breath_led`
2. **LVGL分屏控制器：GIF动图播放 + 触摸按键控RGB灯**｜路径：`examples/02_lcd_lvgl_demo/lvgl_gif_led_ctrl`
   ![屏幕点亮测试界面](/assets/tutorial-images/screen_display1.png)

## ⏳ 正在开发 & 后续规划趣味项目（预留空位，做完即补充）
### 🎤 语音音频系列
- [ ] 项目1：双麦语音唤醒 + 语音指令控制板载灯光
- [ ] 项目2：便携录音播放器（录音保存到SD卡、本地回放）
- [ ] 项目3：简易变声器（实时拾音变调+扬声器外放）

### 📷 摄像头视觉系列
- [ ] 项目1：触屏一键拍照，JPG图片自动存入MicroSD卡
- [ ] 项目2：屏幕实时取景器（LCD实时显示摄像头画面）
- [ ] 项目3：简易人脸检测（依托S31内置AI加速）

### 🌐 Wi-Fi物联网系列
- [ ] 项目1：手机网页远程控制RGB灯光 + 屏幕在线改字
- [ ] 项目2：环境数据采集（外接温湿度），数据上报云端仪表盘
- [ ] 项目3：简易无线投屏（电脑图片下发，LCD屏幕显示）

### 🎮 趣味创意小项目
- [ ] 项目1：LVGL触屏小游戏（贪吃蛇/打地鼠）
- [ ] 项目2：桌面电子相册（SD卡读取图片自动轮播）
- [ ] 项目3：迷你频谱仪（麦克风拾音，屏幕绘制音频频谱）

###  边缘计算系列
- [ ] 项目1：边缘计算物联网设备（摄像头+麦克风+扬声器）
- [ ] 项目2：针对传感器数据的边缘计算


## ⚙️ 统一开发环境规范
全仓库项目统一开发配置，所有Demo通用：
| 工具 | 选型说明 |
| ---- | ---- |
| 编译框架 | ESP-IDF Master分支（EIM工具一键安装） |
| 开发IDE | VSCode + CodeBuddy(DeepSeek V4 Pro AI辅助开发)  /claude code+deepseek v4 flash          |
| UI库 | LVGL v8.4.0 + esp_lvgl_port v2.8.0 |
| 硬件驱动 | 乐鑫官方ESP32 BSP |

## 🚀 项目快速运行步骤
1. 克隆仓库到本地
```bash
git clone https://github.com/yunxiao11xie/awesome-esp32-s31.git
```



参照docs入门文档搭建 ESP-IDF 开发环境
进入任意项目文件夹，idf.py set-target esp32s31配置芯片，编译 + 烧录
🤝 仓库维护与共建
我会持续更新上述规划的趣味小项目，每周不定时新增 Demo；
欢迎提 Issues 提出想要实现的新项目创意；
有优化 / 自研项目可提交 PR 并入仓库。
📄 开源协议
本仓库全部代码与文档采用 MIT License 开源，可自由商用、二次开发，详见 LICENSE
