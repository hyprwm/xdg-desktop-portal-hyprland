COMPILER   = g++ -std=c++23
HYPR_FLAGS = -Ihyprtoolkit/core/ -I/usr/include/hyprtoolkit/window/ -I/usr/include/hyprutils/ -I/usr/include/hyprtoolkit/element/ -I/usr/include/pixman-1/ -I/usr/include/drm/
LDFLAGS    = -lhyprutils -lhyprtoolkit -lpixman-1 
TARGET     = build/main
SRC        = main.cpp

all:
	mkdir -p build
	$(COMPILER) $(HYPR_FLAGS) $(LDFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -rf build/main

