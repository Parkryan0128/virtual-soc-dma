.PHONY: test demo build
# Docker provides the same Linux toolchain on macOS and Linux.
test:
	./scripts/test-in-container.sh
demo:
	./scripts/run-demo.sh --docker
# Native/incremental build: run this target inside the Linux development image.
build:
	./scripts/build-firmware.sh
	./scripts/build-qemu.sh
