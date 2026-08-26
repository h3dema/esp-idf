# Makefile
.PHONY: build menuconfig flash clean monitor dev shell

build:
	docker-compose run --rm esp-idf idf.py build

menuconfig:
	docker-compose run --rm esp-idf idf.py menuconfig

flash:
	docker-compose run --rm esp-idf-flash

monitor:
	docker-compose run --rm esp-idf-flash idf.py -p /dev/ttyUSB0 monitor

clean:
	docker-compose run --rm esp-idf idf.py clean

fullclean:
	docker-compose run --rm esp-idf idf.py fullclean

dev:
	docker-compose run --rm esp-idf-dev

shell:
	docker-compose exec esp-idf bash
