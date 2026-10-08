project('wgpu-native', 'c', version: '29.0.1.1', meson_version: '>=1.1')

cc = meson.get_compiler('c')
system = host_machine.system()

wgpu_static = cc.find_library('wgpu_native',
  dirs: meson.current_source_dir() / 'lib',
  static: true)

platform_deps = []
if system == 'windows'
  foreach name : ['d3dcompiler_47', 'd3d12', 'dxgi', 'opengl32', 'gdi32',
                  'user32', 'ole32', 'oleaut32', 'propsys', 'runtimeobject',
                  'ws2_32', 'userenv', 'bcrypt', 'ntdll', 'advapi32',
                  'kernel32', 'synchronization', 'dbghelp', 'shell32']
    platform_deps += cc.find_library(name)
  endforeach
elif system == 'darwin'
  platform_deps += dependency('appleframeworks',
    modules: ['Metal', 'QuartzCore', 'Foundation', 'CoreFoundation',
              'CoreGraphics', 'IOKit', 'IOSurface', 'Security'])
  platform_deps += cc.find_library('objc')
else
  platform_deps += cc.find_library('dl', required: false)
  platform_deps += cc.find_library('m', required: false)
  platform_deps += dependency('threads')
endif

wgpu_native_dep = declare_dependency(
  include_directories: include_directories('include'),
  dependencies: [wgpu_static] + platform_deps,
)
