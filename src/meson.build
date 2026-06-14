python = import('python').find_installation()

qjsc_prog = find_program('qjsc', required: false)
qjs_prog  = find_program('qjs',  required: false)

if not meson.is_cross_build() and qjsc_prog.found() and qjs_prog.found()
  # files() so the script path is absolute — a raw '../scripts/...' string would
  # be resolved relative to the build dir and break out-of-tree builds.
  verify_polyfills_py = files('../scripts/verify-polyfills.py')
  polyfills_verify = custom_target(
    'polyfills-verify',
    input: ['../data/js/polyfills.js', '../scripts/verify-polyfills.js'],
    output: 'polyfills.verified',
    command: [
      python, verify_polyfills_py,
      qjsc_prog, qjs_prog, '@INPUT0@', '@INPUT1@', '@OUTPUT@',
    ],
    build_by_default: true,
  )
endif

polyfills_h = custom_target(
  'polyfills.h',
  input: ['../scripts/embed-text.py', '../data/js/polyfills.js'],
  output: 'polyfills.h',
  command: [python, '@INPUT0@', '@INPUT1@', '@OUTPUT@',
            'ns_js_polyfills_src'],
)

wpt_hook_h = custom_target(
  'wpt_hook.h',
  input: ['../scripts/embed-text.py', '../data/js/wpt-hook.js'],
  output: 'wpt_hook.h',
  command: [python, '@INPUT0@', '@INPUT1@', '@OUTPUT@',
            'ns_wpt_hook_src'],
)

nordstjernen_engine_sources = files(
  'ai.c',
  'anim.c',
  'bcache.c',
  'bookmarks.c',
  'cache.c',
  'config.c',
  'csp.c',
  'css.c',
  'datetime.c',
  'debuglog.c',
  'dom.c',
  'engine.c',
  'env.c',
  'eventsource.c',
  'forms.c',
  'font.c',
  'glctx.c',
  'history.c',
  'html.c',
  'i18n.c',
  'image.c',
  'image_ico.c',
  'image_webp.c',
  'image_wuffs.c',
  'idb.c',
  'js.c',
  'js_canvas.c',
  'js_intl.c',
  'js_date.c',
  'js_realm.c',
  'layout.c',
  'mobile.c',
  'net.c',
  'netutil.c',
  'paint.c',
  'pdf.c',
  'render.c',
  'security.c',
  'selection.c',
  'tab_worker.c',
  'texture.c',
  'video.c',
  'wasm.c',
  'webcrypto.c',
  'webgl.c',
  'ws.c',
  'html_lexbor.c',
)

# GTK-free helpers used by the desktop executable but not the engine UI:
# the headless render driver and the crash watchdog. Kept in src/ as common
# code; the GTK frontend itself lives in src/gtk/.
nordstjernen_app_sources = files(
  'headless.c',
  'media.c',
  'watchdog.c',
)

if have_avif
  nordstjernen_engine_sources += files('image_avif.c')
endif

nordstjernen_win_resources = []
if host_machine.system() == 'windows'
  windows = import('windows')
  nordstjernen_win_resources = windows.compile_resources(
    'nordstjernen.rc',
    depend_files: files('../data/icons/nordstjernen.ico'),
  )
endif

if is_android
  nordstjernen_deps = [
    glib,
    gobject,
    gio,
    gmodule,
    cairo,
    pango,
    pangocairo,
    libcurl,
    libcrypto,
    libm,
    libquickjs,
    libwebp,
    libwuffs,
    libuchardet,
    liblexbor,
    libwamr,
    sqlite3,
    zlib,
  ]
else
  nordstjernen_deps = [
    glib,
    gobject,
    gio,
    cairo,
    pango,
    pangocairo,
    gdkpixbuf,
    epoxy,
    gmodule,
    libcurl,
    libcrypto,
    libm,
    libquickjs,
    libwebp,
    libwuffs,
    libuchardet,
    liblexbor,
    libwamr,
    librsvg,
    sqlite3,
    zlib,
  ]

  if have_seccomp
    nordstjernen_deps += [libseccomp]
  endif

  if have_poppler
    nordstjernen_deps += [libpoppler]
  endif

  if have_avif
    nordstjernen_deps += [libavif]
  endif
endif

if have_fontconfig
  nordstjernen_deps += [libfontconfig]
endif

if have_pangoft2
  nordstjernen_deps += [libpangoft2]
endif

if have_freetype
  nordstjernen_deps += [libfreetype]
endif

nordstjernen_deps += [libpsl]

if have_ai
  cpp_stdlib_name = host_machine.system() == 'darwin' ? 'c++' : 'stdc++'
  nordstjernen_deps += ai_deps
  nordstjernen_deps += [
    meson.get_compiler('cpp').find_library(cpp_stdlib_name, required: true),
    cc.find_library('dl', required: false),
    dependency('threads'),
  ]
endif

nordstjernen_win_subsystem = (
  host_machine.system() == 'windows' ? 'windows' : 'console'
)

nordstjernen_link_args = []
if host_machine.system() == 'windows'
  nordstjernen_link_args += cc.get_supported_link_arguments(
    ['-Wl,--stack,8388608'],
  )
  nordstjernen_link_args += ['-lbcrypt', '-ladvapi32', '-lshell32', '-luser32']
endif

if host_machine.system() == 'windows'
  executable(
    'nordstjernen-launcher',
    files('win_launcher.c') + nordstjernen_win_resources,
    win_subsystem: 'windows',
    link_args: ['-luser32', '-lshell32'],
  )
endif

nordstjernen_embed_sources = nordstjernen_engine_sources + files(
  'embed_shim.c',
  'libnordstjernen.c',
) + [polyfills_h]

if is_android
  nordstjernen_embed_sources += files(
    'ipc_http.c',
    'renderer_serve.c',
    'rproc_http.c',
  )
endif

libnordstjernen_lib = library(
  'nordstjernen',
  nordstjernen_embed_sources,
  dependencies: nordstjernen_deps,
  link_args: nordstjernen_link_args,
  install: true,
)

# A static variant of the engine so the out-of-process renderer links it in
# and ships as a single self-contained executable (no libnordstjernen.so
# runtime dependency to bundle in distro packages).
libnordstjernen_static = static_library(
  'nordstjernen-static',
  nordstjernen_embed_sources,
  dependencies: nordstjernen_deps,
  install: false,
)

install_headers('libnordstjernen.h', subdir: 'nordstjernen')

# Out-of-process renderer (step 1 of the process-per-tab direction): a small
# executable that drives libnordstjernen over an IPC control channel + shared
# memory framebuffer (src/rproc.*), plus a proof-of-concept client.
if not is_android
  rproc_deps = []
  if host_machine.system() != 'windows'
    rproc_deps += [cc.find_library('rt', required: false)]
  endif

  executable(
    'nordstjernen-renderer',
    files('renderer_http.c', 'renderer_serve.c', 'ipc_http.c'),
    link_with: libnordstjernen_static,
    include_directories: include_directories('.'),
    dependencies: nordstjernen_deps + rproc_deps,
    link_args: nordstjernen_link_args,
    install: true,
  )

  if get_option('ipc_experiment') and host_machine.system() != 'windows'
    executable(
      'ipc-http-demo',
      files('ipc_http_demo.c', 'rproc_http.c', 'ipc_http.c'),
      include_directories: include_directories('.'),
      dependencies: rproc_deps,
      install: false,
    )
  endif
endif

if not is_android and have_gtk
  subdir('gtk')
endif

qt_feature = get_option('qt')
if not is_android and qt_feature.allowed()
  subdir('qt')
endif

if not is_android and not have_gtk
  message('GTK 4 shell disabled — building the engine, out-of-process renderer' +
          ' and embeddable library without GTK. WebGL is unavailable in this' +
          ' configuration (its GL context is GTK-backed).')
endif
