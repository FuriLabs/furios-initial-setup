/*
 * Copyright (C) 2022 Purism SPC
 *               2023-2024 Guido Günther
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pt-window"

#include "phosh-tour-config.h"
#include "pt-application.h"
#include "pt-hw-page.h"
#include "pt-window.h"
#include "pt-page.h"

#define GMOBILE_USE_UNSTABLE_API
#include <gmobile.h>
#include <glib/gi18n.h>

#include "cc-language-chooser.h"
#include "cc-network-list.h"
#include "cc-online-account-provider-row.h"
#include "cc-online-account-row.h"
#include "pt-online-accounts.h"
#include "pt-security-settings.h"


struct _PtWindow {
  AdwApplicationWindow parent_instance;

  AdwCarousel         *main_carousel;
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


static void
pt_window_class_init (PtWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  g_type_ensure (CC_TYPE_LANGUAGE_CHOOSER);
  g_type_ensure (CC_TYPE_NETWORK_LIST);
  g_type_ensure (CC_TYPE_ONLINE_ACCOUNT_ROW);
  g_type_ensure (CC_TYPE_ONLINE_ACCOUNT_PROVIDER_ROW);
  g_type_ensure (PT_TYPE_SECURITY_SETTINGS);
  g_type_ensure (PT_TYPE_ONLINE_ACCOUNTS);
  g_type_ensure (PT_TYPE_PAGE);
  g_type_ensure (PT_TYPE_HW_PAGE);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/PhoshTour/ui/pt-window.ui");
  gtk_widget_class_bind_template_child (widget_class, PtWindow, main_carousel);

  gtk_widget_class_bind_template_callback (widget_class, get_btn_next_visible);
  gtk_widget_class_bind_template_callback (widget_class, get_btn_previous_visible);
  gtk_widget_class_bind_template_callback (widget_class, get_btn_next_sensitive);
  gtk_widget_class_bind_template_callback (widget_class, get_btn_previous_sensitive);

  gtk_widget_class_install_action (widget_class, "win.flip-page", "i", on_flip_page_activated);
}

static void
pt_window_init (PtWindow *self)
{
  g_auto (GStrv) compatibles = gm_device_tree_get_compatibles (NULL, NULL);
  int kept = 0, removed = 0;

  gtk_widget_init_template (GTK_WIDGET (self));

  while (kept < adw_carousel_get_n_pages (self->main_carousel)) {
    GtkWidget *page;
    gboolean compatible;

    page = adw_carousel_get_nth_page (self->main_carousel, kept);

    compatible = !PT_IS_HW_PAGE (page) ||
      pt_hw_page_is_compatible (PT_HW_PAGE (page), (const char * const *)compatibles);

    if (!compatible) {
      adw_carousel_remove (self->main_carousel, page);
      removed++;
      continue;
    }

    kept++;
  }

  g_debug ("Kept %d page(s), removed %d hw specific page(s)", kept, removed);
}
