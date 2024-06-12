/*
 * Copyright (C) 2024 Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pt-hw-page"

#include "furios-initial-setup-config.h"

#include "pt-hw-page.h"

#define GMOBILE_USE_UNSTABLE_API
#include <gmobile/gmobile.h>

/**
 * PtHwPage:
 *
 * A tour page for a specific hardware. A page is considered useful for
 * a certain hardware if the device tree compatibles on the page match
 * any of the devices device tree compatibles.
 */

enum {
  PROP_0,
  PROP_COMPATIBLES,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PtHwPage {
  PtPage       parent;

  GStrv        compatibles;
};

static void pt_hw_page_buildable_init (GtkBuildableIface *iface);

G_DEFINE_TYPE_WITH_CODE (PtHwPage, pt_hw_page, PT_TYPE_PAGE,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE,
                                                pt_hw_page_buildable_init))

static void
set_compatibles (PtHwPage *self, const char *const *compatibles)
{
  GStrvBuilder *builder = g_strv_builder_new ();

  /* Strip whitespace and empty elements */
  for (int i = 0; compatibles[i]; i++) {
    g_autofree char *compatible = NULL;

    compatible = g_strstrip (g_strdup (compatibles[i]));
    if (compatible[0] == '\0')
      continue;

    g_strv_builder_add (builder, compatible);
  }

  g_clear_pointer (&self->compatibles, g_strfreev);
  self->compatibles = g_strv_builder_end (builder);
}


static void
pt_hw_page_set_property (GObject      *object,
                         guint         property_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  PtHwPage *self = PT_HW_PAGE (object);

  switch (property_id) {
  case PROP_COMPATIBLES:
    self->compatibles = g_value_dup_boxed (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pt_hw_page_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
  PtHwPage *self = PT_HW_PAGE (object);

  switch (property_id) {
  case PROP_COMPATIBLES:
    g_value_set_boxed (value, self->compatibles);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
pt_hw_page_finalize (GObject *object)
{
  PtHwPage *self = PT_HW_PAGE (object);

  g_clear_pointer (&self->compatibles, g_strfreev);

  G_OBJECT_CLASS (pt_hw_page_parent_class)->finalize (object);
}


static GtkBuildableIface *parent_buildable_iface;

static void
pt_hw_page_buildable_set_buildable_property (GtkBuildable *buildable,
                                             GtkBuilder   *builder,
                                             const gchar  *name,
                                             const GValue *value)
{
  PtHwPage *self = PT_HW_PAGE (buildable);

  if (strcmp (name, "compatibles") == 0) {
    set_compatibles (self, g_value_get_boxed (value));
    return;
  }

  if (parent_buildable_iface->set_buildable_property)
    parent_buildable_iface->set_buildable_property (buildable, builder, name, value);
  else
    g_object_set_property (G_OBJECT (self), name, value);
}


static void
pt_hw_page_buildable_init (GtkBuildableIface *iface)
{
  parent_buildable_iface = g_type_interface_peek_parent (iface);
  iface->set_buildable_property = pt_hw_page_buildable_set_buildable_property;
}


static void
pt_hw_page_class_init (PtHwPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = pt_hw_page_get_property;
  object_class->set_property = pt_hw_page_set_property;
  object_class->finalize = pt_hw_page_finalize;

  /**
   * PtHwPage:compatibles:
   *
   * The device tree compatibles this page applies to.
   */
  props[PROP_COMPATIBLES] =
    g_param_spec_boxed ("compatibles", "", "",
                        G_TYPE_STRV,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
pt_hw_page_init (PtHwPage *self)
{
}


PtHwPage *
pt_hw_page_new (void)
{
  return g_object_new (PT_TYPE_HW_PAGE, NULL);
}
