.PHONY: test demo build
# Docker provides the same Linux toolchain on macOS and Linux.
test:
	./scripts/test-in-container.sh
demo:
	./scripts/test-in-container.sh demo
# Native/incremental build: run this target inside the Linux development image.
build:
	python3 scripts/build-firmware.py
	./scripts/build-qemu.sh
