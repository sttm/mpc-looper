.NOTPARALLEL:
SDK := ../../vendor/mpc-plugin-builder-sdk
FLAGS := -D_FILE_OFFSET_BITS=64 -std=c++17 -O2 -Wall -Wextra -Werror -pthread -isystem "$(SDK)/framework" -Isrc -isystem vendor/signalsmith-stretch -isystem vendor/signalsmith-linear/include
SOURCES := src/MPCLooper.cpp
HEADERS := src/HostTimeline.h src/DiskAudio.h src/DiskFiles.h src/AudioMemory.h src/TempoDSP.h vendor/signalsmith-stretch/signalsmith-stretch.h src/LoopEngine.h src/LoopFiles.h $(SDK)/framework/mpc_plugin.h
SHARED := -shared -fPIC -fvisibility=hidden -fvisibility-inlines-hidden
OUT := build/sttm - VST - MPC Looper
.PHONY: all native arm32 arm64 test skin package sanitize disk-sanitize
all: native arm32 arm64 test package
native: build/native/mpc-looper.so
build/native/mpc-looper.so: $(SOURCES) $(HEADERS)
	mkdir -p build/native
	$(CXX) $(FLAGS) $(SHARED) $(SOURCES) -o $@
arm32:
	mkdir -p "$(OUT)/VST"
	arm-linux-gnueabihf-g++ $(FLAGS) $(SHARED) -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -static-libstdc++ -static-libgcc -Wl,--no-undefined -Wl,--version-script="$(SDK)/tools/exports.map" -Wl,-s $(SOURCES) -o "$(OUT)/VST/mpc-looper-arm32.so"
arm64:
	mkdir -p "$(OUT)/VST"
	aarch64-linux-gnu-g++ $(FLAGS) $(SHARED) -march=armv8-a -static-libstdc++ -static-libgcc -Wl,--no-undefined -Wl,--version-script="$(SDK)/tools/exports.map" -Wl,-s $(SOURCES) -o "$(OUT)/VST/mpc-looper-arm64.so"
build/host_sim: $(SDK)/tools/host-sim/host_sim.cpp
	mkdir -p build
	$(CXX) -std=c++17 -O2 $< -ldl -pthread -o $@
build/core_test: tests/core_test.cpp $(HEADERS)
	mkdir -p build
	$(CXX) $(FLAGS) $< -o $@
build/tempo_test: tests/tempo_test.cpp $(HEADERS)
	mkdir -p build
	$(CXX) $(FLAGS) $< -o $@
build/integration_test: tests/integration_test.cpp $(SOURCES) $(HEADERS)
	mkdir -p build
	$(CXX) $(FLAGS) $< -o $@
build/pedal_test: tests/pedal_test.cpp $(HEADERS)
	mkdir -p build
	$(CXX) $(FLAGS) $< -o $@
build/memory_test: tests/memory_test.cpp $(HEADERS)
	mkdir -p build
	$(CXX) $(FLAGS) $< -o $@
build/disk_store_test: tests/disk_store_test.cpp $(HEADERS)
	mkdir -p build
	$(CXX) $(FLAGS) $< -o $@
build/disk_engine_test: tests/disk_engine_test.cpp $(HEADERS)
	mkdir -p build
	$(CXX) $(FLAGS) $< -o $@
test: native build/disk_store_test build/disk_engine_test build/host_sim build/core_test build/integration_test build/tempo_test build/pedal_test build/memory_test
	./build/core_test
	./build/disk_store_test
	./build/disk_engine_test
	./build/tempo_test
	./build/pedal_test
	./build/memory_test
	MPC_PLUGIN_FOLDER="$(CURDIR)/build/test-profile" ./build/integration_test
	./build/host_sim build/native/mpc-looper.so dump > build/params.json
	MPC_PLUGIN_FOLDER="$(CURDIR)/build/test-profile" ./build/host_sim build/native/mpc-looper.so test --seconds 1 --wav build/host-test.wav > build/host-test.log 2>&1
	cat build/host-test.log
skin:
	python3 tools/make_skin.py
package: skin build/host_sim native
	./build/host_sim build/native/mpc-looper.so dump > build/params.json
	python3 tools/package.py
sanitize:
	$(CXX) $(FLAGS) -O1 -g -fsanitize=address,undefined tests/core_test.cpp -o build/core_sanitize
	./build/core_sanitize
	$(CXX) $(FLAGS) -O1 -g -fsanitize=address,undefined tests/integration_test.cpp -o build/integration_sanitize
	MPC_PLUGIN_FOLDER="$(CURDIR)/build/sanitize-profile" ./build/integration_sanitize

	$(CXX) $(FLAGS) -O1 -g -fsanitize=address,undefined tests/tempo_test.cpp -o build/tempo_sanitize
	./build/tempo_sanitize

	$(CXX) $(FLAGS) -O1 -g -fsanitize=address,undefined tests/pedal_test.cpp -o build/pedal_sanitize
	./build/pedal_sanitize

	$(CXX) $(FLAGS) -O1 -g -fsanitize=address,undefined tests/memory_test.cpp -o build/memory_sanitize
	./build/memory_sanitize

	$(MAKE) disk-sanitize

disk-sanitize:
	$(CXX) $(FLAGS) -O1 -g -fsanitize=address,undefined tests/disk_store_test.cpp -o build/disk_store_sanitize
	./build/disk_store_sanitize
	$(CXX) $(FLAGS) -O1 -g -fsanitize=address,undefined tests/disk_engine_test.cpp -o build/disk_engine_sanitize
	./build/disk_engine_sanitize

control-sanitize:
	$(CXX) $(FLAGS) -O1 -g -fsanitize=address,undefined tests/pedal_test.cpp -o build/pedal_sanitize
	./build/pedal_sanitize
	$(CXX) $(FLAGS) -O1 -g -fsanitize=address,undefined tests/integration_test.cpp -o build/integration_sanitize
	MPC_PLUGIN_FOLDER="$(CURDIR)/build/sanitize-profile" ./build/integration_sanitize
