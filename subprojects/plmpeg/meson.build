project('plmpeg', 'c', license: 'MIT')

cc = meson.get_compiler('c')

plmpeg_warn_off = cc.get_supported_arguments([
  '-w',
  '-Wno-unused-function',
  '-Wno-unused-variable',
  '-Wno-unused-but-set-variable',
  '-Wno-unused-parameter',
  '-Wno-shadow',
  '-Wno-pointer-arith',
  '-Wno-format-nonliteral',
  '-Wno-cast-align',
  '-Wno-undef',
  '-Wno-missing-prototypes',
  '-Wno-missing-declarations',
  '-Wno-redundant-decls',
  '-Wno-old-style-definition',
  '-Wno-strict-prototypes',
  '-Wno-write-strings',
  '-Wno-vla',
  '-Wno-sign-compare',
])

plmpeg_lib = static_library(
  'plmpeg',
  'pl_mpeg_impl.c',
  c_args: plmpeg_warn_off,
)

plmpeg_dep = declare_dependency(
  link_with: plmpeg_lib,
  include_directories: include_directories('.'),
)

meson.override_dependency('plmpeg', plmpeg_dep)
