qt6 = import('qt6')

qt6_dep = dependency(
  'qt6',
  modules: ['Core', 'Gui', 'Widgets', 'Concurrent'],
  required: qt_feature,
)

if qt6_dep.found()
  glib_dep = dependency('glib-2.0')
  gio_dep = dependency('gio-2.0')

  qt_moc = qt6.compile_moc(
    headers: ['procwindow.h', 'procview.h'],
    dependencies: qt6_dep,
  )

  qt_resources = qt6.compile_resources(
    name: 'nordstjernen_resources',
    sources: 'nordstjernen.qrc',
  )

  # The Qt frontend is a thin shell that drives one sandboxed renderer process
  # per tab over the HTTP-control + shared-memory-frame IPC in src/rproc_http.c
  # (procwindow/procview). The engine itself runs in nordstjernen-renderer.
  qt_sources = files(
    'main.cpp',
    'procwindow.cpp',
    'procview.cpp',
    '../bookmarks.c',
    '../config.c',
    '../media.c',
    '../netutil.c',
    '../rproc_http.c',
    '../ipc_http.c',
  )

  qt_deps = [qt6_dep, glib_dep, gio_dep]
  if host_machine.system() != 'windows'
    qt_deps += [cc.find_library('rt', required: false)]
  else
    qt_deps += [cc.find_library('shell32')]
  endif

  executable(
    'nordstjernen-qt',
    qt_sources + qt_moc + qt_resources,
    dependencies: qt_deps,
    include_directories: include_directories('..'),
    install: true,
  )

  message('Qt 6 frontend (nordstjernen-qt) enabled — Qt @0@.'.format(qt6_dep.version()))
endif
