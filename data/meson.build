desktop_conf = configuration_data()
desktop_conf.set('BRAND', get_option('brand'))

desktop_file = i18n.merge_file(
  input: configure_file(
    input: files('io.furios.InitialSetup.desktop.in.in'),
    output: 'io.furios.InitialSetup.desktop.in',
    configuration: desktop_conf
  ),
  output: 'io.furios.InitialSetup.desktop',
  type: 'desktop',
  po_dir: '../po',
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications')
)

appstream_file = i18n.merge_file(
  input: configure_file(
    input: files('io.furios.InitialSetup.metainfo.xml.in.in'),
    output: 'io.furios.InitialSetup.metainfo.xml.in',
    configuration: desktop_conf
  ),
  output: 'io.furios.InitialSetup.metainfo.xml',
  po_dir: '../po',
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo')
)

appstream_util = find_program('appstreamcli', required: false)
if appstream_util.found()
  test('Validate appstream file', appstream_util,
    args: ['validate', '--no-net', appstream_file]
  )
endif

install_data('io.furios.InitialSetup.gschema.xml',
  install_dir: join_paths(get_option('datadir'), 'glib-2.0/schemas')
)

compile_schemas = find_program('glib-compile-schemas', required: false)
if compile_schemas.found()
  test('Validate schema file', compile_schemas,
    args: ['--strict', '--dry-run', meson.current_source_dir()]
  )
endif

subdir('icons')
