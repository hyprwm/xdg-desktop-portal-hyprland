COMPILER   = g++ -std=c++23
HYPR_FLAGS = -Ihyprtoolkit/core/ -I/usr/include/hyprtoolkit/window/ -I/usr/include/hyprutils/ -I/usr/include/hyprtoolkit/element/ -I/usr/include/pixman-1/ -I/usr/include/drm/
LDFLAGS    = -lhyprutils -lhyprtoolkit -lpixman-1
TARGET     = build/hyprland-share-picker
SRC        = main.cpp

all:
	mkdir -p build
	$(COMPILER) $(HYPR_FLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

test: all
	env \
	XDPH_WINDOW_SHARING_LIST='335191472[HC>]kitty[HT>]vi main.cpp [HE>]94285626969136[HA>]335605520[HC>]firefox[HT>]Switch case error fix — Mozilla Firefox[HE>]94285641535360[HA>]' \
	XDPH_WORKSPACE_SHARING_LIST='1[WI>]1[WN>]2[WI>]2[WN>]3[WI>]3[WN>]4[WI>]4[WN>]5[WI>]5[WN>]6[WI>]6[WN>]' \
	./build/hyprland-share-picker

clean:
	rm -rf build/*

