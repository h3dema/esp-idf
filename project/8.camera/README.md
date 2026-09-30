# Use of camera and save to file

- Xiao ESP32-S3
- OV3660 Camera Module

Camera slot circuit design for expansion boards
The XIAO ESP32S3 Sense card slot occupies 14 GPIOs of the ESP32-S3, and the pin details of the occupancy are shown in the table below.

| ESP32-S3 GPIO | Camera   | ESP32-S3 GPIO | Camera    |
|---------------|----------|---------------|-----------|
| GPIO10        | XMCLK    | GPIO11        | DVP_Y8    |
| GPIO12        | DVP_Y7   | GPIO13        | DVP_PCLK  |
| GPIO14        | DVP_Y6   | GPIO15        | DVP_Y2    |
| GPIO16        | DVP_Y5   | GPIO17        | DVP_Y3    |
| GPIO18        | DVP_Y4   | GPIO38        | DVP_VSYNC |
| GPIO39        | CAM_SCL  | GPIO40        | CAM_SDA   |
| GPIO47        | DVP_HREF | GPIO48        | DVP_Y9    |
