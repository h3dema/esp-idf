# ESP32-S3 Wi-Fi Connection Test

A simple ESP-IDF application for the ESP32-S3 that connects to a Wi-Fi access point using WPA2-PSK and, after obtaining an IP address through DHCP, tests connectivity by pinging the network gateway.

## Requirements

The project requires:

* ESP32-S3 development board
* ESP-IDF
* USB connection between the ESP32-S3 and the host computer
* Wi-Fi access point configured for WPA2-PSK

## Project Structure

```text
simple_wifi_connect/
├── CMakeLists.txt
├── README.md
└── main/
    ├── CMakeLists.txt
    └── simple_wifi_connect.c
```

## Configure Wi-Fi Credentials

Edit `main/simple_wifi_connect.c` and set the Wi-Fi SSID and password:

```c
#define WIFI_SSID     "YOUR_SSID"
#define WIFI_PASSWORD "YOUR_PASSWORD"
```

The application uses station mode and requires WPA2-PSK authentication.

## Select the ESP32-S3 Target

From the project directory, set the ESP-IDF target to ESP32-S3:

```bash
idf.py set-target ESP32-S3
```

This only needs to be done once for the project unless the target is changed.

You can verify the selected target with:

```bash
idf.py get-target
```


## Compile

Build the project with:

```bash
idf.py build
```

If the project was previously built for another target or with an incompatible configuration, perform a clean build:

```bash
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

## Flash the ESP32-S3

Connect the ESP32-S3 to the host computer and identify the serial port.

For example, on Linux it may appear as:

```text
/dev/ttyACM0
```

Flash the application with:

```bash
idf.py -p /dev/ttyACM0 flash
```

Replace `/dev/ttyACM0` with the appropriate serial port for your system.

If the port is automatically detected, you can also use:

```bash
idf.py flash
```

## Flash and Monitor

The application can be flashed and the serial monitor started in a single command:

```bash
idf.py flash monitor
```

You should see output similar to:

```text
I (...) wifi: Wi-Fi initialization completed.
I (...) wifi: Connecting to AP...
I (...) wifi: Connected!
I (...) wifi: IP address: 192.168.1.100
I (...) wifi: Gateway: 192.168.1.1
```

The application then sends ICMP echo requests to the gateway.

## Exit the Serial Monitor

To exit `idf.py monitor`, press:

```text
Ctrl+]
```

This exits the monitor without resetting or stopping the ESP32 application.

## Expected Operation

After boot, the ESP32-S3 initializes the Wi-Fi stack and connects to the configured WPA2 access point.

Once DHCP provides an IP configuration, the application obtains the network gateway address. The gateway is then used as the destination for ICMP ping requests.

The expected sequence is:

```text
ESP32-S3
   │
   │ WPA2-PSK
   ▼
Wi-Fi Access Point
   │
   │ DHCP
   ▼
IP address + Gateway
   │
   │ ICMP Echo Request
   ▼
Gateway
```

The number of ping requests can be configured in the source code through the ping configuration.
