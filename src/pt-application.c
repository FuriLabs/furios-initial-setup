/*
 * Copyright (C) 2022 Purism SPC
 *               2023-2024 Guido Günther
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "phosh-tour-config.h"

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


static const GOptionEntry entries[] = {
  { "version", '\0', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
    NULL, "Get the current version", NULL,
  },
  G_OPTION_ENTRY_NULL
};


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
pt_application_show_about (GSimpleAction *action,
                                   GVariant      *parameter,
                                   gpointer       user_data)
{
  PtApplication *self = PT_APPLICATION (user_data);
  GtkWindow *window = NULL;
  const char *authors[] = {"Guido Günther", NULL};
  const char *artists[] = {"Sam Hewitt ", NULL};

  g_return_if_fail (PT_IS_APPLICATION (self));

  window = gtk_application_get_active_window (GTK_APPLICATION (self));

  gtk_show_about_dialog (window,
                         "artists", artists,
                         "authors", authors,
                         "license-type", GTK_LICENSE_GPL_3_0,
                         "logo-icon-name", "mobi.phosh.PhoshTour",
                         "program-name", PHOSH_TOUR_BRAND "Tour",
                         "title", _("About " PHOSH_TOUR_BRAND " "),
                         "translator-credits", _ ("translator-credits"),
                         "version", PHOSH_TOUR_VERSION,
                         "website", "https://gitlab.gnome.org/World/Phosh/phosh-tour",
                         NULL);
}


static void
pt_application_init (PtApplication *self)
{
  g_autoptr (GSimpleAction) about_action = NULL;
  g_autoptr (GSimpleAction) quit_action = NULL;

  quit_action = g_simple_action_new ("quit", NULL);
  g_signal_connect_swapped (quit_action, "activate", G_CALLBACK (g_application_quit), self);
  g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (quit_action));

  about_action = g_simple_action_new ("about", NULL);
  g_signal_connect (about_action, "activate", G_CALLBACK (pt_application_show_about), self);
  g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (about_action));

  gtk_application_set_accels_for_action (GTK_APPLICATION (self),
                                         "app.quit",
                                         (const char *[]) {
    "<primary>q",
    NULL,
  });

  g_application_set_option_context_parameter_string (G_APPLICATION (self), DESC);
  g_application_add_main_option_entries (G_APPLICATION (self), entries);
}
