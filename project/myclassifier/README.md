# Folder structure


```text
myclassifier/
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── access.cpp        // SD card + JPEG decoding
│   ├── access.h          // declarations for access.cpp
│   ├── classifier.cpp    // model loading + classification
│   └── classifier.h      // declarations for classifier.cpp
```

# My ES32-S3

```text
esptool v5.3.1
Connected to ESP32-S3 on /dev/ttyACM0:
Chip type:          ESP32-S3 (QFN56) (revision v0.2)
Features:           Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
Crystal frequency:  40MHz
USB mode:           USB-Serial/JTAG
MAC:                10:b4:1d:e9:fc:30
```