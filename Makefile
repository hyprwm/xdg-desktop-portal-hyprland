PREFIX = /usr/local

all:
	@if [[ "$EUID" = 0 ]]; then echo -en "Avoid running $(MAKE) all as sudo.\n"; fi
	$(MAKE) release

release:
	cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Release -H./ -B./build -G Ninja
	cmake --build ./build --config Release --target all -j`nproc`

debug:
	cmake --no-warn-unused-cli -DCMAKE_BUILD_TYPE:STRING=Debug -H./ -B./build -G Ninja
	cmake --build ./build --config Debug --target all -j`nproc`

install:
	@if [ ! -f ./build/xdg-desktop-portal-hyprland ]; then echo -e "You need to run $(MAKE) all first.\n" && exit 1; fi
	@echo -en "!NOTE: Please note make install does not compile xdg-desktop-portal-hyprland and only installs the already built files.\n"

	mkdir -p ${PREFIX}/bin
	mkdir -p ${PREFIX}/lib
	mkdir -p ${PREFIX}/share/xdg-desktop-portal/portals
	mkdir -p ${PREFIX}/share/dbus-1/services/
	mkdir -p ${PREFIX}/lib/systemd/user/

	cp -f ./build/hyprland-share-picker/hyprland-share-picker ${PREFIX}/bin
	cp -f ./build/xdg-desktop-portal-hyprland ${PREFIX}/lib/
	cp -f ./hyprland.portal ${PREFIX}/share/xdg-desktop-portal/portals/
	sed "s|@libexecdir@|${PREFIX}/lib|g" ./org.freedesktop.impl.portal.desktop.hyprland.service.in > ${PREFIX}/share/dbus-1/services/org.freedesktop.impl.portal.desktop.hyprland.service
	sed "s|@libexecdir@|${PREFIX}/lib|g" ./contrib/systemd/xdg-desktop-portal-hyprland.service.in > ${PREFIX}/lib/systemd/user/xdg-desktop-portal-hyprland.service
	chmod 755 ${PREFIX}/lib/xdg-desktop-portal-hyprland
