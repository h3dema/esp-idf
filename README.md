# ESP-IDF in Docker

```text
repo/
├── .env                    # Environment variables
├── .dockerignore           # Optional: exclude files from build context
├── Dockerfile              # Dockerfile in root
├── docker-compose.yml      # Compose file in root
├── Makefile                # commands with `make`
└── project/                # Your ESP-IDF project is here
    ├── main/
    │   └── main.c
    ├── CMakeLists.txt
    ├── sdkconfig          # Generated
    └── build/             # Generated
```

You can use the following commands to build and flash the project:

```bash
make build      # Build project
make menuconfig # Open menuconfig
make flash      # Flash to device
make dev        # Open interactive shell
```

### Important Notes
- The project must exist: Make sure ./project/ exists and contains a valid ESP-IDF project before running commands
- Path references: Inside the container, everything works from /project, but on the host, it's in ./project/
- Permissions: If you have permission issues with generated files, you can add user: ${UID:-1000} to your docker-compose services
- USB devices: For flashing, uncomment the devices section and ensure your user has permissions to access /dev/ttyUSB*


## Initialize a new project

1. Create the project directory
```bash
mkdir -p project
```

2. Initialize a new ESP-IDF project
```bash
docker-compose run --rm esp-idf idf.py create-project project_name
```

```text
# This creates project_name/ inside your project/ directory
# Your structure becomes:
# your-esp-project/
#   └── project/
#       └── project_name/
#           ├── main/
#           └── CMakeLists.txt
```

Or you can create it manually.


## Extra commands

```bash
# Build
docker-compose run --rm esp-idf idf.py build

# Interactive shell
docker-compose run --rm esp-idf bash

# Flash firmware (with USB access)
docker-compose run --rm esp-idf idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
docker-compose run --rm esp-idf idf.py -p /dev/ttyUSB0 monitor

# Menuconfig
docker-compose run --rm esp-idf idf.py menuconfig

# Clean project
docker-compose run --rm esp-idf idf.py clean

# Full clean
docker-compose run --rm esp-idf idf.py fullclean

# Set target (e.g., ESP32, ESP32-S3, ESP32-C3)
docker-compose run --rm esp-idf idf.py set-target esp32s3
```
