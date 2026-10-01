# Fabrice Bellard's original QuickJS, built for -Dquickjs=quickjs. Upstream
# ships only a Makefile; this overlay compiles the same library objects with
# the same defines and exposes them as quickjs_dep.

project('quickjs', 'c',
  version: files('VERSION'),
  license: 'MIT',
  meson_version: '>=1.0',
)

cc = meson.get_compiler('c')

quickjs_c_args = cc.get_supported_arguments('-w', '-fwrapv') + [
  '-D_GNU_SOURCE',
  '-DCONFIG_VERSION="' + meson.project_version() + '"',
]
if host_machine.system() == 'windows'
  quickjs_c_args += '-D__USE_MINGW_ANSI_STDIO'
endif

quickjs_sys_deps = [
  cc.find_library('m', required: false),
  dependency('threads'),
]

quickjs_lib = static_library('quickjs',
  'quickjs.c',
  'dtoa.c',
  'libregexp.c',
  'libunicode.c',
  'cutils.c',
  c_args: quickjs_c_args,
  dependencies: quickjs_sys_deps,
  gnu_symbol_visibility: 'hidden',
)

quickjs_dep = declare_dependency(
  link_with: quickjs_lib,
  dependencies: quickjs_sys_deps,
  include_directories: include_directories('.', is_system: true),
)

quickjs_version = meson.project_version()
