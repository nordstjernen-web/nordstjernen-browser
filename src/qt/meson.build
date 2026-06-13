qt6 = import('qt6')

qt6_dep = dependency(
  'qt6',
  version: '>=6.4',
  modules: ['Core', 'Gui', 'Widgets', 'Concurrent', 'Svg'],
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

  # The Qt frontend drives one sandboxed renderer process per tab over the
  # HTTP-control + shared-memory-frame IPC in src/rproc_http.c
  # (procwindow/procview); with --single-process the same protocol is served
  # by an in-process engine thread, so the engine is linked in statically.
  qt_sources = files(
    'main.cpp',
    'procwindow.cpp',
    'procview.cpp',
    '../media.c',
    '../renderer_serve.c',
    '../rproc_http.c',
    '../rproc_inproc.c',
    '../ipc_http.c',
  )

  qt_deps = [qt6_dep, glib_dep, gio_dep] + nordstjernen_deps
  if host_machine.system() != 'windows'
    qt_deps += [cc.find_library('rt', required: false)]
  else
    qt_deps += [cc.find_library('shell32')]
  endif

  executable(
    'nordstjernen-qt',
    qt_sources + qt_moc + qt_resources,
    dependencies: qt_deps,
    link_with: libnordstjernen_static,
    include_directories: include_directories('..'),
    link_args: nordstjernen_link_args,
    win_subsystem: nordstjernen_win_subsystem,
    install: true,
  )

  message('Qt 6 frontend (nordstjernen-qt) enabled — Qt @0@.'.format(qt6_dep.version()))
endif
