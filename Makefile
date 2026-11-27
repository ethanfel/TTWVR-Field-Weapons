WINCXX ?= i686-w64-mingw32-g++
FLAGS := -std=c++17 -O2 -Wall -Wextra -Werror
VERSION := 1.1.8-r10
PLUGIN := build/NVSE/Plugins/TTWVRFieldWeapons.dll
ZIP := build/TTWVR-Field-Weapons-$(VERSION).zip

all: $(PLUGIN)
$(PLUGIN): Makefile $(wildcard src/*.cpp src/*.h)
	mkdir -p $(@D)
	$(WINCXX) $(FLAGS) -DFIELD_WEAPONS_VERSION='"$(VERSION)"' -static -shared -s src/Plugin.cpp src/Host.cpp src/Stagger.cpp src/NpcThrow.cpp -o $@
test: tests/Motion.cpp src/Motion.h src/Impact.h
	mkdir -p build
	$(CXX) $(FLAGS) tests/Motion.cpp -o build/test-motion
	./build/test-motion
build/test-host.exe: tests/Host.cpp src/Host.cpp src/Host.h src/Motion.h
	mkdir -p build
	$(WINCXX) $(FLAGS) -municode -static tests/Host.cpp src/Host.cpp -o $@
build/NVSE/Plugins/TTWVRFieldWeapons.ini: TTWVRFieldWeapons.ini
	mkdir -p $(@D)
	cp $< $@
package: $(PLUGIN) build/NVSE/Plugins/TTWVRFieldWeapons.ini test
	cd build && zip -q -X -FS $(notdir $(ZIP)) NVSE/Plugins/TTWVRFieldWeapons.dll NVSE/Plugins/TTWVRFieldWeapons.ini
.PHONY: all test package
