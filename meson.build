project('aeroglasswm', 'c',
  version: '0.1',
  default_options: ['warning_level=2', 'c_std=c11'])

add_project_arguments('-DWLR_USE_UNSTABLE', '-D_GNU_SOURCE', language: 'c')

scenefx = dependency('scenefx-0.4')
wlroots = dependency('wlroots-0.19', version: '>=0.19.0')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')
pixman = dependency('pixman-1')
cairo = dependency('cairo')
wayland_protocols = dependency('wayland-protocols')

wayland_scanner = find_program('wayland-scanner')
wl_protocol_dir = wayland_protocols.get_variable(pkgconfig: 'pkgdatadir')
xdg_shell_xml = join_paths(wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml')
layer_shell_xml = 'protocols/wlr-layer-shell-unstable-v1.xml'

xdg_shell_header = custom_target(
  'xdg-shell-protocol.h',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

xdg_shell_code = custom_target(
  'xdg-shell-protocol.c',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

layer_shell_header = custom_target(
  'wlr-layer-shell-unstable-v1-protocol.h',
  input: layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

layer_shell_code = custom_target(
  'wlr-layer-shell-unstable-v1-protocol.c',
  input: layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

sources = [
  'src/main.c',
  'src/modules/wm.c',
  'src/modules/config.c',
  'src/modules/ipc.c',
  'src/modules/output.c',
  'src/modules/grid.c',
  'src/toml.c',
  xdg_shell_header,
  xdg_shell_code,
  layer_shell_header,
  layer_shell_code,
]
deps = [scenefx, wlroots, wayland_server, xkbcommon, pixman, cairo]

if get_option('slider')
  cairo_dep = dependency('cairo', required: true)
  sources += ['src/modules/slider.c']
  deps += [cairo_dep]
  add_project_arguments('-DAEROGLASS_WITH_SLIDER', language: 'c')
endif

inc = include_directories('src', 'src/modules')
pc_xml = '/usr/share/wayland-protocols/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml'
pc_proto_h = custom_target(
  'pointer-constraints-unstable-v1-protocol.h',
  input: pc_xml,
  output: 'pointer-constraints-unstable-v1-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

executable('aeroglasswm', pc_proto_h, sources, include_directories: inc, dependencies: deps, install: true)


executable('wmctl', 'src/wmctl.c', install: true)
