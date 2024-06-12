/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pt-page"

#include "furios-initial-setup-config.h"
#include "pt-page.h"
#include "cc-network-list.h"

#include <adwaita.h>
#include <glib/gi18n.h>

enum {
  PROP_0,
  PROP_SUMMARY,
  PROP_EXPLANATION,
  PROP_IMAGE_URI,
  PROP_WIDGET,
  PROP_CAN_PROCEED,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

typedef struct _PtPagePrivate {
  char       *image_uri;
  GtkPicture *image;
  GtkLabel   *lbl_summary;
  GtkLabel   *lbl_explanation;
  AdwBin     *bin_widget;
  gboolean   can_proceed;
} PtPagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PtPage, pt_page, ADW_TYPE_BIN)



static void
pt_page_set_property (GObject      *object,
                      guint         property_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
  PtPage *self = PT_PAGE (object);
  PtPagePrivate *priv = pt_page_get_instance_private (self);

  switch (property_id) {
  case PROP_SUMMARY:
    pt_page_set_summary (self, g_value_get_string (value));
    break;
  case PROP_EXPLANATION:
    pt_page_set_explanation (self, g_value_get_string (value));
    break;
  case PROP_IMAGE_URI:
    pt_page_set_image_uri (self, g_value_get_string (value));
    g_object_notify (object, "image-uri");
    break;
  case PROP_WIDGET:
    pt_page_set_widget (self, g_value_get_object (value));
    break;
  case PROP_CAN_PROCEED:
    priv->can_proceed = g_value_get_boolean (value);
    g_object_notify_by_pspec (object, pspec);

    // Hack: force the buttons to update by making the carousel emit its position again
    g_object_notify (G_OBJECT (gtk_widget_get_parent (GTK_WIDGET (self))), "position");
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pt_page_get_property (GObject    *object,
                      guint       property_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
  PtPage *self = PT_PAGE (object);
  PtPagePrivate *priv = pt_page_get_instance_private (self);

  switch (property_id) {
  case PROP_SUMMARY:
    g_value_set_string (value, gtk_label_get_label (priv->lbl_summary));
    break;
  case PROP_EXPLANATION:
    g_value_set_string (value, gtk_label_get_label (priv->lbl_explanation));
    break;
  case PROP_IMAGE_URI:
    g_value_set_string (value, priv->image_uri);
    break;
  case PROP_WIDGET:
    g_value_set_object (value, adw_bin_get_child (priv->bin_widget));
    break;
  case PROP_CAN_PROCEED:
    g_value_set_boolean (value, priv->can_proceed);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pt_page_finalize (GObject *object)
{
  PtPage *self = PT_PAGE (object);
  PtPagePrivate *priv = pt_page_get_instance_private (self);

  g_clear_pointer (&priv->image_uri, g_free);

  G_OBJECT_CLASS (pt_page_parent_class)->finalize (object);
}


static void
pt_page_class_init (PtPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = pt_page_finalize;
  object_class->set_property = pt_page_set_property;
  object_class->get_property = pt_page_get_property;

  props[PROP_SUMMARY] =
    g_param_spec_string ("summary", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_EXPLANATION] =
    g_param_spec_string ("explanation", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_IMAGE_URI] =
    g_param_spec_string ("image-uri", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_WIDGET] =
    g_param_spec_object ("widget", "", "",
                         GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_CAN_PROCEED] =
    g_param_spec_boolean ("can-proceed", "", "",
                          FALSE,
                          G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/PhoshTour/ui/pt-page.ui");
  gtk_widget_class_bind_template_child_private (widget_class, PtPage, image);
  gtk_widget_class_bind_template_child_private (widget_class, PtPage, lbl_summary);
  gtk_widget_class_bind_template_child_private (widget_class, PtPage, lbl_explanation);
  gtk_widget_class_bind_template_child_private (widget_class, PtPage, bin_widget);
}


static void
pt_page_init (PtPage *self)
{
  PtPagePrivate *priv;
  gtk_widget_init_template (GTK_WIDGET (self));

  priv = pt_page_get_instance_private (self);
  priv->can_proceed = TRUE;
}


PtPage *
pt_page_new (void)
{
  return PT_PAGE (g_object_new (PT_TYPE_PAGE, NULL));
}


void
pt_page_set_summary (PtPage *self, const char *summary)
{
  PtPagePrivate *priv;
  g_autoptr (GString) label = g_string_new (summary);

  g_return_if_fail (PT_IS_PAGE (self));
  priv = pt_page_get_instance_private (self);

  gtk_label_set_label (priv->lbl_summary, label->str);
}


void
pt_page_set_explanation (PtPage *self, const char *explanation)
{
  PtPagePrivate *priv;
  g_autoptr (GString) label = g_string_new (explanation);

  g_return_if_fail (PT_IS_PAGE (self));
  priv = pt_page_get_instance_private (self);

  gtk_label_set_label (priv->lbl_explanation, label->str);
}


void
pt_page_set_image_uri (PtPage *self, const char *uri)
{
  PtPagePrivate *priv;
  g_autoptr (GdkTexture) texture = NULL;

  g_return_if_fail (PT_IS_PAGE (self));
  priv = pt_page_get_instance_private (self);

  g_free (priv->image_uri);
  priv->image_uri = g_strdup (uri);

  if (priv->image_uri)
    gtk_picture_set_resource (priv->image, &uri[strlen("resource://")]);
  else
    gtk_picture_set_paintable (priv->image, GDK_PAINTABLE (texture));
}


void
pt_page_set_widget (PtPage *self, GtkWidget *widget)
{
  PtPagePrivate *priv;

  g_return_if_fail (PT_IS_PAGE (self));
  g_return_if_fail (GTK_IS_WIDGET (widget));

  priv = pt_page_get_instance_private (self);

  adw_bin_set_child (priv->bin_widget, widget);

  gtk_widget_set_visible (GTK_WIDGET (priv->bin_widget), !!widget);
}

gboolean
pt_page_get_can_proceed (PtPage *self)
{
  PtPagePrivate *priv;

  g_return_val_if_fail (PT_IS_PAGE (self), FALSE);
  priv = pt_page_get_instance_private (self);

  return priv->can_proceed;
}

void
pt_page_set_can_proceed (PtPage *self, gboolean can_proceed)
{
  PtPagePrivate *priv;

  g_return_if_fail (PT_IS_PAGE (self));
  priv = pt_page_get_instance_private (self);

  priv->can_proceed = can_proceed;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CAN_PROCEED]);
}
