# web-text-lib — the browser's text layout library, a Pango fork built in-tree.
# Public surface: `web_text_dep`, `web_text_cairo_dep`, `web_text_fc_dep`.

wtl_version = '1.58.2'.split('.')
wtl_major_version = wtl_version[0].to_int()
wtl_minor_version = wtl_version[1].to_int()
wtl_micro_version = wtl_version[2].to_int()
wtl_api_version = '@0@.0'.format(wtl_major_version)

wtl_host_system = host_machine.system()
wtl_argument_syntax = cc.get_argument_syntax()

wtl_conf = configuration_data()
wtl_conf.set('NS_PANGO_BINARY_AGE', wtl_minor_version * 100 + wtl_micro_version)
wtl_conf.set('NS_PANGO_INTERFACE_AGE',
             wtl_minor_version.is_odd() ? 0 : wtl_micro_version)
wtl_conf.set('NS_PANGO_VERSION_MAJOR', wtl_major_version)
wtl_conf.set('NS_PANGO_VERSION_MINOR', wtl_minor_version)
wtl_conf.set('NS_PANGO_VERSION_MICRO', wtl_micro_version)

wtl_cflags = [
  '-D_POSIX_C_SOURCE=200809L',
  '-D_POSIX_THREAD_SAFE_FUNCTIONS',
  '-D_GNU_SOURCE',
  '-DG_LOG_USE_STRUCTURED=1',
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_80',
  '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_80',
]
if wtl_host_system == 'windows'
  wtl_cflags += '-D_WIN32_WINNT=0x0A00'
endif

wtl_test_cflags = []
if wtl_argument_syntax == 'msvc'
  wtl_test_cflags += ['-FImsvc_recommended_pragmas.h', '-utf-8']
elif wtl_argument_syntax == 'gcc'
  wtl_test_cflags += [
    '-w',
    '-fno-strict-aliasing',
    '-fstack-protector-strong',
    '-fstack-clash-protection',
    '-fcf-protection',
  ]
  if wtl_host_system == 'windows'
    wtl_test_cflags += '-mms-bitfields'
  endif
endif
wtl_cflags += cc.get_supported_arguments(wtl_test_cflags)

if wtl_argument_syntax == 'msvc'
  wtl_cflags += cc.get_supported_arguments(['/Gy', '/Gw'])
else
  wtl_cflags += cc.get_supported_arguments([
    '-ffunction-sections',
    '-fdata-sections',
  ])
  if wtl_host_system == 'linux'
    wtl_cflags += cc.get_supported_arguments([
      '-fno-semantic-interposition',
      '-fno-plt',
    ])
  endif
  if cc.has_header_symbol('features.h', '__GLIBC__')
    wtl_cflags += ['-U_FORTIFY_SOURCE', '-D_FORTIFY_SOURCE=3']
  endif
endif

if ns_is_release
  wtl_cflags += ['-DG_DISABLE_CAST_CHECKS', '-DNDEBUG']
else
  wtl_cflags += '-DNS_PANGO_ENABLE_DEBUG'
endif

wtl_override_options = [
  'c_std=gnu17',
  'optimization=3',
  'warning_level=0',
  'werror=false',
]

wtl_glib_req = '>= 2.80'
wtl_deps = [
  cc.find_library('m', required: false),
  dependency('glib-2.0', version: wtl_glib_req),
  dependency('gobject-2.0', version: wtl_glib_req),
  dependency('gio-2.0', version: wtl_glib_req),
  dependency('fribidi', version: '>= 1.0.6'),
  dependency('harfbuzz', version: '>= 8.3.0'),
]

wtl_thai_dep = dependency('libthai', version: '>= 0.1.9', required: false)
if wtl_thai_dep.found()
  wtl_conf.set('HAVE_LIBTHAI', 1)
  wtl_deps += wtl_thai_dep
  if cc.has_function('th_brk_find_breaks', dependencies: wtl_thai_dep)
    wtl_conf.set('HAVE_TH_BRK_FIND_BREAKS', 1)
  endif
endif

wtl_fontconfig_dep = dependency('fontconfig', version: '>= 2.15.0')
wtl_deps += wtl_fontconfig_dep
wtl_conf.set('HAVE_FC_CONFIG_SET_DEFAULT_SUBSTITUTE',
             cc.has_function('FcConfigSetDefaultSubstitute',
                             dependencies: wtl_fontconfig_dep,
                             prefix: '#include <fontconfig/fontconfig.h>'))

wtl_deps += dependency(['freetype2', 'freetype'])
wtl_conf.set('HAVE_FREETYPE', 1)

wtl_cairo_dep = dependency('cairo', version: '>= 1.18.0')
wtl_cairo_ft_dep = dependency('cairo-ft', version: '>= 1.18.0')
if not cc.links('''
#include <cairo-ft.h>
int main() {
  FcPattern *p = NULL;
  cairo_ft_font_face_create_for_pattern (p);
  return 0;
}''',
                dependencies: wtl_cairo_ft_dep,
                name: 'Cairo is built with FreeType and FontConfig support')
  error('web-text-lib needs cairo built with FreeType and FontConfig support')
endif
wtl_conf.set('HAVE_CAIRO_FREETYPE', 1)
wtl_conf.set('HAVE_CAIRO', 1)
wtl_conf.set('HAVE_SYSPROF', false)
wtl_deps += wtl_cairo_dep

configure_file(output: 'config.h', configuration: wtl_conf)

wtl_root_inc = include_directories('.')

subdir('ns-pango')
