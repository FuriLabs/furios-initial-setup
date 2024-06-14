project('furios-initial-setup', 'c',
          version: '0.39.0',
    meson_version: '>= 0.58.0',
  default_options: [ 'warning_level=2',
                     'werror=false',
                     'c_std=gnu11',
                   ],
)

application_id = 'io.furios.InitialSetup'
prefix = get_option('prefix')
libdir = join_paths(prefix, get_option('libdir'))
plugins_dir = join_paths(prefix, libdir, 'phosh', 'plugins')

glib_ver = '2.74'
glib_ver_str = 'GLIB_VERSION_@0@'.format(glib_ver.replace('.','_'))
glib_ver_cmp = '>=@0@'.format(glib_ver)

gtk_ver = '4.12'
gtk_ver_str = 'GDK_VERSION_@0@'.format(gtk_ver.replace('.','_'))
gtk_ver_cmp = '>=@0@'.format(gtk_ver)

adw_ver = '1.4'
adw_ver_str = 'ADW_VERSION_@0@'.format(adw_ver.replace('.','_'))
adw_ver_cmp = '>=@0@'.format(adw_ver)

glib_dep = dependency('glib-2.0', version: glib_ver_cmp)
gio_dep = dependency('gio-2.0', version: glib_ver_cmp)
gtk_dep = dependency('gtk4', version: gtk_ver_cmp)
adwaita_dep = dependency('libadwaita-1', version: adw_ver_cmp)
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
global_c_args = []
test_c_args = [
  '-Wcast-align',
  '-Wdate-time',
  '-Wdeclaration-after-statement',
  ['-Werror=format-security', '-Werror=format=2'],
  '-Wendif-labels',
  '-Werror=incompatible-pointer-types',
  '-Werror=missing-declarations',
  '-Werror=overflow',
  '-Werror=return-type',
  '-Werror=shift-count-overflow',
  '-Werror=shift-overflow=2',
  '-Werror=implicit-fallthrough=3',
  '-Wfloat-equal',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Winit-self',
  '-Wmaybe-uninitialized',
  '-Wmissing-field-initializers',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wnested-externs',
  '-Wno-missing-field-initializers',
  '-Wno-sign-compare',
  '-Wno-strict-aliasing',
  '-Wno-unused-parameter',
  '-Wold-style-definition',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wshadow',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wswitch-enum',
  '-Wtype-limits',
  '-Wundef',
  '-Wunused-function',
]
if get_option('buildtype') != 'plain'
  test_c_args += '-fstack-protector-strong'
endif

foreach arg: test_c_args
  if cc.has_multi_arguments(arg)
    global_c_args += arg
  endif
endforeach
add_project_arguments(
  [global_c_args,
   '-DGLIB_VERSION_MIN_REQUIRED=@0@'.format(glib_ver_str),
   '-DGLIB_VERSION_MAX_ALLOWED=@0@'.format(glib_ver_str),
   '-DGDK_VERSION_MIN_REQUIRED=@0@'.format(gtk_ver_str),
   '-DGDK_VERSION_MAX_ALLOWED=@0@'.format(gtk_ver_str),
   '-DADW_VERSION_MIN_REQUIRED=@0@'.format(adw_ver_str),
   '-DADW_VERSION_MAX_ALLOWED=@0@'.format(adw_ver_str),
  ],
  language: 'c'
)


config_h = configuration_data()
config_h.set_quoted('GETTEXT_PACKAGE', 'furios-initial-setup')
config_h.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))
config_h.set_quoted('PHOSH_TOUR_APP', meson.project_name())
config_h.set_quoted('PHOSH_TOUR_BRAND', get_option('brand'))
config_h.set_quoted('PHOSH_TOUR_URL', get_option('url'))
config_h.set_quoted('PHOSH_TOUR_VENDOR', get_option('vendor'))
config_h.set_quoted('PHOSH_TOUR_VERSION', meson.project_version())
config_h.set_quoted('PHOSH_TOUR_APP_ID', application_id)

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
install_data('systemd/60-initial-setup.conf', install_dir: '/usr/lib/systemd/system/phosh.service.d/')

summary({
     'Brand': get_option('brand'),
     'Vendor': get_option('vendor'),
  },
  bool_yn: true,
  section: 'Config',
)

# gnome.post_install() is available since meson 0.59.0
# Distributions use their own tooling (e.g. postinst, triggers, etc)
# so it is okay if the post_install() is not run on distro builds
if meson.version().version_compare('>=0.59.0')
  gnome.post_install(
    glib_compile_schemas: true,
    gtk_update_icon_cache: true,
    update_desktop_database: true,
  )
endif
