demo_sources = files(
  'demo.c',
)

blueprints = custom_target(
  'blueprints',
  input: files(
    'window.blp',
  ),
  output: '.',
  command: [
    find_program('blueprint-compiler', version: '>= 0.20.0'),
    'batch-compile',
    '@OUTPUT@',
    '@CURRENT_SOURCE_DIR@',
    '@INPUT@'
  ],
)
demo_sources += gnome.compile_resources('demo-resources',
  'demo.gresource.xml',
  c_name: 'demo',
  dependencies: blueprints
)

demo_deps = [
  bge_dep,
]

demo = executable(
  'bge-demo', demo_sources,
  dependencies: demo_deps,
  install: true,
)
