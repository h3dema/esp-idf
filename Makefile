# Variables
DOCKER_COMPOSE ?= docker-compose
RUN            := $(DOCKER_COMPOSE) run --rm
PORT           ?= /dev/ttyACM0

# Set 'help' as the default target when running 'make' with no arguments
.DEFAULT_GOAL := help

.PHONY: help docker-build build menuconfig flash monitor clean fullclean dev shell arduino

## help: Display this list of available commands
help:
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@sed -n 's/^## //p' $(MAKEFILE_LIST) | column -t -s ':' | sed -e 's/^/  /'

## docker-build: Build or rebuild the Docker services defined in docker-compose.yml
docker-build:
	$(DOCKER_COMPOSE) build

## build: Build the ESP-IDF project
build:
	$(RUN) esp-idf idf.py build

## menuconfig: Run the interactive menuconfig tool
menuconfig:
	$(RUN) esp-idf idf.py menuconfig

## flash: Flash the firmware to the device
flash:
	$(RUN) esp-idf-flash

## monitor: Open the serial monitor (default PORT=/dev/ttyACM0)
monitor:
	$(RUN) esp-idf-flash idf.py -p $(PORT) monitor

## clean: Clean build files
clean:
	$(RUN) esp-idf idf.py clean

## fullclean: Perform a full clean of the build directory
fullclean:
	$(RUN) esp-idf idf.py fullclean

## dev: Run the development container environment
dev:
	$(RUN) esp-idf-dev

## shell: Open an interactive bash shell in a running esp-idf container
shell:
	$(DOCKER_COMPOSE) exec esp-idf bash

## arduino: Run the Arduino container environment
arduino:
	$(RUN) esp-idf-arduino
