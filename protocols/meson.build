wayland_protos = dependency('wayland-protocols',
	version: '>=1.39',
	default_options: ['tests=false'],
)

hyprland_protos = dependency('hyprland-protocols',
	version: '>=0.7.0',
	required: false,
	allow_fallback: false,
)

wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')
hl_protocol_dir = meson.project_source_root() / 'subprojects/hyprland-protocols'
if hyprland_protos.found()
	installed_hl_protocol_dir = hyprland_protos.get_variable('pkgdatadir')
	if import('fs').is_file(installed_hl_protocol_dir / 'protocols/hyprland-workspace-image-capture-source-v1.xml')
		hl_protocol_dir = installed_hl_protocol_dir
	endif
endif

workspace_source_xml = hl_protocol_dir / 'protocols/hyprland-workspace-image-capture-source-v1.xml'
if not import('fs').is_file(workspace_source_xml)
	error('Missing ' + workspace_source_xml + '. Install hyprland-protocols with workspace capture support or initialize/update the bundled submodule.')
endif

hyprwayland_scanner_dep = dependency('hyprwayland-scanner', required: true, native: true, version: '>=0.4.2')
hyprwayland_scanner = find_program(
	hyprwayland_scanner_dep.get_variable(pkgconfig: 'hyprwayland_scanner'),
	native: true,
)

client_protocols = [
	'wlr-screencopy-unstable-v1.xml',
	'wlr-foreign-toplevel-management-unstable-v1.xml',
	hl_protocol_dir / 'protocols/hyprland-toplevel-export-v1.xml',
	hl_protocol_dir / 'protocols/hyprland-toplevel-mapping-v1.xml',
	hl_protocol_dir / 'protocols/hyprland-global-shortcuts-v1.xml',
	hl_protocol_dir / 'protocols/hyprland-input-capture-v1.xml',
	workspace_source_xml,
	wl_protocol_dir / 'stable/linux-dmabuf/linux-dmabuf-v1.xml',
	wl_protocol_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml',
	wl_protocol_dir / 'staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml',
	wl_protocol_dir / 'staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml',
	wl_protocol_dir / 'staging/ext-image-capture-source/ext-image-capture-source-v1.xml',
	wl_protocol_dir / 'staging/ext-workspace/ext-workspace-v1.xml',
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
