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
#include "pt-window.h"
#include "pt-page.h"

#define GMOBILE_USE_UNSTABLE_API
#include <gmobile.h>
#include <glib/gi18n.h>

#include "cc-language-chooser.h"
#include "cc-network-list.h"
#include "pt-security-settings.h"
#include "pt-update-progress.h"

#include <gsettings-desktop-schemas/gdesktop-enums.h>
#define INTERFACE_PATH_ID "org.gnome.desktop.interface"
#define INTERFACE_COLOR_SCHEME_KEY "color-scheme"
#define INTERFACE_ACCENT_COLOR_KEY "accent-color"

static const char *SCREEN_SCALES[] = {"1", "1.25", "1.5", "1.75", "2", "2.25", "2.5", "2.75", "3"};
static const int SCREEN_SCALES_COUNT = G_N_ELEMENTS (SCREEN_SCALES);

enum {
  PROP_0,
  PROP_ANDROID_AUTOSTART,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PtWindow {
  AdwApplicationWindow parent_instance;

  AdwCarousel         *main_carousel;
  GtkCssProvider      *theme_transition_provider;
  guint                transition_disable_timeout;
  gboolean             android_autostart;

  int                  pending_commits;

  GSettings           *interface_settings;
  gdouble              last_position;

  GtkWidget           *accent_box;

  PtUpdateProgress    *update_progress;
  GtkWidget           *update_page;
  AdwBanner           *skip_updates_banner;

  GtkWidget           *password_page;
  gboolean             updates_skipped;
};

G_DEFINE_TYPE (PtWindow, pt_window, ADW_TYPE_APPLICATION_WINDOW)

static void
goto_page (PtWindow *self, int num)
{
  int n_pages;
  GtkWidget *page;

  if (!self->main_carousel)
    return;

  n_pages = adw_carousel_get_n_pages (ADW_CAROUSEL (self->main_carousel));

  if (num < 0)
    return;

  if (num >= n_pages)
    return;

  page = adw_carousel_get_nth_page (ADW_CAROUSEL (self->main_carousel), num);
  adw_carousel_scroll_to (ADW_CAROUSEL (self->main_carousel), page, TRUE);
}

static void
on_flip_page_activated (GtkWidget *widget, const char *action_name, GVariant *param)
{
  PtWindow *self = PT_WINDOW (widget);
  int num;
  gint32 offset;

  num = adw_carousel_get_position (self->main_carousel);
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
  if (position < 0.5)
    return FALSE;

  return TRUE;
}

static gboolean
get_btn_next_sensitive (GObject *object, AdwCarousel *carousel, double position)
{
  PtPage *page;

  page = PT_PAGE (adw_carousel_get_nth_page (carousel, position));
  return pt_page_get_can_proceed (page);
}

static gboolean
get_btn_previous_sensitive (GObject *object, AdwCarousel *carousel, double position)
{
  /* HACK: this method gets called whenever the user starts swiping anywhere
   * This is DEFINITELY NOT THE RIGHT PLACE TO DO THIS, but we want to ensure
   * that we unfocus any text entry or any other crap like that. So I'm just gonna
   * do it here */
  GtkRoot *root;
  GtkWidget *prev_page;
  PtWindow *self;
  int cur_index;

  root = gtk_widget_get_root (GTK_WIDGET (carousel));
  self = PT_WINDOW (root);

  if (!G_APPROX_VALUE (self->last_position, position, 0.0001)) {
    gtk_window_set_focus (GTK_WINDOW (self), NULL);
    self->last_position = position;
  }

  if (!self->updates_skipped || !self->update_page || !self->main_carousel)
    return TRUE;

  cur_index = (int) position;
  if (cur_index <= 0)
    return TRUE;

  prev_page = adw_carousel_get_nth_page (self->main_carousel, cur_index - 1);

  if (prev_page == self->update_page)
    return FALSE;

  return TRUE;
}

static gdouble
get_success_backdrop_opacity (GObject *object,
                              GtkBox *box,
                              double position)
{
  GtkRoot *root;
  PtWindow *self;
  guint page_count;
  gdouble opacity = 0.0;
  AdwCarousel *carousel;

  root = gtk_widget_get_root (GTK_WIDGET (box));
  self = PT_WINDOW (root);
  carousel = self->main_carousel;

  if (!carousel)
    return 0.0;

  page_count = adw_carousel_get_n_pages (ADW_CAROUSEL (carousel));

  position += 1.0;
  if (position >= page_count - 1)
    opacity = (position - (page_count - 1));

  return opacity;
}

static int
pt_window_get_page_index (PtWindow *self,
                          GtkWidget *page)
{
  int i, n_pages;

  if (!self->main_carousel || !page)
    return -1;

  n_pages = adw_carousel_get_n_pages (self->main_carousel);

  for (i = 0; i < n_pages; i++) {
    if (adw_carousel_get_nth_page (self->main_carousel, i) == page)
      return i;
  }

  return -1;
}

static void
pt_set_dark_mode (GtkToggleButton *btn, gpointer user_data)
{
  GtkRoot *root;
  PtWindow *self;

  root = gtk_widget_get_root (GTK_WIDGET (btn));
  self = PT_WINDOW (root);

  g_settings_set_enum (self->interface_settings, INTERFACE_COLOR_SCHEME_KEY,
                       G_DESKTOP_COLOR_SCHEME_PREFER_DARK);
}

static void
pt_set_default_mode (GtkToggleButton *btn, gpointer user_data)
{
  GtkRoot *root;
  PtWindow *self;

  root = gtk_widget_get_root (GTK_WIDGET (btn));
  self = PT_WINDOW (root);

  g_settings_set_enum (self->interface_settings, INTERFACE_COLOR_SCHEME_KEY,
                       G_DESKTOP_COLOR_SCHEME_DEFAULT);
}

static void
pt_check_should_exit (PtWindow *self)
{
  const gchar *home_dir;
  g_autofree gchar *file_path = NULL;

  if (self->pending_commits == 0) {
    g_debug ("Everything went well. See you never again!\n");

    home_dir = g_get_home_dir ();
    file_path = g_build_filename (home_dir, ".config/furios-initial-setup-pending", NULL);

    g_debug ("Removing %s\n", file_path);

    unlink (file_path);

    gtk_window_close (GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (self))));
  }
}

static void
pt_on_security_settings_applied (PtSecuritySettings *security_settings, gboolean ok, gpointer user_data)
{
  PtPage *page;
  PtWindow *self;

  page = PT_PAGE (user_data);
  self = PT_WINDOW (gtk_widget_get_root (GTK_WIDGET (page)));

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

static void
pt_commit_language_settings (PtPage *page, gpointer user_data)
{
  GtkRoot *root;
  PtWindow *self;
  GtkWidget *language_chooser;

  root = gtk_widget_get_root (GTK_WIDGET (page));
  self = PT_WINDOW (root);
  language_chooser = pt_page_get_widget (page);

  cc_language_chooser_apply (CC_LANGUAGE_CHOOSER (language_chooser));

  self->pending_commits--;
  pt_check_should_exit (self);
}

static void
pt_commit_all (PtPage *final_page)
{
  GtkRoot *root;
  PtWindow *self;
  int i;
  int n_pages;

  root = gtk_widget_get_root (GTK_WIDGET (final_page));
  self = PT_WINDOW (root);
  n_pages = adw_carousel_get_n_pages (ADW_CAROUSEL (self->main_carousel));

  self->pending_commits = 0;

  /* First we check how many commits we need to do */
  for (i = 0; i < n_pages; i++) {
    PtPage *page;

    page = PT_PAGE (adw_carousel_get_nth_page (self->main_carousel, i));
    if (g_signal_handler_find (page,
                               G_SIGNAL_MATCH_ID,
                               g_signal_lookup ("apply-changes", G_OBJECT_TYPE (page)),
                               0, NULL, NULL, NULL))
      self->pending_commits++;
  }

  /* And now we truly commit */
  for (i = 0; i < n_pages; i++) {
    PtPage *page;

    page = PT_PAGE (adw_carousel_get_nth_page (self->main_carousel, i));
    if (g_signal_handler_find (page,
                               G_SIGNAL_MATCH_ID,
                               g_signal_lookup ("apply-changes", G_OBJECT_TYPE (page)),
                               0, NULL, NULL, NULL))
      g_signal_emit_by_name (page, "apply-changes");
  }
}

static void
on_update_progress_ready (GObject    *object,
                          GParamSpec *pspec,
                          gpointer    user_data)
{
  PtWindow *self;
  gboolean ready = FALSE;

  self = PT_WINDOW (user_data);

  g_object_get (object, "ready", &ready, NULL);

  if (ready && self->skip_updates_banner)
    adw_banner_set_revealed (self->skip_updates_banner, FALSE);
}

static void
on_carousel_position_changed (GObject    *object,
                              GParamSpec *pspec,
                              gpointer    user_data)
{
  PtWindow *self;
  AdwCarousel *carousel;
  int pos;
  GtkWidget *page;
  gboolean show = FALSE;
  gboolean ready = FALSE;

  self = PT_WINDOW (user_data);
  carousel = ADW_CAROUSEL (object);

  if (!self->skip_updates_banner)
    return;

  if (!self->update_progress || !self->update_page) {
    adw_banner_set_revealed (self->skip_updates_banner, FALSE);
  } else {
    g_object_get (self->update_progress, "ready", &ready, NULL);

    if (ready || self->updates_skipped) {
      adw_banner_set_revealed (self->skip_updates_banner, FALSE);
    } else {
      pos = adw_carousel_get_position (carousel);
      page = adw_carousel_get_nth_page (carousel, pos);

      if (page == self->update_page)
        show = TRUE;

      adw_banner_set_revealed (self->skip_updates_banner, show);
    }
  }

  if (self->updates_skipped && self->password_page && self->main_carousel) {
    int current_index = adw_carousel_get_position (self->main_carousel);
    int password_index = pt_window_get_page_index (self, self->password_page);

    if (password_index >= 0 && current_index < password_index) {
      GtkWidget *pwd_page = self->password_page;
      adw_carousel_scroll_to (self->main_carousel, pwd_page, FALSE);
    }
  }
}

static void
pt_update_begin (PtUpdateProgress *update_progress)
{
  GtkRoot *root;
  PtWindow *self;
  GtkWidget *w;
  GtkWidget *page = NULL;

  root = gtk_widget_get_root (GTK_WIDGET (update_progress));
  self = PT_WINDOW (root);

  /* Remember which PtUpdateProgress is active */
  self->update_progress = update_progress;

  w = GTK_WIDGET (update_progress);
  while (w) {
    GtkWidget *parent = gtk_widget_get_parent (w);

    if (!parent)
      break;

    if (ADW_IS_CAROUSEL (parent)) {
      page = w;
      break;
    }

    w = parent;
  }

  self->update_page = page;

  if (self->skip_updates_banner)
    adw_banner_set_revealed (self->skip_updates_banner, TRUE);

  /* Hide banner again once updates are done (ready == TRUE) */
  g_signal_connect (update_progress,
                    "notify::ready",
                    G_CALLBACK (on_update_progress_ready),
                    self);

  pt_update_progress_begin (update_progress);
}

static gboolean
pt_set_scaling (GtkScale *scale)
{
  int value;
  g_autoptr (GSettings) display_settings = NULL;
  g_autoptr (GVariantDict) display_config = NULL;
  g_autofree char *command = NULL;

  value = gtk_range_get_value (GTK_RANGE (scale));
  display_settings = g_settings_new ("sm.puri.phosh.monitors");
  display_config = g_variant_dict_new (NULL);

  /* Don't change the size from under the user */
  if (gtk_widget_get_state_flags (GTK_WIDGET (scale)) & GTK_STATE_FLAG_ACTIVE) {
    g_timeout_add (100, (GSourceFunc) pt_set_scaling, scale);
    return G_SOURCE_REMOVE;
  }

  if (value < 0 || value >= SCREEN_SCALES_COUNT) {
    g_warning ("Invalid scaling value %d", value);
    return G_SOURCE_REMOVE;
  }

  /* This is not super nice */
  command = g_strdup_printf ("wlr-randr --output HWCOMPOSER-1 --scale %s", SCREEN_SCALES[value]);
  g_spawn_command_line_async (command, NULL);
  g_variant_dict_insert_value (display_config, "HWCOMPOSER-1",
                               g_variant_new_parsed ("{'x':<%i>, 'y':<%i>, 'scale':<%d>}",
                                                     0, 0, atof (SCREEN_SCALES[value])));

  g_settings_set_value (display_settings, "config", g_variant_dict_end (display_config));

  return G_SOURCE_REMOVE;
}

static void
pt_window_set_property (GObject *object,
                        guint property_id,
                        const GValue *value,
                        GParamSpec *pspec)
{
  PtWindow *self = PT_WINDOW (object);

  switch (property_id) {
  case PROP_ANDROID_AUTOSTART:
    self->android_autostart = g_value_get_boolean (value);
    if (self->android_autostart) {
      g_autofree char *android_enable_path = g_build_filename (g_get_home_dir (), ".android_enable", NULL);
      g_file_set_contents (android_enable_path, "", 0, NULL);
    } else {
      g_autofree char *android_enable_path = g_build_filename (g_get_home_dir (), ".android_enable", NULL);
      unlink (android_enable_path);
    }
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
pt_window_get_property (GObject *object,
                        guint property_id,
                        GValue *value,
                        GParamSpec *pspec)
{
  PtWindow *self = PT_WINDOW (object);

  switch (property_id) {
  case PROP_ANDROID_AUTOSTART:
    g_value_set_boolean (value, self->android_autostart);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

/* Adapted from adw-inspector-page.c */
static const char *
get_color_tooltip (GDesktopAccentColor color)
{
  switch (color) {
  case G_DESKTOP_ACCENT_COLOR_BLUE:
    return _("Blue");
  case G_DESKTOP_ACCENT_COLOR_TEAL:
    return _("Teal");
  case G_DESKTOP_ACCENT_COLOR_GREEN:
    return _("Green");
  case G_DESKTOP_ACCENT_COLOR_YELLOW:
    return _("Yellow");
  case G_DESKTOP_ACCENT_COLOR_ORANGE:
    return _("Orange");
  case G_DESKTOP_ACCENT_COLOR_RED:
    return _("Red");
  case G_DESKTOP_ACCENT_COLOR_PINK:
    return _("Pink");
  case G_DESKTOP_ACCENT_COLOR_PURPLE:
    return _("Purple");
  case G_DESKTOP_ACCENT_COLOR_SLATE:
    return _("Slate");
  default:
    g_assert_not_reached ();
  }
}

static const char *
get_untranslated_color (GDesktopAccentColor color)
{
  switch (color) {
  case G_DESKTOP_ACCENT_COLOR_BLUE:
    return "blue";
  case G_DESKTOP_ACCENT_COLOR_TEAL:
    return "teal";
  case G_DESKTOP_ACCENT_COLOR_GREEN:
    return "green";
  case G_DESKTOP_ACCENT_COLOR_YELLOW:
    return "yellow";
  case G_DESKTOP_ACCENT_COLOR_ORANGE:
    return "orange";
  case G_DESKTOP_ACCENT_COLOR_RED:
    return "red";
  case G_DESKTOP_ACCENT_COLOR_PINK:
    return "pink";
  case G_DESKTOP_ACCENT_COLOR_PURPLE:
    return "purple";
  case G_DESKTOP_ACCENT_COLOR_SLATE:
    return "slate";
  default:
    g_assert_not_reached ();
  }
}

static void
on_accent_color_toggled_cb (PtWindow *self,
                            GtkToggleButton *toggle)
{
  GDesktopAccentColor accent_color_from_key;
  GDesktopAccentColor accent_color;

  accent_color = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (toggle), "accent-color"));

  accent_color_from_key = g_settings_get_enum (self->interface_settings, INTERFACE_ACCENT_COLOR_KEY);

  /* Don't unnecessarily set the key again */
  if (accent_color == accent_color_from_key)
    return;

  g_settings_set_enum (self->interface_settings,
                       INTERFACE_ACCENT_COLOR_KEY,
                       accent_color);
}

static void
setup_accent_color_toggles (PtWindow *self)
{
  GDesktopAccentColor accent_color;
  GDesktopAccentColor i;

  accent_color = g_settings_get_enum (self->interface_settings, INTERFACE_ACCENT_COLOR_KEY);

  for (i = G_DESKTOP_ACCENT_COLOR_BLUE; i <= G_DESKTOP_ACCENT_COLOR_SLATE; i++) {
    GtkWidget *button;
    GtkToggleButton *grouping_button;

    button = GTK_WIDGET (gtk_toggle_button_new ());
    grouping_button = GTK_TOGGLE_BUTTON (gtk_widget_get_first_child (self->accent_box));

    gtk_widget_set_tooltip_text (button, get_color_tooltip (i));
    gtk_widget_add_css_class (button, "accent-button");
    gtk_widget_add_css_class (button, get_untranslated_color (i));
    g_object_set_data (G_OBJECT (button), "accent-color", GINT_TO_POINTER (i));
    g_signal_connect_object (button, "toggled",
                             G_CALLBACK (on_accent_color_toggled_cb),
                             self,
                             G_CONNECT_SWAPPED);

    if (grouping_button != NULL)
      gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (button), grouping_button);

    if (i == accent_color)
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);

    gtk_box_append (GTK_BOX (self->accent_box), button);
  }
}

static void
on_skip_updates_clicked (AdwBanner *banner,
                         gpointer   user_data)
{
  GtkRoot *root;
  PtWindow *self;
  int pos;
  int next_pos;

  root = gtk_widget_get_root (GTK_WIDGET (banner));
  self = PT_WINDOW (root);

  if (self->update_progress)
    pt_update_progress_skip (self->update_progress);

  adw_banner_set_revealed (banner, FALSE);

  if (self->main_carousel) {
    pos = adw_carousel_get_position (self->main_carousel);
    next_pos = pos + 1;

    self->updates_skipped = TRUE;

    if (next_pos < adw_carousel_get_n_pages (self->main_carousel))
      self->password_page = adw_carousel_get_nth_page (self->main_carousel, next_pos);
    else
      self->password_page = NULL;

    goto_page (self, next_pos);
  }
}

static void
pt_window_class_init (PtWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_ensure (CC_TYPE_LANGUAGE_CHOOSER);
  g_type_ensure (CC_TYPE_NETWORK_LIST);
  g_type_ensure (PT_TYPE_SECURITY_SETTINGS);
  g_type_ensure (PT_TYPE_UPDATE_PROGRESS);
  g_type_ensure (PT_TYPE_PAGE);

  props[PROP_ANDROID_AUTOSTART] =
    g_param_spec_boolean ("android-autostart",
                          "Android autostart",
                          "Whether to autostart Android",
                          FALSE,
                          G_PARAM_READWRITE);

  object_class->set_property = pt_window_set_property;
  object_class->get_property = pt_window_get_property;
  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/io/furios/InitialSetup/ui/pt-window.ui");
  gtk_widget_class_bind_template_child (widget_class, PtWindow, main_carousel);
  gtk_widget_class_bind_template_child (widget_class, PtWindow, accent_box);
  gtk_widget_class_bind_template_child (widget_class, PtWindow, skip_updates_banner);

  gtk_widget_class_bind_template_callback (widget_class, get_btn_next_visible);
  gtk_widget_class_bind_template_callback (widget_class, get_btn_previous_visible);
  gtk_widget_class_bind_template_callback (widget_class, get_btn_next_sensitive);
  gtk_widget_class_bind_template_callback (widget_class, get_btn_previous_sensitive);
  gtk_widget_class_bind_template_callback (widget_class, get_success_backdrop_opacity);
  gtk_widget_class_bind_template_callback (widget_class, pt_set_dark_mode);
  gtk_widget_class_bind_template_callback (widget_class, pt_set_default_mode);
  gtk_widget_class_bind_template_callback (widget_class, pt_set_scaling);
  gtk_widget_class_bind_template_callback (widget_class, pt_commit_language_settings);
  gtk_widget_class_bind_template_callback (widget_class, pt_commit_security_settings);
  gtk_widget_class_bind_template_callback (widget_class, pt_commit_all);
  gtk_widget_class_bind_template_callback (widget_class, pt_update_begin);
  gtk_widget_class_bind_template_callback (widget_class, on_skip_updates_clicked);

  gtk_widget_class_install_action (widget_class, "win.flip-page", "i", on_flip_page_activated);
}

static void
pt_window_init (PtWindow *self)
{
  g_autoptr (GtkCssProvider) css_provider = NULL;
  g_autofree char *android_enable_path = NULL;

  css_provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (css_provider, "/io/furios/InitialSetup/style.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (css_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  self->theme_transition_provider = gtk_css_provider_new ();

  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (self->theme_transition_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  gtk_widget_init_template (GTK_WIDGET (self));

  self->interface_settings = g_settings_new (INTERFACE_PATH_ID);

  android_enable_path = g_build_filename (g_get_home_dir (), ".android_enable", NULL);
  self->android_autostart = g_file_test (android_enable_path, G_FILE_TEST_EXISTS);

  self->update_progress = NULL;
  self->update_page = NULL;
  self->password_page = NULL;
  self->updates_skipped = FALSE;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ANDROID_AUTOSTART]);
  setup_accent_color_toggles (self);

  if (self->main_carousel)
    g_signal_connect (self->main_carousel,
                      "notify::position",
                      G_CALLBACK (on_carousel_position_changed),
                      self);
}
