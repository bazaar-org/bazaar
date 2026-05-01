math = cc.find_library('m', required: false)
xmllint = find_program('xmllint', required: true)

gtk_dep              = dependency('gtk4', version: '>= 4.22.1')
libadwaita_dep       = dependency('libadwaita-1', version: '>= 1.8')
libdex_dep           = dependency('libdex-1', version: '>= 1.0.0')
flatpak_dep          = dependency('flatpak', version: '>= 1.9')
appstream_dep        = dependency('appstream', version: '>= 1.0')
xmlb_dep             = dependency('xmlb', version: '>= 0.3.4')
yaml_dep             = dependency('yaml-0.1', version: '>= 0.2.5')
libsoup_dep          = dependency('libsoup-3.0', version: '>= 3.6.0')
json_glib_dep        = dependency('json-glib-1.0', version: '>= 1.10.0')
glycin_dep           = dependency('glycin-2', version: '>= 2.0')
glycin_gtk4_dep      = dependency('glycin-gtk4-2', version: '>= 2.0')
webkit_dep           = dependency('webkitgtk-6.0', version: '>= 2.50.2')
libsecret_dep        = dependency('libsecret-1', version: '>= 0.20')
libproxy_dep         = dependency('libproxy-1.0', version: '>= 0.5')
malcontent_dep 	     = dependency('malcontent-0', version: '>= 0.12.0')


dl_worker_sources = [
  'bz-env.c',
  'bz-global-net.c',
  'dl-worker.c',
]

dl_worker_deps = [
  math,
  libdex_dep,
  libsoup_dep,
  json_glib_dep,
  libproxy_dep,
]

dl_worker_exe = executable(dl_worker_bin_name, dl_worker_sources,
  dependencies: dl_worker_deps,
       install: true,
)


marshalers = gnome.genmarshal('bz-marshalers',
  sources: 'bz-marshalers.list',
  prefix: 'bz_marshal',
  valist_marshallers: true,
)

gdbus_src = gnome.gdbus_codegen(
  'gs-shell-search-provider-generated',
  'shell-search-provider-dbus-interfaces.xml',
  interface_prefix : 'org.gnome.',
  namespace : 'Bz',
)

bz_sources = files(
  'bz-addon-tile.c',
  'bz-addons-dialog.c',
  'bz-age-rating-dialog.c',
  'bz-all-apps-page.c',
  'bz-app-permissions.c',
  'bz-app-size-dialog.c',
  'bz-app-tile.c',
  'bz-application-map-factory.c',
  'bz-application.c',
  'bz-apps-page.c',
  'bz-appstream-description-render.c',
  'bz-appstream-parser.c',
  'bz-async-texture.c',
  'bz-auth-state.c',
  'bz-backend.c',
  'bz-category-flags.c',
  'bz-category-tile.c',
  'bz-content-provider.c',
  'bz-context-row.c',
  'bz-context-tile.c',
  'bz-context-tile-callbacks.c',
  'bz-curated-app-tile.c',
  'bz-curated-view.c',
  'bz-data-graph.c',
  'bz-decorated-screenshot.c',
  'bz-developer-badge.c',
  'bz-donations-dialog.c',
  'bz-download-worker.c',
  'bz-dynamic-list-view.c',
  'bz-entry-cache-manager.c',
  'bz-entry-group-util.c',
  'bz-entry-group.c',
  'bz-entry-inspector.c',
  'bz-entry-selection-row.c',
  'bz-entry.c',
  'bz-env.c',
  'bz-error-dialog.c',
  'bz-error.c',
  'bz-fading-clamp.c',
  'bz-favorite-button.c',
  'bz-favorites-page.c',
  'bz-favorites-tile.c',
  'bz-featured-carousel.c',
  'bz-featured-tile.c',
  'bz-flathub-category-section.c',
  'bz-flathub-category.c',
  'bz-flathub-category.c',
  'bz-flathub-page.c',
  'bz-flathub-state.c',
  'bz-flatpak-entry.c',
  'bz-flatpak-instance.c',
  'bz-full-view.c',
  'bz-global-net.c',
  'bz-gnome-shell-search-provider.c',
  'bz-group-tile-css-watcher.c',
  'bz-hardware-support-dialog.c',
  'bz-hooks.c',
  'bz-inhibited-scrollable.c',
  'bz-inspector.c',
  'bz-install-controls.c',
  'bz-installed-tile.c',
  'bz-io.c',
  'bz-library-page.c',
  'bz-license-dialog.c',
  'bz-list-tile.c',
  'bz-login-page.c',
  'bz-lozenge.c',
  'bz-malcontent-service.c',
  'bz-metainfo-preview.c',
  'bz-newline-parser.c',
  'bz-parser.c',
  'bz-preferences-dialog.c',
  'bz-progress-bar.c',
  'bz-releases-list.c',
  'bz-result.c',
  'bz-rich-app-tile.c',
  'bz-rounded-picture.c',
  'bz-row-view.c',
  'bz-safety-calculator.c',
  'bz-safety-dialog.c',
  'bz-screenshot-page.c',
  'bz-screenshot.c',
  'bz-screenshots-carousel.c',
  'bz-search-engine.c',
  'bz-search-filter-popover.c',
  'bz-search-pill-list.c',
  'bz-search-page.c',
  'bz-section-view.c',
  'bz-serializable.c',
  'bz-share-list.c',
  'bz-spdx.c',
  'bz-stats-dialog.c',
  'bz-subcategory-list.c',
  'bz-tag-list.c',
  'bz-template-callbacks.c',
  'bz-themed-entry-group-rect.c',
  'bz-transaction-dialog.c',
  'bz-transaction-list-dialog.c',
  'bz-transaction-manager.c',
  'bz-transaction-tile.c',
  'bz-transaction.c',
  'bz-updates-card.c',
  'bz-user-data-page.c',
  'bz-user-data-tile.c',
  'bz-window.c',
  'bz-world-map-parser.c',
  'bz-world-map.c',
  'bz-yaml-parser.c',
  'bz-zoom.c',
)
subdir('progress-bar-designs')

bz_deps = [
  # Bazaar GTK Extensions; see ../bge/
  bge_dep,

  math,
  gtk_dep,
  libadwaita_dep,
  flatpak_dep,
  appstream_dep,
  xmlb_dep,
  yaml_dep,
  libsoup_dep,
  json_glib_dep,
  libdex_dep,
  glycin_dep,
  glycin_gtk4_dep,
  webkit_dep,
  libsecret_dep,
  libproxy_dep,
  malcontent_dep,
]

gen_gobject = find_program('./gen_gobject.sh')
gen_gobject_header = generator(
  gen_gobject,
  output: '@BASENAME@.h',
  arguments: ['--header', '@INPUT@', '@OUTPUT@'],
)
gen_gobject_code = generator(
  gen_gobject,
  output: '@BASENAME@.c',
  arguments: ['--code', '@INPUT@', '@OUTPUT@'],
)

gobject_specs = files(
  'bz-age-rating-attribute.txt',
  'bz-backend-notification.txt',
  'bz-backend-transaction-op-payload.txt',
  'bz-backend-transaction-op-progress-payload.txt',
  'bz-blocklist-condition-match-envvar.txt',
  'bz-blocklist-condition-match-locale.txt',
  'bz-blocklist-condition.txt',
  'bz-blocklist.txt',
  'bz-bulk-install-dialog-result.txt',
  'bz-comet.txt',
  'bz-country-data-point.txt',
  'bz-country.txt',
  'bz-curated-category-info.txt',
  'bz-curated-image-info.txt',
  'bz-curated-markdown-info.txt',
  'bz-curated-row.txt',
  'bz-curated-section.txt',
  'bz-data-point.txt',
  'bz-exponential-function.txt',
  'bz-finished-search-query.txt',
  'bz-flathub-auth-provider.txt',
  'bz-flathub-sub-category.txt',
  'bz-hash-table-object.txt',
  'bz-hook-dialog-option.txt',
  'bz-hook-dialog.txt',
  'bz-hook.txt',
  'bz-internal-config.txt',
  'bz-linear-function.txt',
  'bz-main-config.txt',
  'bz-pride-flag-config.txt',
  'bz-pride-flag-spec.txt',
  'bz-pride-flag-stripe-spec.txt',
  'bz-release.txt',
  'bz-repository.txt',
  'bz-root-blocklist.txt',
  'bz-root-curated-config.txt',
  'bz-safety-row.txt',
  'bz-search-bias.txt',
  'bz-search-result.txt',
  'bz-state-info.txt',
  'bz-transaction-dialog-result.txt',
  'bz-transaction-entry-tracker.txt',
  'bz-transaction-task.txt',
  'bz-url.txt',
  'bz-verification-status.txt',
)

gen_gobject_srcs = []
foreach f : gobject_specs
  header = gen_gobject_header.process(f)
  code = gen_gobject_code.process(f)
  gen_gobject_srcs += [header, code]
endforeach

generated_gobjects = declare_dependency(
  sources: gen_gobject_srcs,
)
bz_deps += [ generated_gobjects ]

blueprints = custom_target('blueprints',
  input: files(
    'bz-addon-tile.blp',
    'bz-addons-dialog.blp',
    'bz-donations-dialog.blp',
    'bz-age-rating-dialog.blp',
    'bz-all-apps-page.blp',
    'bz-app-size-dialog.blp',
    'bz-app-tile.blp',
    'bz-apps-page.blp',
    'bz-appstream-description-render.blp',
    'bz-category-tile.blp',
    'bz-context-tile.blp',
    'bz-curated-app-tile.blp',
    'bz-curated-view.blp',
    'bz-decorated-screenshot.blp',
    'bz-developer-badge.blp',
    'bz-entry-inspector.blp',
    'bz-entry-selection-row.blp',
    'bz-error-dialog.blp',
    'bz-favorite-button.blp',
    'bz-favorites-page.blp',
    'bz-favorites-tile.blp',
    'bz-featured-carousel.blp',
    'bz-featured-tile.blp',
    'bz-flathub-category-section.blp',
    'bz-flathub-page.blp',
    'bz-full-view.blp',
    'bz-hardware-support-dialog.blp',
    'bz-inspector.blp',
    'bz-install-controls.blp',
    'bz-installed-tile.blp',
    'bz-library-page.blp',
    'bz-license-dialog.blp',
    'bz-login-page.blp',
    'bz-preferences-dialog.blp',
    'bz-progress-bar.blp',
    'bz-releases-dialog.blp',
    'bz-releases-list.blp',
    'bz-rich-app-tile.blp',
    'bz-row-view.blp',
    'bz-safety-dialog.blp',
    'bz-screenshot-page.blp',
    'bz-screenshots-carousel.blp',
    'bz-search-page.blp',
    'bz-search-filter-popover.blp',
    'bz-section-view.blp',
    'bz-stats-dialog.blp',
    'bz-transaction-list-dialog.blp',
    'bz-transaction-tile.blp',
    'bz-updates-card.blp',
    'bz-user-data-page.blp',
    'bz-user-data-tile.blp',
    'bz-window.blp',
    'shortcuts-dialog.blp',
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

release_notes = custom_target('release-notes.xml',
  output: 'release-notes.xml',
  input: meson.project_source_root() / 'data' / 'io.github.kolunmi.Bazaar.metainfo.xml.in',
  command: [
    'sh', '-c',
    xmllint.full_path() + ' --xpath "//releases/release[1]" "$1" > "$2"',
    '--',
    '@INPUT@',
    '@OUTPUT@',
  ],
)

countries_gvariant = custom_target(
  'countries-gvariant',
  input: files('../scripts/countries.json.in'),
  output: 'countries.gvariant',
  command: [
    python3,
    meson.project_source_root() / 'scripts' / 'countries.py',
    '@INPUT@',
    meson.project_source_root() / 'po' / 'LINGUAS',
    '@OUTPUT@',
  ],
)

bz_sources += gnome.compile_resources('bz-resources',
  'bazaar.gresource.xml',
  c_name: 'bz',
  source_dir: meson.current_build_dir(),
  dependencies: [blueprints, release_notes, countries_gvariant],
)

executable('bazaar', bz_sources + ['main.c'], gdbus_src, marshalers,
           dependencies: bz_deps,
           install: true,
)

executable(refresh_worker_bin_name,
           bz_sources + ['refresh-worker.c'],
           gdbus_src, marshalers,
           dependencies: bz_deps,
           install: true,
)
