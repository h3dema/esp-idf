## For ESP32-S3

The configuration is

| GPIO | MicroSD SPI Function |
|---|---|
| D10 / A10 / Qt9 / GPIO9 | MicroSD SPI MOSI |
| D9 / A9 / Qt8 / GPIO8 | MicroSD SPI MISO |
| D8 / A8 / Qt7 / GPIO7 | MicroSD SPI SCK |
| GPIO 21 | MicroSD SPI CS |

## Code

Our code will mount the SD card as `/sdcard`.
It will try to list all the .bmp images in `/sdcard/images`.
