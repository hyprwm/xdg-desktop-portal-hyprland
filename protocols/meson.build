wayland_protos = dependency('wayland-protocols',
	version: '>=1.31',
	default_options: ['tests=false'],
)

hyprland_protos = dependency('hyprland-protocols',
	version: '>=0.2',
	fallback: 'hyprland-protocols',
)

wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')
hl_protocol_dir = hyprland_protos.get_variable('pkgdatadir')

hyprwayland_scanner_dep = dependency('hyprwayland-scanner', required: true, native: true, version: '>=0.4.2')
hyprwayland_scanner = find_program(
	hyprwayland_scanner_dep.get_variable(pkgconfig: 'hyprwayland_scanner'),
	native: true,
)

client_protocols = [
	'wlr-screencopy-unstable-v1.xml',
	'wlr-foreign-toplevel-management-unstable-v1.xml',
	hl_protocol_dir / 'protocols/hyprland-toplevel-export-v1.xml',
	hl_protocol_dir / 'protocols/hyprland-global-shortcuts-v1.xml',
	hl_protocol_dir / 'protocols/hyprland-input-capture-v1.xml',
	wl_protocol_dir / 'stable/linux-dmabuf/linux-dmabuf-v1.xml',
]

wl_proto_files = []

foreach xml: client_protocols
	wl_proto_files += custom_target(
		xml.underscorify() + '_c',
		input: xml,
		output: ['@BASENAME@.cpp', '@BASENAME@.hpp'],
		command: [hyprwayland_scanner, '--client', '@INPUT@', '@OUTDIR@'],
	)
endforeach

wayland_scanner = dependency('wayland-scanner')
wayland_scanner_dir = wayland_scanner.get_variable('pkgdatadir')

wayland_xml = wayland_scanner_dir / 'wayland.xml'
wayland_protocol = custom_target(
	wayland_xml.underscorify(),
	input: wayland_xml,
	output: ['@BASENAME@.cpp', '@BASENAME@.hpp'],
	command: [hyprwayland_scanner, '--wayland-enums', '--client', '@INPUT@', '@OUTDIR@'],
)

lib_client_protos = static_library(
	'client_protos',
	wl_proto_files + wayland_protocol,
)

client_protos = declare_dependency(
	link_with: lib_client_protos,
	sources: wl_proto_files + wayland_protocol
)
