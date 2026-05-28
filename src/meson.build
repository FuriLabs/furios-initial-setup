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
  'pt-security-settings-fingerprint.c',
  'pt-security-settings-face.c',
  'pt-security-settings-private.h',
  'pt-application.h',
  'pt-application.c',
  'pt-window.h',
  'pt-window.c',
  'pt-page.h',
  'pt-page.c',
  'run-passwd.h',
  'run-passwd.c',
  'pt-update-progress.c',
  'pt-update-progress.h',
  'ed25519/add_scalar.c',
  'ed25519/fe.c',
  'ed25519/ge.c',
  'ed25519/key_exchange.c',
  'ed25519/keypair.c',
  'ed25519/sc.c',
  'ed25519/seed.c',
  'ed25519/sha512.c',
  'ed25519/sign.c',
  'ed25519/verify.c',
]

furios_initial_setup_deps = [
  gio_dep,
  glib_dep,
  gmobile_dep,
  gtk_dep,
  adwaita_dep,
  gst_dep,
  gnome_desktop_dep,
  fontconfig_dep,
  accountsservice_dep,
  libnm_dep,
  libnma_dep,
  libsoup_dep,
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
