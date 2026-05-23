COMPILER   = g++ -std=c++23
HYPR_FLAGS = -Ihyprtoolkit/core/ -I/usr/include/hyprtoolkit/window/ -I/usr/include/hyprutils/ -I/usr/include/hyprtoolkit/element/ -I/usr/include/pixman-1/ -I/usr/include/drm/
LDFLAGS    = -lhyprutils -lhyprtoolkit -lpixman-1
TARGET     = build/hyprland-share-picker
SRC        = main.cpp

all:
	mkdir -p build
	$(COMPILER) $(HYPR_FLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -rf build/*

