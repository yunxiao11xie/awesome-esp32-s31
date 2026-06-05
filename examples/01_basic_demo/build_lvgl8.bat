@echo off
chcp 65001 >nul 2>&1
set PYTHONUTF8=1
set IDF_TOOLS_PATH=E:\esp_idf\Espressif
set IDF_PATH=E:\esp_idf\Espressif\frameworks\esp-idf-master
set ESP_IDF_VERSION=6.2.0
set IDF_PYTHON_ENV_PATH=E:\esp_idf\Espressif\python_env\idf6.2_py3.11_env
set PATH=E:\esp_idf\Espressif\tools\cmake\4.0.3\bin;E:\esp_idf\Espressif\tools\ninja\1.12.1;E:\esp_idf\Espressif\tools\idf-exe\1.0.3;E:\esp_idf\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin;E:\esp_idf\Espressif\python_env\idf6.2_py3.11_env\Scripts;%PATH%
cd /d d:\ESP32_learning\esp32s31_2project
E:\esp_idf\Espressif\python_env\idf6.2_py3.11_env\Scripts\python.exe %IDF_PATH%\tools\idf.py fullclean 2>&1
echo === Fullclean done, now building ===
E:\esp_idf\Espressif\python_env\idf6.2_py3.11_env\Scripts\python.exe %IDF_PATH%\tools\idf.py build 2>&1 | findstr /i /c:"error" /c:"warning" /c:"Compiling" /c:"Linking" /c:"Complete" /c:"lvgl_port" /c:"tp_gt1151" /c:"gif_player" /c:"ui_smart" /c:"FAILED" /c:"ninja: build" /c:"Project build"
