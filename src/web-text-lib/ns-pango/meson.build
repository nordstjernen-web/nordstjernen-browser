# web-text-lib/ns-pango — the library's sources, built as static libraries.

wtl_sources = files(
  'break.c',
  'ellipsize.c',
  'fonts.c',
  'glyphstring.c',
  'itemize.c',
  'modules.c',
  'pango-attributes.c',
  'pango-bidi-type.c',
  'pango-color.c',
  'pango-context.c',
  'pango-coverage.c',
  'pango-emoji.c',
  'pango-engine.c',
  'pango-fontmap.c',
  'pango-fontset.c',
  'pango-fontset-simple.c',
  'pango-glyph-item.c',
  'pango-gravity.c',
  'pango-item.c',
  'pango-language.c',
  'pango-layout.c',
  'pango-markup.c',
  'pango-matrix.c',
  'pango-renderer.c',
  'pango-script.c',
  'pango-tabs.c',
  'pango-utils.c',
  'reorder-items.c',
  'shape.c',
  'ns-break-cache.c',
  'ns-item-cache.c',
  'ns-shape-cache.c',
)

wtl_enum_headers = files(
  'pango.h',
  'pango-attributes.h',
  'pango-bidi-type.h',
  'pango-break.h',
  'pango-color.h',
  'pango-context.h',
  'pango-coverage.h',
  'pango-direction.h',
  'pango-engine.h',
  'pango-font.h',
  'pango-fontmap.h',
  'pango-fontset.h',
  'pango-fontset-simple.h',
  'pango-glyph.h',
  'pango-glyph-item.h',
  'pango-gravity.h',
  'pango-item.h',
  'pango-language.h',
  'pango-layout.h',
  'pango-matrix.h',
  'pango-markup.h',
  'pango-modules.h',
  'pango-renderer.h',
  'pango-script.h',
  'pango-tabs.h',
  'pango-types.h',
  'pango-utils.h',
  'ns-pango-cache.h',
)

wtl_features_conf = configuration_data()
wtl_features_conf.set('NS_PANGO_VERSION_MAJOR', wtl_major_version)
wtl_features_conf.set('NS_PANGO_VERSION_MINOR', wtl_minor_version)
wtl_features_conf.set('NS_PANGO_VERSION_MICRO', wtl_micro_version)
configure_file(
  input: 'pango-features.h.meson',
  output: 'pango-features.h',
  configuration: wtl_features_conf,
)

wtl_enums = import('gnome').mkenums(
  'pango-enum-types',
  sources: wtl_enum_headers,
  c_template: 'pango-enum-types.c.template',
  h_template: 'pango-enum-types.h.template',
)

wtl_lib_cflags = wtl_cflags + [
  '-DG_LOG_DOMAIN="WebTextLib"',
  '-DNS_PANGO_COMPILATION',
  '-DSYSCONFDIR="@0@"'.format(get_option('prefix') / get_option('sysconfdir')),
  '-DLIBDIR="@0@"'.format(get_option('prefix') / get_option('libdir')),
]
wtl_inc = [wtl_root_inc, include_directories('.')]

wtl_lib = static_library('web-text-lib',
  wtl_sources + wtl_enums,
  dependencies: wtl_deps,
  include_directories: wtl_inc,
  c_args: wtl_lib_cflags,
  override_options: wtl_override_options,
)
web_text_dep = declare_dependency(
  link_with: wtl_lib,
  include_directories: wtl_root_inc,
  dependencies: wtl_deps,
  sources: wtl_enums[1],
)

wtl_fc_lib = static_library('web-text-lib-fc',
  files(
    'pangofc-font.c',
    'pangofc-fontmap.c',
    'pangofc-decoder.c',
    'pango-trace.c',
  ),
  dependencies: wtl_deps + web_text_dep,
  include_directories: wtl_inc,
  c_args: wtl_lib_cflags + '-DNS_PANGO_DISABLE_DEPRECATION_WARNINGS',
  override_options: wtl_override_options,
)
web_text_fc_dep = declare_dependency(
  link_with: wtl_fc_lib,
  include_directories: wtl_root_inc,
  dependencies: wtl_deps + web_text_dep,
)

wtl_cairo_lib = static_library('web-text-lib-cairo',
  files(
    'pangocairo-context.c',
    'pangocairo-font.c',
    'pangocairo-fontmap.c',
    'pangocairo-render.c',
    'pangocairo-fcfont.c',
    'pangocairo-fcfontmap.c',
  ),
  dependencies: wtl_deps + [web_text_dep, web_text_fc_dep],
  include_directories: wtl_inc,
  c_args: wtl_lib_cflags + '-DNS_PANGO_DISABLE_DEPRECATION_WARNINGS',
  override_options: wtl_override_options,
)
web_text_cairo_dep = declare_dependency(
  link_with: wtl_cairo_lib,
  include_directories: wtl_root_inc,
  dependencies: wtl_deps + web_text_dep,
)
