/*
 * Copyright (C) 2022 Purism SPC
 *               2023-2024 Guido Günther
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pt-window"

#include "furios-initial-setup-config.h"
#include "pt-application.h"
#include "pt-hw-page.h"
#include "pt-window.h"
#include "pt-page.h"

#define GMOBILE_USE_UNSTABLE_API
#include <gmobile/gmobile.h>
#include <glib/gi18n.h>

#include "cc-language-chooser.h"
#include "cc-network-list.h"
#include "cc-online-account-provider-row.h"
#include "cc-online-account-row.h"
#include "pt-online-accounts.h"
#include "pt-security-settings.h"

enum {
  PROP_0,
  PROP_WAYDROID_AUTOSTART,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];


struct _PtWindow {
  AdwApplicationWindow parent_instance;

  AdwCarousel *main_carousel;
  GtkCssProvider *theme_transition_provider;
  guint transition_disable_timeout;
  gboolean waydroid_autostart;

  int pending_commits;
};

G_DEFINE_TYPE (PtWindow, pt_window, ADW_TYPE_APPLICATION_WINDOW)


static void
goto_page (PtWindow *self, int num)
{
  int n_pages = adw_carousel_get_n_pages (self->main_carousel);
  GtkWidget *page;

  if (num < 0)
    return;

  if (num >= n_pages)
    return;

  page = adw_carousel_get_nth_page (self->main_carousel, num);
  adw_carousel_scroll_to (self->main_carousel, page, TRUE);
}


static void
on_flip_page_activated (GtkWidget *widget, const char *action_name, GVariant *param)
{
  PtWindow *self = PT_WINDOW (widget);
  int num = adw_carousel_get_position (self->main_carousel);
  gint32 offset;

  offset = g_variant_get_int32 (param);
  goto_page (self, num + offset);
}


static gboolean
get_btn_next_visible (GObject *object, double position, int n_pages)
{
  if ((position + 0.5) > (n_pages - 1))
    return FALSE;

  return TRUE;
}


static gboolean
get_btn_previous_visible (GObject *object, double position)
{
  // printf("Checking visibility: %f\n", position);
  if (position < 0.5)
    return FALSE;

  return TRUE;
}


static gboolean
get_btn_next_sensitive (GObject *object, AdwCarousel *carousel, double position)
{
  PtPage *page = PT_PAGE (adw_carousel_get_nth_page (carousel, position));

  return pt_page_get_can_proceed (page);
}


static gboolean
get_btn_previous_sensitive (GObject *object, AdwCarousel *carousel, double position)
{
  return TRUE;
}


static gboolean
pt_disable_transition_style (PtWindow *self)
{
  GtkCssProvider *provider = self->theme_transition_provider;
  gtk_css_provider_load_from_string (provider, "");

  self->transition_disable_timeout = 0;

  return G_SOURCE_REMOVE;
}


static void
pt_enable_transition_style (PtWindow *self)
{
  GtkCssProvider *provider = self->theme_transition_provider;
  static const char *transition_css =
    "window {\n \
      transition: background-color 0.2s ease-in, color 0.2s ease-in;\n \
    }";

  gtk_css_provider_load_from_string (provider, transition_css);

  if (self->transition_disable_timeout)
    g_source_remove (self->transition_disable_timeout);

  self->transition_disable_timeout = g_timeout_add (250, (GSourceFunc) pt_disable_transition_style, self);
}

static void
pt_set_dark_mode (GtkToggleButton *btn, gpointer user_data)
{
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (btn));
  PtWindow *self = PT_WINDOW (root);
  GtkSettings *settings = gtk_settings_get_default ();

  pt_enable_transition_style (self);
  g_object_set (settings, "gtk-theme-name", "adw-gtk3-dark", NULL);
}

static void
pt_set_default_mode (GtkToggleButton *btn, gpointer user_data)
{
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (btn));
  PtWindow *self = PT_WINDOW (root);
  GtkSettings *settings = gtk_settings_get_default ();

  pt_enable_transition_style (self);
  g_object_set (settings, "gtk-theme-name", "adw-gtk3", NULL);
}

static void
pt_check_should_exit (PtWindow *self)
{
  if (self->pending_commits == 0) {
    g_debug ("Everything went well. See you never again!\n");
    gtk_window_close (GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (self))));
  }
}

static void
pt_on_security_settings_applied (PtSecuritySettings *security_settings, gboolean ok, gpointer user_data)
{
  PtPage *page = PT_PAGE (user_data);
  PtWindow *self = PT_WINDOW (gtk_widget_get_root (GTK_WIDGET (page)));

  if (ok) {
    self->pending_commits--;
    g_debug ("Security settings applied\n");
    pt_check_should_exit (self);
  } else {
    adw_carousel_scroll_to (self->main_carousel, GTK_WIDGET (page), TRUE);
  }
}

static void
pt_commit_security_settings (PtPage *page, gpointer user_data)
{
  pt_security_settings_apply (G_OBJECT (pt_page_get_widget (page)), (ApplyCallback) pt_on_security_settings_applied, page);
}

{
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (page));
  PtWindow *self = PT_WINDOW (root);

static void
pt_commit_language_settings (PtPage *page, gpointer user_data)
{
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (page));
  PtWindow *self = PT_WINDOW (root);
  GtkWidget *language_chooser = pt_page_get_widget (page);

  cc_language_chooser_apply (CC_LANGUAGE_CHOOSER (language_chooser));

  self->pending_commits--;
  pt_check_should_exit (self);
}

static void
pt_commit_all (PtPage *final_page)
{
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (final_page));
  PtWindow *self = PT_WINDOW (root);

  self->pending_commits = 0;

  int n_pages = adw_carousel_get_n_pages (self->main_carousel);
  int i;

  for (i = 0; i < n_pages; i++) {
    PtPage *page = PT_PAGE (adw_carousel_get_nth_page (self->main_carousel, i));
    if (g_signal_handler_find (page, G_SIGNAL_MATCH_ID, g_signal_lookup ("apply-changes", G_OBJECT_TYPE (page)), 0, NULL, NULL, NULL)) {
      self->pending_commits++;

      g_signal_emit_by_name (page, "apply-changes");
    }
  }

  pt_check_should_exit (self);
}

static const char *SCREEN_SCALES[] = {"1", "1.25", "1.5", "1.75", "2", "2.25", "2.5", "2.75", "3"};
static const int SCREEN_SCALES_COUNT = G_N_ELEMENTS (SCREEN_SCALES);

static gboolean
pt_set_scaling (GtkScale *scale)
{
  int value = gtk_range_get_value (GTK_RANGE (scale));
  const char *command;

  // Don't change the size from under the user
  if (gtk_widget_get_state_flags (GTK_WIDGET (scale)) & GTK_STATE_FLAG_ACTIVE) {
    g_timeout_add (100, (GSourceFunc) pt_set_scaling, scale);
    return G_SOURCE_REMOVE;
  }

  if (value < 0 || value >= SCREEN_SCALES_COUNT) {
    g_warning ("Invalid scaling value %d", value);
    return G_SOURCE_REMOVE;
  }

  // This is not super nice
  command = g_strdup_printf ("wlr-randr --output HWCOMPOSER-1 --scale %s", SCREEN_SCALES[value]);
  g_spawn_command_line_async (command, NULL);

  return G_SOURCE_REMOVE;
}

static void pt_window_set_property (GObject *object,
                                        guint         property_id,
                                        const GValue *value,
                                        GParamSpec *pspec)
{
  PtWindow *self = PT_WINDOW (object);

  switch (property_id) {
  case PROP_WAYDROID_AUTOSTART:
    self->waydroid_autostart = g_value_get_boolean (value);
    if (self->waydroid_autostart)
      g_file_set_contents (g_build_filename (g_get_home_dir (), ".android_enable", NULL), "", 0, NULL);
    else
      unlink (g_build_filename (g_get_home_dir (), ".android_enable", NULL));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void pt_window_get_property (GObject *object,
                                         guint property_id,
                                         GValue *value,
                                         GParamSpec *pspec)
{
  PtWindow *self = PT_WINDOW (object);

  switch (property_id) {
  case PROP_WAYDROID_AUTOSTART:
    g_value_set_boolean (value, self->waydroid_autostart);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
pt_window_class_init (PtWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_ensure (CC_TYPE_LANGUAGE_CHOOSER);
  g_type_ensure (CC_TYPE_NETWORK_LIST);
  g_type_ensure (CC_TYPE_ONLINE_ACCOUNT_ROW);
  g_type_ensure (CC_TYPE_ONLINE_ACCOUNT_PROVIDER_ROW);
  g_type_ensure (PT_TYPE_SECURITY_SETTINGS);
  g_type_ensure (PT_TYPE_ONLINE_ACCOUNTS);
  g_type_ensure (PT_TYPE_PAGE);
  g_type_ensure (PT_TYPE_HW_PAGE);

  props[PROP_WAYDROID_AUTOSTART] =
    g_param_spec_boolean ("waydroid-autostart",
                          "Waydroid autostart",
                          "Whether to autostart Waydroid",
                          FALSE,
                          G_PARAM_READWRITE);

  object_class->set_property = pt_window_set_property;
  object_class->get_property = pt_window_get_property;
  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/PhoshTour/ui/pt-window.ui");
  gtk_widget_class_bind_template_child (widget_class, PtWindow, main_carousel);

  gtk_widget_class_bind_template_callback (widget_class, get_btn_next_visible);
  gtk_widget_class_bind_template_callback (widget_class, get_btn_previous_visible);
  gtk_widget_class_bind_template_callback (widget_class, get_btn_next_sensitive);
  gtk_widget_class_bind_template_callback (widget_class, get_btn_previous_sensitive);
  gtk_widget_class_bind_template_callback (widget_class, pt_set_dark_mode);
  gtk_widget_class_bind_template_callback (widget_class, pt_set_default_mode);
  gtk_widget_class_bind_template_callback (widget_class, pt_set_scaling);
  gtk_widget_class_bind_template_callback (widget_class, pt_commit_language_settings);
  gtk_widget_class_bind_template_callback (widget_class, pt_commit_security_settings);
  gtk_widget_class_bind_template_callback (widget_class, pt_commit_all);

  gtk_widget_class_install_action (widget_class, "win.flip-page", "i", on_flip_page_activated);

}

static void
pt_window_init (PtWindow *self)
{
  g_autoptr (GtkCssProvider) css_provider = gtk_css_provider_new ();

  gtk_css_provider_load_from_resource (css_provider, "/mobi/phosh/PhoshTour/style.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (css_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  self->theme_transition_provider = gtk_css_provider_new ();

  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (self->theme_transition_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  gtk_widget_init_template (GTK_WIDGET (self));

  self->waydroid_autostart = g_file_test (g_build_filename (g_get_home_dir (), ".android_enable", NULL),
                                          G_FILE_TEST_EXISTS); 

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_WAYDROID_AUTOSTART]);
}
