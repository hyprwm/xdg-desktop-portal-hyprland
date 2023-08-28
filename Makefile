PREFIX = /usr/local
LIBEXEC = /usr/lib
SHARE = /usr/share

all:
	$(MAKE) release

release:
	cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Release -H./ -B./build -G Ninja
	cmake --build ./build --config Release --target all -j$(nproc)

debug:
	cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Debug -H./ -B./build -G Ninja
	cmake --build ./build --config Debug --target all -j$(nproc)

install:
	$(MAKE) release
	cp ./build/hyprland-share-picker/hyprland-share-picker ${PREFIX}/bin
	cp ./build/xdg-desktop-portal-hyprland ${LIBEXEC}/
	cp ./hyprland.portal ${SHARE}/xdg-desktop-portal/portals/
	cp ./org.freedesktop.impl.portal.desktop.hyprland.service ${SHARE}/dbus-1/services/