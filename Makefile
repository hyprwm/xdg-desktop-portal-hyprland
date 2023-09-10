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
	cp -f ./build/hyprland-share-picker/hyprland-share-picker ${PREFIX}/bin
	cp -f ./build/xdg-desktop-portal-hyprland ${LIBEXEC}/
	cp -f ./hyprland.portal ${SHARE}/xdg-desktop-portal/portals/
	sed "s|@libexecdir@|${LIBEXEC}|g" ./org.freedesktop.impl.portal.desktop.hyprland.service.in > ${SHARE}/dbus-1/services/org.freedesktop.impl.portal.desktop.hyprland
	sed "s|@libexecdir@|${LIBEXEC}|g" ./contrib/systemd/xdg-desktop-portal-hyprland.service.in > ${LIBEXEC}/systemd/user/xdg-desktop-portal-hyprland.service
