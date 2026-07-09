project('nestegg', 'c', version: '1.0.0', license: 'ISC')

cc = meson.get_compiler('c')

nestegg_lib = static_library(
  'nestegg',
  files('src/nestegg.c'),
  include_directories: include_directories('include'),
  c_args: cc.get_supported_arguments(['-w']),
  install: false,
)

nestegg_dep = declare_dependency(
  link_with: nestegg_lib,
  include_directories: include_directories('include'),
)
