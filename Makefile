SHELL := /bin/sh
PROFILE ?= release
IMAGE := esp32-led-controller-builder:local
PORT ?= /dev/ttyUSB0

.PHONY: image cache-init build debug release lint test preview clean flash flashfs monitor

image:
	docker build -t $(IMAGE) .

cache-init: image
	@mkdir -p build-cache/platformio
	docker run --rm --user root --entrypoint sh \
		-v "$(CURDIR)/build-cache/platformio:/opt/platformio" $(IMAGE) -c \
		'chown -R $(shell id -u):$(shell id -g) /opt/platformio'

# `make build` defaults to release. Both `make build debug` and
# `make build PROFILE=debug` are supported.
build: cache-init
	@profile="$(PROFILE)"; \
	case "$(MAKECMDGOALS)" in *debug*) profile=debug;; *release*) profile=release;; esac; \
	docker run --rm -u "$$(id -u):$$(id -g)" \
		-v "$(CURDIR):/workspace" \
		-v "$(CURDIR)/build-cache/platformio:/opt/platformio" \
		-w /workspace $(IMAGE) run -e $$profile && \
	docker run --rm -u "$$(id -u):$$(id -g)" \
		-v "$(CURDIR):/workspace" \
		-v "$(CURDIR)/build-cache/platformio:/opt/platformio" \
		-w /workspace $(IMAGE) run -e $$profile -t buildfs

debug:
	@:

release:
	@:

lint:
	python3 tools/lint.py

test: cache-init
	docker run --rm -u "$$(id -u):$$(id -g)" \
		-v "$(CURDIR):/workspace" \
		-v "$(CURDIR)/build-cache/platformio:/opt/platformio" \
		--entrypoint sh -w /workspace $(IMAGE) -c \
		'python3 tests/test_web_assets.py && pio run -e debug && pio run -e debug -t buildfs'

preview:
	python3 tools/preview.py

flash: image
	docker run --rm -it --privileged --user root \
		-v "$(CURDIR):/workspace" \
		-v "$(CURDIR)/build-cache/platformio:/opt/platformio" \
		-v /dev:/dev -w /workspace $(IMAGE) run -e $(PROFILE) -t upload --upload-port $(PORT)

flashfs: image
	docker run --rm -it --privileged --user root \
		-v "$(CURDIR):/workspace" \
		-v "$(CURDIR)/build-cache/platformio:/opt/platformio" \
		-v /dev:/dev -w /workspace $(IMAGE) run -e $(PROFILE) -t uploadfs --upload-port $(PORT)

monitor: image
	docker run --rm -it --privileged --user root \
		-v /dev:/dev -w /workspace $(IMAGE) device monitor --port $(PORT) -b 115200

clean:
	rm -rf .pio build-cache/platformio
