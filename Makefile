CMAKE ?= cmake
BUILD_DIR ?= build
BUILD_TYPE ?= Debug
CMAKE_FLAGS ?=
BUILD_FLAGS ?=

.DEFAULT_GOAL := build

.PHONY: help configure build viewer test test-unit test-integration test-sandbox sandbox benchmark advanced agent-demo quality sanitizers thread-sanitizer clean distclean

help:
	@$(CMAKE) -E echo "ICAD developer targets:"
	@$(CMAKE) -E echo "  make build             Configure and compile (default)"
	@$(CMAKE) -E echo "  make viewer            Build optional webview/webview desktop host"
	@$(CMAKE) -E echo "  make test              Run every CTest test"
	@$(CMAKE) -E echo "  make test-unit         Run lexer/component tests"
	@$(CMAKE) -E echo "  make test-integration  Run CLI integration tests"
	@$(CMAKE) -E echo "  make test-sandbox      Run isolated-workspace tests"
	@$(CMAKE) -E echo "  make benchmark         Build and read back the advanced bridge"
	@$(CMAKE) -E echo "  make advanced          Emit complete STEP/STL/OBJ/glTF/3MF/DXF/viewer package"
	@$(CMAKE) -E echo "  make agent-demo        Create the detailed robot arm from one short prompt"
	@$(CMAKE) -E echo "  make quality           Werror tests plus sanitizer tests"
	@$(CMAKE) -E echo "  make sanitizers        Rebuild and test with ASan/UBSan"
	@$(CMAKE) -E echo "  make thread-sanitizer  Race-check the shared incremental compiler"
	@$(CMAKE) -E echo "  make clean             Remove compiled outputs"
	@$(CMAKE) -E echo "  make distclean         Remove the complete build tree"

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTING=ON $(CMAKE_FLAGS)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel $(BUILD_FLAGS)

viewer:
	$(MAKE) BUILD_DIR=$(BUILD_DIR) CMAKE_FLAGS="-DICAD_BUILD_DESKTOP_VIEWER=ON" build

test: build
	$(CMAKE) --build $(BUILD_DIR) --target test

test-unit: build
	$(CMAKE) --build $(BUILD_DIR) --target test-unit

test-integration: build
	$(CMAKE) --build $(BUILD_DIR) --target test-integration

test-sandbox sandbox: build
	$(CMAKE) --build $(BUILD_DIR) --target test-sandbox

benchmark: build
	$(CMAKE) --build $(BUILD_DIR) --target benchmark

advanced: build
	$(BUILD_DIR)/bin/icad build examples/advanced.icad --output-dir $(BUILD_DIR)/examples

agent-demo: build
	$(BUILD_DIR)/bin/icad agent-create "Create a detailed articulated industrial robotic arm with a gripper and animated joints" --source-out $(BUILD_DIR)/agent/robotic_arm.icad --output-dir $(BUILD_DIR)/agent/robotic_arm

quality:
	$(MAKE) BUILD_DIR=$(BUILD_DIR) CMAKE_FLAGS="-DICAD_WARNINGS_AS_ERRORS=ON -DICAD_ENABLE_SANITIZERS=OFF -DICAD_ENABLE_THREAD_SANITIZER=OFF" test
	$(MAKE) BUILD_DIR=$(BUILD_DIR) CMAKE_FLAGS="-DICAD_WARNINGS_AS_ERRORS=ON -DICAD_ENABLE_SANITIZERS=ON -DICAD_ENABLE_THREAD_SANITIZER=OFF" test
	$(MAKE) BUILD_DIR=$(BUILD_DIR) CMAKE_FLAGS="-DICAD_WARNINGS_AS_ERRORS=OFF -DICAD_ENABLE_SANITIZERS=OFF -DICAD_ENABLE_THREAD_SANITIZER=OFF" build

sanitizers:
	$(MAKE) BUILD_DIR=$(BUILD_DIR) CMAKE_FLAGS="-DICAD_WARNINGS_AS_ERRORS=OFF -DICAD_ENABLE_SANITIZERS=ON -DICAD_ENABLE_THREAD_SANITIZER=OFF" test
	$(MAKE) BUILD_DIR=$(BUILD_DIR) CMAKE_FLAGS="-DICAD_WARNINGS_AS_ERRORS=OFF -DICAD_ENABLE_SANITIZERS=OFF -DICAD_ENABLE_THREAD_SANITIZER=OFF" build

thread-sanitizer:
	$(MAKE) BUILD_DIR=$(BUILD_DIR) CMAKE_FLAGS="-DICAD_BUILD_DESKTOP_VIEWER=OFF -DICAD_WARNINGS_AS_ERRORS=ON -DICAD_ENABLE_SANITIZERS=OFF -DICAD_ENABLE_THREAD_SANITIZER=ON" build
	$(CMAKE) --build $(BUILD_DIR) --target icad_incremental_test --parallel $(BUILD_FLAGS)
	$(CMAKE) -E env CTEST_OUTPUT_ON_FAILURE=1 ctest --test-dir $(BUILD_DIR) -R '^unit.incremental$$'
	$(MAKE) BUILD_DIR=$(BUILD_DIR) CMAKE_FLAGS="-DICAD_BUILD_DESKTOP_VIEWER=ON -DICAD_WARNINGS_AS_ERRORS=OFF -DICAD_ENABLE_SANITIZERS=OFF -DICAD_ENABLE_THREAD_SANITIZER=OFF" build

clean:
	@if [ -f "$(BUILD_DIR)/CMakeCache.txt" ]; then \
		$(CMAKE) --build "$(BUILD_DIR)" --target clean; \
	fi

distclean:
	@case "$(BUILD_DIR)" in \
		build|build-[A-Za-z0-9._-]*) ;; \
		*) $(CMAKE) -E echo "refusing to remove unsafe BUILD_DIR: $(BUILD_DIR)"; exit 2 ;; \
	esac
	@if [ ! -f "$(BUILD_DIR)/CMakeCache.txt" ]; then \
		$(CMAKE) -E echo "refusing to remove an unconfigured build directory: $(BUILD_DIR)"; \
		exit 2; \
	fi
	$(CMAKE) -E remove_directory "$(BUILD_DIR)"
