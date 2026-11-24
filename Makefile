WINCXX ?= i686-w64-mingw32-g++
FLAGS := -std=c++17 -O2 -Wall -Wextra -Werror
PLUGIN := build/NVSE/Plugins/TTWVRFieldWeapons.dll
ZIP := build/TTWVR-Field-Weapons-0.1.1.zip

all: $(PLUGIN)
$(PLUGIN): $(wildcard src/*.cpp src/*.h)
	mkdir -p $(@D)
	$(WINCXX) $(FLAGS) -static -shared -s src/Plugin.cpp src/Host.cpp -o $@
test: tests/Motion.cpp src/Motion.h
	mkdir -p build
	$(CXX) $(FLAGS) tests/Motion.cpp -o build/test-motion
	./build/test-motion
package: $(PLUGIN) test
	cd build && zip -q -X -FS $(notdir $(ZIP)) NVSE/Plugins/TTWVRFieldWeapons.dll
.PHONY: all test package
