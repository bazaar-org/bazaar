project('bazaar', 'c',
          version: run_command('version.sh', 'get-vcs').stdout().strip(),
    meson_version: '>= 1.0.0',
  default_options: [
    'warning_level=2',
    'werror=false',
    'c_std=gnu11',
  ],
)

# can't have statements before `project` ...
bz_vcs_version = run_command('version.sh', 'get-vcs').stdout().strip()
bz_version = run_command('version.sh', 'get-version').stdout().strip()

python3 = find_program('python3')
i18n = import('i18n')
if get_option('im_a_translator')
  subdir('po')
else
  gnome = import('gnome')
  cc = meson.get_compiler('c')

  project_c_args = []
  test_c_args = [
    '-Wcast-align',
    '-Wdeclaration-after-statement',
    '-Werror=address',
    '-Werror=array-bounds',
    '-Werror=empty-body',
    '-Werror=implicit',
    '-Werror=implicit-function-declaration',
    '-Werror=incompatible-pointer-types',
    '-Werror=init-self',
    '-Werror=int-conversion',
    '-Werror=int-to-pointer-cast',
    '-Werror=main',
    '-Werror=misleading-indentation',
    '-Werror=missing-braces',
    '-Werror=missing-include-dirs',
    '-Werror=nonnull',
    '-Werror=overflow',
    '-Werror=parenthesis',
    '-Werror=pointer-arith',
    '-Werror=pointer-to-int-cast',
    '-Werror=redundant-decls',
    '-Werror=return-type',
    '-Werror=sequence-point',
    '-Werror=shadow',
    '-Werror=strict-prototypes',
    '-Werror=trigraphs',
    '-Werror=undef',
    '-Werror=write-strings',
    '-Wformat-nonliteral',
    '-Wignored-qualifiers',
    '-Wimplicit-function-declaration',
    '-Wlogical-op',
    '-Wmissing-declarations',
    '-Wmissing-format-attribute',
    '-Wmissing-include-dirs',
    '-Wmissing-noreturn',
    '-Wnested-externs',
    '-Wno-cast-function-type',
    '-Wno-dangling-pointer',
    '-Wno-missing-field-initializers',
    '-Wno-sign-compare',
    '-Wno-unused-parameter',
    '-Wold-style-definition',
    '-Wpointer-arith',
    '-Wredundant-decls',
    '-Wstrict-prototypes',
    '-Wswitch-default',
    '-Wswitch-enum',
    '-Wundef',
    '-Wuninitialized',
    '-Wunused',
    '-fno-strict-aliasing',
    ['-Werror=format-security', '-Werror=format=2'],
  ]
  if get_option('buildtype') != 'plain'
    test_c_args += '-fstack-protector-strong'
  endif
  foreach arg: test_c_args
    if cc.has_multi_arguments(arg)
      project_c_args += arg
    endif
  endforeach
  add_project_arguments(project_c_args, language: 'c')

  if get_option('bge_only')
    subdir('bge')
  else
    config_h = configuration_data()

    config_h.set_quoted('PACKAGE_VERSION', bz_version)
    config_h.set_quoted('PACKAGE_VCS_VERSION', bz_vcs_version)
    config_h.set_quoted('GETTEXT_PACKAGE', 'bazaar')
    config_h.set_quoted('LOCALEDIR', get_option('prefix') / get_option('localedir'))
    config_h.set_quoted('DONATE_LINK', 'https://ko-fi.com/kolunmi')
    config_h.set_quoted('RELEASE_PAGE', run_command('version.sh', 'get-gh-release').stdout().strip())

    refresh_worker_bin_name = 'bazaar-refresh-worker'
    config_h.set_quoted('REFRESH_WORKER_BIN_NAME', refresh_worker_bin_name)

    dl_worker_bin_name = 'bazaar-dl-worker'
    config_h.set_quoted('DL_WORKER_BIN_NAME', dl_worker_bin_name)

    if get_option('hardcoded_main_config_path') != ''
      config_h.set_quoted('HARDCODED_MAIN_CONFIG', get_option('hardcoded_main_config_path'))
    endif
    if get_option('hardcoded_blocklist_path') != ''
      config_h.set_quoted('HARDCODED_BLOCKLIST', get_option('hardcoded_blocklist_path'))
    endif
    if get_option('hardcoded_content_config_path') != ''
      config_h.set_quoted('HARDCODED_CONTENT_CONFIG', get_option('hardcoded_content_config_path'))
    endif
    if get_option('sandboxed_libflatpak')
      config_h.set_quoted('SANDBOXED_LIBFLATPAK', '1')
    endif

    if get_option('development')
      config_h.set10('DEVELOPMENT_BUILD', get_option('development'))
    endif

    configure_file(output: 'config.h', configuration: config_h)
    add_project_arguments(['-I' + meson.project_build_root()], language: 'c')

    subdir('bge')
    subdir('data')
    subdir('src')
    subdir('po')

    gnome.post_install(
      glib_compile_schemas: true,
      gtk_update_icon_cache: true,
      update_desktop_database: true,
    )
  endif
endif
