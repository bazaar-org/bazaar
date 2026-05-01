api_version = bz_version

bge_sources = [
  'libbge.c',
  'bge-animation.c',
  'bge-carousel.c',
  'bge-markdown-render.c',
]

bge_headers = [
  'bge.h',
  'bge-animation.h',
  'bge-carousel.h',
]

subdir('wdgt')

version_split = api_version.split('.')
version_conf = configuration_data()
version_conf.set('VERSION', api_version)
version_conf.set('MAJOR_VERSION', version_split[0])
version_conf.set('MINOR_VERSION', version_split[1])
version_conf.set('MICRO_VERSION', version_split[2])

configure_file(
          input: 'bge-version.h.in',
         output: 'bge-version.h',
  configuration: version_conf,
        install: true,
    install_dir: get_option('includedir') / 'bge'
)

bge_deps = [
  cc.find_library('m', required: true),
  dependency('gtk4', version: '>= 4.22.1'),
  dependency('md4c', version: '>= 0.5.1'),
]

bge_marshalers = gnome.genmarshal('bge-marshalers',
  sources: 'bge-marshalers.list',
  prefix: 'bge_marshal',
  valist_marshallers: true,
)

bge_lib = shared_library(
  'bge-' + api_version,
  bge_sources, bge_marshalers,
  dependencies: bge_deps,
       install: true,
)

install_headers(
  bge_headers + files('bge-version-macros.h'),
  subdir: 'bge')

pkg = import('pkgconfig')
pkg.generate(
  description: 'Bazaar GTK Extensions',
    libraries: bge_lib,
         name: 'libbge',
     filebase: 'bge-' + api_version,
      version: api_version,
      subdirs: 'bge',
     requires: ['gtk4', 'md4c'],
  install_dir: get_option('libdir') / 'pkgconfig'
)

bge_dep = declare_dependency(
  link_with: bge_lib,
  dependencies: [bge_deps],
  include_directories : include_directories('..', '.'),
)

subdir('demo')
