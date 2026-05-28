project(
    'furios-initial-setup',
    ['c'],
    version: '1.0.0',
    meson_version: '>=0.58'
)

prefix = get_option('prefix')
libdir = join_paths(prefix, get_option('libdir'))

glib_dep = dependency('glib-2.0', version: '>=2.74')
gio_dep = dependency('gio-2.0', version: '>=2.74')
gtk_dep = dependency('gtk4', version: '>=4.12')
adwaita_dep = dependency('libadwaita-1', version: '>=1.4')
gst_dep = dependency('gstreamer-1.0')
gnome_desktop_dep = dependency('gnome-desktop-4')
fontconfig_dep = dependency('fontconfig')
accountsservice_dep = dependency('accountsservice')
libnm_dep = dependency('libnm', version: '>= 1.2')
libnma_dep = dependency('libnma-gtk4', version: '>= 1.0')
libsoup_dep = dependency('libsoup-3.0', version: '>= 3.0')

gmobile_dep = dependency('gmobile',
  version: '>= 0.1.0',
  fallback: ['gmobile', 'gmobile_dep'],
  default_options: [ 'examples=false',
                     'introspection=false',
                     'gtk_doc=false',
                     'tests=false',
  ])

i18n = import('i18n')

gnome = import('gnome')

cc = meson.get_compiler('c')

config_h = configuration_data()
config_h.set_quoted('GETTEXT_PACKAGE', 'furios-initial-setup')
config_h.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))
config_h.set_quoted('INITIAL_SETUP_APP', meson.project_name())
config_h.set_quoted('INITIAL_SETUP_VERSION', meson.project_version())
config_h.set_quoted('INITIAL_SETUP_APP_ID', 'io.furios.InitialSetup')

configure_file(
  output: 'furios-initial-setup-config.h',
  configuration: config_h,
)
add_project_arguments([
  '-I' + meson.project_build_root(),
], language: 'c')

subdir('data')
subdir('src')
subdir('po')

install_data('systemd/furios-initial-setup', install_dir: '/usr/libexec/')
install_data('systemd/furios-initial-setup-inner', install_dir: '/usr/libexec/')
install_data('systemd/furios-initial-setup.desktop', install_dir: '/etc/xdg/autostart/')
install_data('systemd/60-initial-setup.conf', install_dir: '/usr/lib/systemd/system/phosh.service.d/')

if meson.version().version_compare('>=0.59.0')
  gnome.post_install(
    glib_compile_schemas: true,
    gtk_update_icon_cache: true,
    update_desktop_database: true,
  )
endif
