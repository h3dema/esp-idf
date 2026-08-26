# USB devices from a Docker container running on WSL2

#### 1. Install `usbipd-win` on Windows
First, you need to install the `usbipd-win` tool on your Windows host. This tool allows Windows to share USB devices with WSL2.

*   **Recommended method:** Open PowerShell as Administrator and run `winget install usbipd` .
*   **Alternative:** Download and install the latest `.msi` file from the project's releases page on GitHub .

#### 2. Attach Your ESP32 to WSL2
Now, with your ESP32 development board connected to your PC:

1.  **List USB Devices:** In an **Administrator** PowerShell window, run:
    ```bash
    usbipd list
    ```
    You will see a list of all connected USB devices. Note the `BUSID` for your ESP32 (e.g., `1-1` or `2-1`) .
2.  **Bind the Device:** Share the device with WSL by binding it :
    ```bash
    usbipd bind --busid <your_busid>
    ```
    (Replace `<your_busid>` with the one you noted, e.g., `1-1`).

3.  **Attach the Device to WSL:** Attach the bound device to WSL :
    ```bash
    usbipd attach --wsl --busid <your_busid>
    ```
    **A critical point for Docker Desktop users:** By default, this may attach the device to the special `docker-desktop` WSL distribution. This distribution is minimal and often lacks features like `systemd` or full `udev` support, which can lead to the device not being correctly recognized or given the proper permissions (e.g., it might show up as `root:root` instead of `root:dialout`) . **A more reliable approach is to attach the device to a full-featured WSL distribution you use (like Ubuntu) .**
    To do this, specify the distribution:
    ```bash
    usbipd attach --wsl --distribution Ubuntu --busid <your_busid>
    ```
    *(Replace `Ubuntu` with the name of your WSL distro. To see your distros, run `wsl -l` in a Windows terminal).*

#### 3. Find the Device in WSL
Now, start your WSL distribution and verify that the device is visible.

1.  Run `lsusb` in your WSL terminal. You should see your ESP32 device listed (e.g., `ID 303a:1001 Espressif USB JTAG/serial debug unit`) .
2.  The device will usually appear as `/dev/ttyUSB0` or `/dev/ttyACM0`. You can find the exact name by running `ls /dev/ttyUSB*` or `dmesg | grep tty` .

#### 4. Update Your `docker-compose.yml`
Now that the device is available in WSL, you can pass it through to your ESP-IDF Docker container.

In your `docker-compose.yml`, under the `services` section for your ESP-IDF service, add the `devices` key:

```yaml
services:
  esp-idf:
    build:
      context: .
      dockerfile: Dockerfile
    volumes:
      - ./project:/project
    working_dir: /project
    devices:
      - "/dev/ttyUSB0:/dev/ttyUSB0" # Maps the host's ttyUSB0 to the container's ttyUSB0
    # ... the rest of your configuration
```

#### 5. Verify Inside Your Container
Start your container and verify the device is mounted correctly:

```bash
docker-compose run --rm esp-idf ls -l /dev/ttyUSB0
```

A successful output will look something like:
```bash
crw-rw----    1 root     dialout   188,   0 Sep 21 12:00 /dev/ttyUSB0
```
This confirms the device is available and has the correct permissions for the `dialout` group .

### Important Caveats

1.  **Persistence:** The `usbipd bind` command is persistent across reboots, but `usbipd attach` is not. You will need to run the `attach` command again every time you restart your computer or reconnect the device .
2.  **Auto-attach Option:** You can use `usbipd attach --wsl --busid <your_busid> --auto-attach` to have the device automatically reattach when it's plugged in .

### Troubleshooting: Device Not Available

If your device doesn't appear in the container (especially if you attached to `docker-desktop`), a common fix is to ensure the kernel modules for your device are loaded inside the container. For ESP32 boards, this usually means adding the `cp210x` module, although the exact module might differ (e.g., `ch341`) .

You can add this to your `Dockerfile` to ensure it's loaded in the container :

```dockerfile
# In your Dockerfile
RUN echo "cp210x" | tee -a /etc/modules
```

However, this is more of a workaround. The most robust solution is to **use a full-featured WSL distribution** for your development work and attach the USB device to that distribution .