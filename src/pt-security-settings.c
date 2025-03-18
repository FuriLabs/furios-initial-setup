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

#define FPD_DBUS_NAME         "org.droidian.fingerprint"
#define FPD_DBUS_PATH         "/org/droidian/fingerprint"
#define FPD_DBUS_INTERFACE    "org.droidian.fingerprint"

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
  GDBusProxy *fpd_proxy;
  GtkWidget *fingerprint_group;
  AdwToastOverlay *toast_overlay;
  GtkListBox *finger_list;
  GtkWidget *enroll_step;
  GtkProgressBar *enroll_progress;
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

static gchar **
get_enrolled_fingers (GDBusProxy *fpd_proxy)
{
  GError *error = NULL;
  GVariant *result;
  gchar **fpd_fingers = NULL;

  result = g_dbus_proxy_call_sync(
    fpd_proxy,
    "GetAll",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling GetAll: %s\n", error->message);
    g_clear_error (&error);
    return NULL;
  } else {
    GVariant *fpd_list;
    fpd_list = g_variant_get_child_value (result, 0);
    fpd_fingers = g_variant_dup_strv (fpd_list, NULL);
    g_variant_unref (fpd_list);
    g_variant_unref (result);
  }

  return fpd_fingers;
}

static GtkWidget *
create_finger_row (const gchar *finger_name)
{
  GtkWidget *box, *icon, *label;
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 8);
  gtk_widget_set_margin_start (box, 16);
  gtk_widget_set_margin_end (box, 16);

  icon = gtk_image_new_from_icon_name ("auth-fingerprint-symbolic");
  gtk_image_set_icon_size (GTK_IMAGE (icon), GTK_ICON_SIZE_LARGE);
  gtk_box_append (GTK_BOX (box), icon);

  label = gtk_label_new (finger_name);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_box_append (GTK_BOX (box), label);

  return box;
}

static void
refresh_fingerprint_list (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_instance_private (self);

  gchar *all_fingers[] = {
    "right-index-finger",
    "left-index-finger",
    "right-thumb",
    "right-middle-finger",
    "right-ring-finger",
    "right-little-finger",
    "left-thumb",
    "left-middle-finger",
    "left-ring-finger",
    "left-little-finger",
    NULL
  };

  gchar **enrolled_fingers = get_enrolled_fingers (priv->fpd_proxy);

  gtk_list_box_remove_all (priv->finger_list);

  for (int i = 0; all_fingers[i] != NULL; i++) {
    gboolean is_enrolled = g_strv_contains ((const gchar *const *) enrolled_fingers, all_fingers[i]);
    if (!is_enrolled) {
      GtkWidget *row = create_finger_row (all_fingers[i]);
      gtk_list_box_append (priv->finger_list, row);
    }
  }

  g_strfreev (enrolled_fingers);
}

static void
show_toast (AdwToastOverlay *toast_overlay, const char *format, ...)
{
  va_list args;
  char *message;
  AdwToast *toast;

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);

  toast = adw_toast_new (message);
  adw_toast_set_timeout (toast, 3);

  adw_toast_overlay_add_toast (toast_overlay, toast);

  g_free (message);
}

static void
handle_signal (GDBusProxy *proxy, gchar *sender_name, gchar *signal_name, GVariant *parameters, gpointer user_data)
{
  gint progress;
  gchar *info;
  PtSecuritySettings *self = (PtSecuritySettings *) user_data;
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_instance_private (self);
  AdwToastOverlay *toast_overlay = priv->toast_overlay;

  if (g_strcmp0 (signal_name, "EnrollProgressChanged") == 0) {
    g_variant_get (parameters, "(i)", &progress);

    gtk_progress_bar_set_fraction (priv->enroll_progress, progress / 100.0);

    if (progress == 100) {
      gtk_widget_activate_action (GTK_WIDGET (self), "win.flip-page", "i", 1);
    }
  } else if (g_strcmp0 (signal_name, "ErrorInfo") == 0) {
    g_variant_get (parameters, "(s)", &info);
    g_debug ("%s received: %s", signal_name, info);

    if (g_strcmp0 (info, "ERROR_NO_SPACE") == 0)
      show_toast (toast_overlay, "No space available for new fingerprints");
    else if (g_strcmp0 (info, "ERROR_HW_UNAVAILABLE") == 0)
      show_toast (toast_overlay, "Fingerprint hardware is unavailable");
    else if (g_strcmp0 (info, "ERROR_UNABLE_TO_PROCESS") == 0)
      show_toast (toast_overlay, "Unable to process fingerprint");
    else if (g_strcmp0 (info, "ERROR_TIMEOUT") == 0 || g_strcmp0 (info, "ERROR_CANCELED") == 0)
      show_toast (toast_overlay, "Fingerprint operation timed out");

    // Go back to the first screen so the user can select the finger again
    gtk_widget_set_visible (GTK_WIDGET (priv->finger_list), TRUE);
    gtk_widget_set_visible (priv->enroll_step, FALSE);
  } else if (g_strcmp0 (signal_name, "AcquisitionInfo") == 0) {
    g_variant_get (parameters, "(s)", &info);
    g_debug ("%s received: %s", signal_name, info);
    if (g_strcmp0 (info, "FPACQUIRED_PARTIAL") == 0)
      show_toast (toast_overlay, "Partial fingerprint detected. Please try again");
    else if (g_strcmp0 (info, "FPACQUIRED_IMAGER_DIRTY") == 0)
      show_toast (toast_overlay, "The sensor is dirty. Please clean and try again");
    else if (g_strcmp0 (info, "FPACQUIRED_TOO_FAST") == 0)
      show_toast (toast_overlay, "Finger moved too fast. Please try again");
    else if (g_strcmp0 (info, "FPACQUIRED_TOO_SLOW") == 0)
      show_toast (toast_overlay, "Finger moved too slow. Please try again");
    else if (g_strcmp0 (info, "FPACQUIRED_INSUFFICIENT") == 0)
      show_toast (toast_overlay, "Couldn't process fingerprint. Please try again");

    g_free (info);
  }
}

static GtkWidget *
find_child_by_name (GtkWidget *widget, const gchar *name)
{
  GtkWidget *next_child = gtk_widget_get_first_child (widget);

  const gchar *child_name = gtk_widget_get_name (widget);

  if (!GTK_IS_WIDGET (widget)) {
    return NULL;
  }

  if (g_strcmp0 (name, child_name) == 0)
      return widget;

  while (next_child) {
    GtkWidget *found = find_child_by_name (next_child, name);
    if (found) return found;
    next_child = gtk_widget_get_next_sibling (next_child);
  }

  return NULL;
}

static void
on_finger_activated (GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
  GVariant *result;
  GError *error = NULL;
  PtSecuritySettings *self = (PtSecuritySettings *) user_data;
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_instance_private (self);

  GtkWidget *child = gtk_list_box_row_get_child (row);
  GtkWidget *label = gtk_widget_get_first_child (child);

  while (label && !GTK_IS_LABEL (label)) {
    label = gtk_widget_get_next_sibling (label);
  }

  if (GTK_IS_LABEL (label)) {
    const gchar *finger_name = gtk_label_get_text (GTK_LABEL (label));

    gtk_progress_bar_set_fraction (priv->enroll_progress, 0);
    gtk_widget_set_visible (GTK_WIDGET (priv->finger_list), FALSE);
    gtk_widget_set_visible (priv->enroll_step, TRUE);

    result = g_dbus_proxy_call_sync(
      priv->fpd_proxy,
      "Enroll",
      g_variant_new ("(s)", finger_name),
      G_DBUS_CALL_FLAGS_NONE,
      -1,
      NULL,
      &error
    );

    if (error) {
      g_warning ("Error calling Enroll: %s\n", error->message);
      g_clear_error (&error);
    }

    g_variant_unref (result);
  }
}

static void
register_fingerprint (PtSecuritySettings *self)
{
  GError *error = NULL;
  // Still absolutely GHASTLY, GNARLY, AWFUL
  PtPage *parent_page = PT_PAGE (gtk_widget_get_parent (gtk_widget_get_parent (gtk_widget_get_parent (gtk_widget_get_parent (gtk_widget_get_parent (GTK_WIDGET (self)))))));
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_instance_private (self);

  priv->toast_overlay = ADW_TOAST_OVERLAY (find_child_by_name (GTK_WIDGET (parent_page), "toast_overlay"));
  priv->finger_list = GTK_LIST_BOX (find_child_by_name (GTK_WIDGET (parent_page), "finger_list"));
  priv->enroll_step = find_child_by_name (GTK_WIDGET (parent_page), "enroll_step");
  priv->enroll_progress = GTK_PROGRESS_BAR (find_child_by_name (GTK_WIDGET (parent_page), "enroll_progress"));

  g_signal_connect (priv->finger_list, "row-activated", G_CALLBACK (on_finger_activated), self);

  priv->fpd_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    FPD_DBUS_NAME,
    FPD_DBUS_PATH,
    FPD_DBUS_INTERFACE,
    NULL,
    &error
  );

  g_signal_connect (priv->fpd_proxy, "g-signal", G_CALLBACK (handle_signal), self);

  refresh_fingerprint_list (self);

  pt_page_switch_to_subpage (parent_page);
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
                                              "/io/furios/InitialSetup/ui/pt-security-settings.ui");

  gtk_widget_class_bind_template_child_private (widget_class, PtSecuritySettings, password_entry);
  gtk_widget_class_bind_template_child_private (widget_class, PtSecuritySettings, verify_entry);
  gtk_widget_class_bind_template_child_private (widget_class, PtSecuritySettings, fingerprint_group);

  gtk_widget_class_bind_template_callback (widget_class, update_password_match);
  gtk_widget_class_bind_template_callback (widget_class, register_fingerprint);
}

static gboolean
ping_fpd (void)
{
  GDBusProxy *proxy;
  GError *error = NULL;
  GVariant *result;

  proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    FPD_DBUS_NAME,
    FPD_DBUS_PATH,
    "org.freedesktop.DBus.Peer",
    NULL,
    &error
  );

  if (error) {
    g_warning ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  result = g_dbus_proxy_call_sync(
    proxy,
    "Ping",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  g_object_unref (proxy);

  if (error) {
    g_warning ("Error calling Ping: %s\n", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  g_variant_unref (result);
  return TRUE;
}

static void
pt_security_settings_init (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_instance_private (self);
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_widget_set_visible (priv->fingerprint_group, ping_fpd());
}

PtSecuritySettings *
pt_security_settings_new (void)
{
  return PT_SECURITY_SETTINGS (g_object_new (PT_TYPE_SECURITY_SETTINGS, NULL));
}
