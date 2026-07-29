service_conf = configuration_data()
service_conf.set('bindir', get_option('prefix') / get_option('bindir'))
configure_file(
  input: 'io.github.kolunmi.Bazaar.SearchProvider.service.in',
  output: 'io.github.kolunmi.Bazaar.SearchProvider.service',
  configuration: service_conf,
  install_dir: get_option('datadir') / 'dbus-1' / 'services'
)
