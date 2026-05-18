project('xdg-desktop-portal-hyprland', 'cpp', 'c',
	version: run_command('cat', files('VERSION'), check: true).stdout().strip(),
	license: 'BSD-3-Clause',
	meson_version: '>=0.63.0',
	default_options: [
		'warning_level=2',
		'optimization=3',
		'buildtype=release',
		'debug=false',
    # 'cpp_std=c++23' # not yet supported by meson, as of version 0.63.0
	],
)

# clang v14.0.6 uses C++2b instead of C++23, so we've gotta account for that
# replace the following with a project default option once meson gets support for C++23
cpp_compiler = meson.get_compiler('cpp')
if cpp_compiler.has_argument('-std=c++23')
  add_global_arguments('-std=c++23', language: 'cpp')
elif cpp_compiler.has_argument('-std=c++2b')
  add_global_arguments('-std=c++2b', language: 'cpp')
else
  error('Could not configure current C++ compiler (' + cpp_compiler.get_id() + ' ' + cpp_compiler.version() + ') with required C++ standard (C++23)')
endif

add_project_arguments(cpp_compiler.get_supported_arguments([
  '-Wno-missing-field-initializers',
  '-Wno-narrowing',
  '-Wno-pointer-arith',
  '-Wno-unused-parameter',
  '-Wno-unused-value',
  '-fpermissive',
  '-Wno-address-of-temporary'
]), language: 'cpp')

conf_data = configuration_data()
conf_data.set('LIBEXECDIR', join_paths(get_option('prefix'), get_option('libexecdir')))

systemd = dependency('systemd', required: get_option('systemd'))

if systemd.found()
	systemd_service_file = 'xdg-desktop-portal-hyprland.service'
	user_unit_dir = systemd.get_variable(pkgconfig: 'systemduserunitdir',
		pkgconfig_define: ['prefix', get_option('prefix')])

	configure_file(
		configuration: conf_data,
		input: 'contrib/systemd/' + systemd_service_file + '.in',
		output: '@BASENAME@',
		install_dir: user_unit_dir,
	)
endif

configure_file(
	configuration: conf_data,
	input: 'org.freedesktop.impl.portal.desktop.hyprland.service.in',
	output: '@BASENAME@',
	install_dir: join_paths(get_option('datadir'), 'dbus-1', 'services'),
)

install_data(
	'hyprland.portal',
	install_dir: join_paths(get_option('datadir'), 'xdg-desktop-portal', 'portals'),
)
version = run_command('cat', files('VERSION'), check: true).stdout().strip()

add_project_arguments(f'-DXDPH_VERSION="@version@"', language : 'cpp')

inc = include_directories('.', 'protocols')

subdir('protocols')
subdir('src')
subdir('hyprland-share-picker')
