/*
 * Copyright (C) 2022 Purism SPC
 *               2023-2024 Guido Günther
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "furios-initial-setup-config.h"

#include "pt-application.h"
#include "pt-window.h"

#include <locale.h>
#include <glib/gi18n.h>

#include "cc-common-language.h"

#define DESC _("- A graphical tour introducing your device")

struct _PtApplication {
  GtkApplication parent_instance;
};

G_DEFINE_TYPE (PtApplication, pt_application, ADW_TYPE_APPLICATION)



PtApplication *
pt_application_new (char *application_id, GApplicationFlags flags)
{
  return g_object_new (PT_TYPE_APPLICATION,
                       "application-id", application_id,
                       "flags", flags,
                       NULL);
}


static void
pt_application_activate (GApplication *app)
{
  GtkWindow *window;

  g_assert (GTK_IS_APPLICATION (app));

  cc_common_language_set_current_language (setlocale (LC_MESSAGES, NULL));
  // Reset back to the C locale. We do this so that the UI is loaded with its
  // original language, so we can stash away the original string and dynamically
  // translate it later.
  setlocale (LC_MESSAGES, "C");

  window = gtk_application_get_active_window (GTK_APPLICATION (app));
  if (window == NULL)
    window = g_object_new (PT_TYPE_WINDOW, "application", app, NULL);

  gtk_window_present (window);
}


static int
pt_application_handle_local_options (GApplication *app, GVariantDict *options)
{
  if (g_variant_dict_contains (options, "version")) {
    g_print ("%s %s %s\n", PHOSH_TOUR_APP, PHOSH_TOUR_VERSION, DESC);

    return 0;
  }

  return G_APPLICATION_CLASS (pt_application_parent_class)->handle_local_options (app, options);
}


static void
pt_application_class_init (PtApplicationClass *klass)
{
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  app_class->activate = pt_application_activate;
  app_class->handle_local_options = pt_application_handle_local_options;
}


static void
pt_application_init (PtApplication *self)
{
  g_autoptr (GtkCssProvider) css_provider = gtk_css_provider_new ();

  gtk_css_provider_load_from_resource (css_provider, "/mobi/phosh/PhoshTour/style.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (css_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}
