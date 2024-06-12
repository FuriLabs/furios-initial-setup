/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "pt-security"

#include "furios-initial-setup-config.h"
#include "pt-security-settings.h"
#include "pt-page.h"
#include "run-passwd.h"

#include <adwaita.h>
#include <glib/gi18n.h>

enum
{
  PROP_0,
  PROP_READY,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

typedef struct _PtSecuritySettingsPrivate
{
  AdwPasswordEntryRow *password_entry;
  AdwPasswordEntryRow *verify_entry;
  gboolean ready;
  ApplyCallback apply_cb;
  gpointer apply_user_data;
} PtSecuritySettingsPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PtSecuritySettings, pt_security_settings, ADW_TYPE_BIN)

#define DEFAULT_PASSWORD "1234"

static void
password_changed_cb (PasswdHandler      *handler,
                     GError             *error,
                     void               *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_instance_private (PT_SECURITY_SETTINGS (self));
  if (error)
  {
    priv->apply_cb (self, FALSE, priv->apply_user_data);
    g_warning ("Error changing password: %s", error->message);
    return;
  }
  g_message ("Password changed successfully");
  priv->apply_cb (self, TRUE, priv->apply_user_data);
}

static void
auth_cb (PasswdHandler      *handler,
         GError             *error,
         void               *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_instance_private (PT_SECURITY_SETTINGS (self));
  if (error)
  {
    priv->apply_cb (self, FALSE, priv->apply_user_data);
    g_warning ("Error authenticating: %s", error->message);
    return;
  }
  g_message ("Authenticated successfully");
  passwd_change_password (handler, gtk_editable_get_text (GTK_EDITABLE (priv->password_entry)), password_changed_cb, self);
}

static void
pt_security_settings_finalize (GObject *object)
{
  G_OBJECT_CLASS (pt_security_settings_parent_class)->finalize (object);
}

void
pt_security_settings_apply (GObject *self, ApplyCallback cb, gpointer user_data)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_instance_private (PT_SECURITY_SETTINGS (self));
  PasswdHandler *passwd_handler;

  if (priv->ready)
  {
    priv->apply_cb = cb;
    priv->apply_user_data = user_data;
    passwd_handler = passwd_init ();
    passwd_authenticate (passwd_handler, DEFAULT_PASSWORD, auth_cb, self);
  }
  else
  {
    g_warning ("Password not ready");
    cb (self, FALSE, user_data);
  }
}

static void
update_password_match (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_instance_private (self);
  const gchar *password;
  const gchar *verify;
  bool can_proceed = FALSE;

  password = gtk_editable_get_text (GTK_EDITABLE (priv->password_entry));
  verify = gtk_editable_get_text (GTK_EDITABLE (priv->verify_entry));

  if (strlen (verify) > 0)
  {
    if (strlen (password) < 6)
    {
      gtk_widget_add_css_class (GTK_WIDGET (priv->password_entry), "error");
      gtk_widget_add_css_class (GTK_WIDGET (priv->verify_entry), "error");
    }
    else
    {
      gtk_widget_remove_css_class (GTK_WIDGET (priv->password_entry), "error");

      if (strcmp (password, verify) != 0)
      {
        gtk_widget_add_css_class (GTK_WIDGET (priv->verify_entry), "error");
      }
      else
      {
        gtk_widget_remove_css_class (GTK_WIDGET (priv->verify_entry), "error");
        can_proceed = TRUE;
      }
    }
  }
  else
  {
    gtk_widget_remove_css_class (GTK_WIDGET (priv->verify_entry), "error");
  }

  g_object_set (self, "ready", can_proceed, NULL);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_READY]);
}

static void
pt_security_settings_set_property (GObject *object,
                                  guint property_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
  PtSecuritySettings *self = PT_SECURITY_SETTINGS (object);
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_instance_private (self);

  switch (property_id)
  {
  case PROP_READY:
    priv->ready = g_value_get_boolean (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
pt_security_settings_get_property (GObject *object,
                                  guint property_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
  PtSecuritySettings *self = PT_SECURITY_SETTINGS (object);
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_instance_private (self);

  switch (property_id)
  {
  case PROP_READY:
    g_value_set_boolean (value, priv->ready);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
pt_security_settings_class_init (PtSecuritySettingsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = pt_security_settings_set_property;
  object_class->get_property = pt_security_settings_get_property;

  props[PROP_READY] =
    g_param_spec_boolean ("ready",
                         "Ready",
                         "Whether the user has entered a valid password",
                         FALSE,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  object_class->finalize = pt_security_settings_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                              "/mobi/phosh/PhoshTour/ui/pt-security-settings.ui");

  gtk_widget_class_bind_template_child_private (widget_class, PtSecuritySettings, password_entry);
  gtk_widget_class_bind_template_child_private (widget_class, PtSecuritySettings, verify_entry);

  gtk_widget_class_bind_template_callback (widget_class, update_password_match);
}

static void
pt_security_settings_init (PtSecuritySettings *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

PtSecuritySettings *
pt_security_settings_new (void)
{
  return PT_SECURITY_SETTINGS (g_object_new (PT_TYPE_SECURITY_SETTINGS, NULL));
}
