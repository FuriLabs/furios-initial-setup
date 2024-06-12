furios_initial_setup_enum_sources = gnome.mkenums_simple('ms-enum-types',
     sources : files('phosh-config-enums.h'))

furios_initial_setup_sources = [
  'main.c',
  'cc-util.c',
  'cc-util.h',
  'cc-common-language.c',
  'cc-common-language.h',
  'cc-language-chooser.c',
  'cc-language-chooser.h',
  'cc-network-list.c',
  'cc-network-list.h',
  'pt-security-settings.c',
  'pt-security-settings.h',
  'cc-online-account-provider-row.c',
  'cc-online-account-provider-row.h',
  'cc-online-account-row.c',
  'cc-online-account-row.h',
  'pt-online-accounts.c',
  'pt-online-accounts.h',
  'pt-application.h',
  'pt-application.c',
  'pt-window.h',
  'pt-window.c',
  'pt-page.h',
  'pt-page.c',
  'pt-hw-page.h',
  'pt-hw-page.c',
  'run-passwd.h',
  'run-passwd.c',
]

furios_initial_setup_deps = [
  gio_dep,
  glib_dep,
  gmobile_dep,
  gtk_dep,
  adwaita_dep,
  dependency ('gnome-desktop-4'),
  dependency ('fontconfig'),
  dependency ('accountsservice'),
  dependency ('libnm', version: '>= 1.2'),
  dependency ('libnma-gtk4', version: '>= 1.0'),
  dependency ('goa-backend-1.0', version: '>= 3.49.0'),
]

gnome = import('gnome')

furios_initial_setup_sources += gnome.compile_resources('furios-initial-setup-resources',
  'furios-initial-setup.gresource.xml',
  c_name: 'furios_initial_setup'
)

executable('furios-initial-setup', furios_initial_setup_sources,
  dependencies: furios_initial_setup_deps,
  install: true,
)
