/*
 * Copyright (C) 2026 Furi Labs
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Bardia Moshiri <bardia@furilabs.com>
 */

#define G_LOG_DOMAIN "pt-security"

#include "furios-initial-setup-config.h"
#include "pt-security-settings.h"
#include "pt-security-settings-private.h"
#include "pt-page.h"

#include <adwaita.h>
#include <glib/gi18n.h>
#include <biomd/biomd_enums.h>

#define BIOMD_DBUS_NAME                   "io.FuriOS.Biomd"
#define BIOMD_DBUS_FINGERPRINT_PATH       "/io/FuriOS/Biomd/Fingerprint"
#define BIOMD_DBUS_FINGERPRINT_INTERFACE  "io.FuriOS.Biomd.Fingerprint"

static void
show_toast (AdwToastOverlay *toast_overlay,
            const char      *format,
            ...)
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

static gchar **
get_enrolled_fingers (PtSecuritySettings *self)
{
  GError *error = NULL;
  GVariant *result;
  gchar **enrolled_fingers = NULL;
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);

  if (!priv->props_proxy) {
    g_debug ("Properties proxy not available");
    return NULL;
  }

  result = g_dbus_proxy_call_sync (priv->props_proxy,
                                   "Get",
                                   g_variant_new ("(ss)", BIOMD_DBUS_FINGERPRINT_INTERFACE, "EnrolledFingers"),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   NULL,
                                   &error);

  if (error) {
    g_debug ("Error calling Get for EnrolledFingers: %s", error->message);
    g_clear_error (&error);
    return NULL;
  } else {
    GVariant *enrolled_variant;
    g_variant_get (result, "(v)", &enrolled_variant);
    enrolled_fingers = g_variant_dup_strv (enrolled_variant, NULL);
    g_variant_unref (enrolled_variant);
    g_variant_unref (result);
  }

  return enrolled_fingers;
}

static gchar **
get_valid_finger_names (PtSecuritySettings *self)
{
  GError *error = NULL;
  GVariant *result;
  gchar **valid_fingers = NULL;
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);

  if (!priv->props_proxy) {
    g_debug ("Properties proxy not available");
    return NULL;
  }

  result = g_dbus_proxy_call_sync (priv->props_proxy,
                                   "Get",
                                   g_variant_new ("(ss)", BIOMD_DBUS_FINGERPRINT_INTERFACE, "ValidFingerNames"),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   NULL,
                                   &error);

  if (error) {
    g_debug ("Error calling Get for ValidFingerNames: %s", error->message);
    g_clear_error (&error);
    return NULL;
  } else {
    GVariant *valid_variant;
    g_variant_get (result, "(v)", &valid_variant);
    valid_fingers = g_variant_dup_strv (valid_variant, NULL);
    g_variant_unref (valid_variant);
    g_variant_unref (result);
  }

  return valid_fingers;
}

static gboolean
fingerprint_enroll (PtSecuritySettings *self,
                    const gchar        *finger_name,
                    GError            **error)
{
  GVariant *result;
  gboolean success = FALSE;
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);

  if (!priv->fingerprint_proxy) {
    g_set_error (error,
                 G_IO_ERROR,
                 G_IO_ERROR_NOT_INITIALIZED,
                 "Fingerprint proxy not available");
    return FALSE;
  }

  result = g_dbus_proxy_call_sync (priv->fingerprint_proxy,
                                   "Enroll",
                                   g_variant_new ("(s)", finger_name),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   NULL,
                                   error);

  if (result) {
    g_variant_get (result, "(b)", &success);
    g_variant_unref (result);
  }

  return success;
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
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  gchar **valid_finger_names = get_valid_finger_names (self);
  gchar **enrolled_fingers = get_enrolled_fingers (self);

  if (!valid_finger_names) {
    g_debug ("Failed to get valid finger names");
    if (enrolled_fingers)
      g_strfreev (enrolled_fingers);
    return;
  }

  gtk_list_box_remove_all (priv->finger_list);

  for (int i = 0; valid_finger_names[i] != NULL; i++) {
    gboolean is_enrolled = enrolled_fingers &&
      g_strv_contains ((const gchar *const *) enrolled_fingers, valid_finger_names[i]);

    if (!is_enrolled) {
      GtkWidget *row = create_finger_row (valid_finger_names[i]);
      gtk_list_box_append (priv->finger_list, row);
    }
  }

  g_strfreev (valid_finger_names);

  if (enrolled_fingers)
    g_strfreev (enrolled_fingers);
}

static void
handle_signal (GDBusProxy *proxy,
               gchar      *sender_name,
               gchar      *signal_name,
               GVariant   *parameters,
               gpointer    user_data)
{
  gint progress;
  PtSecuritySettings *self = PT_SECURITY_SETTINGS (user_data);
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  AdwToastOverlay *toast_overlay = priv->toast_overlay;
  gint32 state_code, error_code, acquisition_code;
  const gchar *state_str;

  if (g_strcmp0 (signal_name, "EnrollmentProgressChanged") == 0) {
    g_variant_get (parameters, "(i)", &progress);

    gtk_progress_bar_set_fraction (priv->enroll_progress, progress / 100.0);

    if (progress == 100)
      gtk_widget_activate_action (GTK_WIDGET (self), "win.flip-page", "i", 1);
  } else if (g_strcmp0 (signal_name, "EnrolledFingersChanged") == 0) {
    g_debug ("EnrolledFingersChanged signal received");
    refresh_fingerprint_list (self);
  } else if (g_strcmp0 (signal_name, "StateChanged") == 0) {
    g_variant_get (parameters, "(i)", &state_code);

    switch ((BiometricState) state_code) {
    case STATE_IDLE:
      state_str = "IDLE";
      break;
    case STATE_ENROLLING:
      state_str = "ENROLLING";
      break;
    case STATE_IDENTIFYING:
      state_str = "IDENTIFYING";
      break;
    default:
      state_str = "UNKNOWN";
      break;
    }

    g_debug ("%s received: %s", signal_name, state_str);
  } else if (g_strcmp0 (signal_name, "ErrorInfoChanged") == 0) {
    g_variant_get (parameters, "(i)", &error_code);

    switch ((BiometricError) error_code) {
    case ERROR_NO_SPACE:
      show_toast (toast_overlay, "No space available for new fingerprints");
      break;
    case ERROR_HW_UNAVAILABLE:
      show_toast (toast_overlay, "Fingerprint hardware is unavailable");
      break;
    case ERROR_UNABLE_TO_PROCESS:
      show_toast (toast_overlay, "Unable to process fingerprint");
      break;
    case ERROR_TIMEOUT:
      show_toast (toast_overlay, "Fingerprint operation timed out");
      break;
    case ERROR_CANCELED:
      show_toast (toast_overlay, "Fingerprint operation timed out");
      break;
    case ERROR_REMOVE:
      show_toast (toast_overlay, "Unable to remove the fingerprint");
      break;
    case ERROR_LOCKOUT:
      show_toast (toast_overlay, "Too many attempts, fingerprint sensor locked");
      break;
    case ERROR_GENERAL:
      show_toast (toast_overlay, "An error occurred with the fingerprint reader");
      break;
    case ERROR_FINGER_NOT_RECOGNIZED:
      g_debug ("Finger is not recognized");
      break;
    case ERROR_NONE:
    default:
      break;
    }

    if (error_code != ERROR_FINGER_NOT_RECOGNIZED && error_code != ERROR_NONE) {
      gtk_widget_set_visible (GTK_WIDGET (priv->finger_list), TRUE);
      gtk_widget_set_visible (priv->enroll_step, FALSE);
    }
  } else if (g_strcmp0 (signal_name, "AcquisitionInfoChanged") == 0) {
    g_variant_get (parameters, "(i)", &acquisition_code);

    switch ((BiometricAcquisition) acquisition_code) {
    case ACQUISITION_PARTIAL:
      show_toast (toast_overlay, "Partial fingerprint detected. Please try again");
      break;
    case ACQUISITION_IMAGER_DIRTY:
      show_toast (toast_overlay, "The sensor is dirty. Please clean and try again");
      break;
    case ACQUISITION_TOO_FAST:
      show_toast (toast_overlay, "Finger moved too fast. Please try again");
      break;
    case ACQUISITION_TOO_SLOW:
      show_toast (toast_overlay, "Finger moved too slow. Please try again");
      break;
    case ACQUISITION_INSUFFICIENT:
      show_toast (toast_overlay, "Couldn't process fingerprint. Please try again");
      break;
    case ACQUISITION_NONE:
    case ACQUISITION_GOOD:
    default:
      break;
    }
  }
}

static GtkWidget *
find_child_by_name (GtkWidget   *widget,
                    const gchar *name)
{
  GtkWidget *next_child;
  const gchar *child_name;

  if (!GTK_IS_WIDGET (widget))
    return NULL;

  next_child = gtk_widget_get_first_child (widget);
  child_name = gtk_widget_get_name (widget);

  if (g_strcmp0 (name, child_name) == 0)
    return widget;

  while (next_child) {
    GtkWidget *found = find_child_by_name (next_child, name);

    if (found)
      return found;

    next_child = gtk_widget_get_next_sibling (next_child);
  }

  return NULL;
}

static void
on_finger_activated (GtkListBox    *box,
                     GtkListBoxRow *row,
                     gpointer       user_data)
{
  GError *error = NULL;
  gboolean success;
  PtSecuritySettings *self = PT_SECURITY_SETTINGS (user_data);
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  GtkWidget *child = gtk_list_box_row_get_child (row);
  GtkWidget *label = gtk_widget_get_first_child (child);

  while (label && !GTK_IS_LABEL (label))
    label = gtk_widget_get_next_sibling (label);

  if (GTK_IS_LABEL (label)) {
    const gchar *finger_name = gtk_label_get_text (GTK_LABEL (label));

    gtk_progress_bar_set_fraction (priv->enroll_progress, 0);
    gtk_widget_set_visible (GTK_WIDGET (priv->finger_list), FALSE);
    gtk_widget_set_visible (priv->enroll_step, TRUE);

    success = fingerprint_enroll (self, finger_name, &error);

    if (error) {
      g_warning ("Error calling Enroll: %s", error->message);
      g_clear_error (&error);

      gtk_widget_set_visible (GTK_WIDGET (priv->finger_list), TRUE);
      gtk_widget_set_visible (priv->enroll_step, FALSE);
    } else if (!success) {
      g_warning ("Failed to start enrollment");

      gtk_widget_set_visible (GTK_WIDGET (priv->finger_list), TRUE);
      gtk_widget_set_visible (priv->enroll_step, FALSE);
    }
  }
}

static gboolean
init_dbus_proxies (PtSecuritySettings *self)
{
  GError *error = NULL;
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);

  priv->fingerprint_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                           G_DBUS_PROXY_FLAGS_NONE,
                                                           NULL,
                                                           BIOMD_DBUS_NAME,
                                                           BIOMD_DBUS_FINGERPRINT_PATH,
                                                           BIOMD_DBUS_FINGERPRINT_INTERFACE,
                                                           NULL,
                                                           &error);

  if (error) {
    g_warning ("Error creating fingerprint proxy: %s", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  priv->props_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                     G_DBUS_PROXY_FLAGS_NONE,
                                                     NULL,
                                                     BIOMD_DBUS_NAME,
                                                     BIOMD_DBUS_FINGERPRINT_PATH,
                                                     "org.freedesktop.DBus.Properties",
                                                     NULL,
                                                     &error);

  if (error) {
    g_warning ("Error creating properties proxy: %s", error->message);
    g_clear_error (&error);
    g_clear_object (&priv->fingerprint_proxy);
    return FALSE;
  }

  g_signal_connect (priv->fingerprint_proxy, "g-signal", G_CALLBACK (handle_signal), self);

  return TRUE;
}

void
pt_security_settings_register_fingerprint (PtSecuritySettings *self)
{
  PtPage *parent_page;
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);

  /* Still absolutely GHASTLY, GNARLY, AWFUL */
  parent_page = PT_PAGE (gtk_widget_get_parent (gtk_widget_get_parent (gtk_widget_get_parent (gtk_widget_get_parent (gtk_widget_get_parent (GTK_WIDGET (self)))))));

  priv->toast_overlay = ADW_TOAST_OVERLAY (find_child_by_name (GTK_WIDGET (parent_page), "toast_overlay"));
  priv->finger_list = GTK_LIST_BOX (find_child_by_name (GTK_WIDGET (parent_page), "finger_list"));
  priv->enroll_step = find_child_by_name (GTK_WIDGET (parent_page), "enroll_step");
  priv->enroll_progress = GTK_PROGRESS_BAR (find_child_by_name (GTK_WIDGET (parent_page), "enroll_progress"));

  g_signal_connect (priv->finger_list, "row-activated", G_CALLBACK (on_finger_activated), self);

  if (init_dbus_proxies (self)) {
    refresh_fingerprint_list (self);
    pt_page_switch_to_subpage (parent_page);
  } else {
    g_warning ("Failed to initialize DBus proxies for biomd");
  }
}

static gboolean
fingerprint_hardware_available (void)
{
  GDBusProxy *proxy;
  GError *error = NULL;
  GVariant *result;
  GVariant *hardware_variant;
  gboolean available = FALSE;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         BIOMD_DBUS_NAME,
                                         BIOMD_DBUS_FINGERPRINT_PATH,
                                         "org.freedesktop.DBus.Properties",
                                         NULL,
                                         &error);

  if (error) {
    g_warning ("Error creating properties proxy: %s", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  result = g_dbus_proxy_call_sync (proxy,
                                   "Get",
                                   g_variant_new ("(ss)", BIOMD_DBUS_FINGERPRINT_INTERFACE, "HardwareAvailable"),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   NULL,
                                   &error);

  if (error) {
    g_warning ("Error getting HardwareAvailable: %s", error->message);
    g_clear_error (&error);
    g_object_unref (proxy);
    return FALSE;
  }

  g_variant_get (result, "(v)", &hardware_variant);
  available = g_variant_get_boolean (hardware_variant);

  g_debug ("Fingerprint hardware available: %s",
           available ? "true" : "false");

  g_variant_unref (hardware_variant);
  g_variant_unref (result);
  g_object_unref (proxy);

  return available;
}

static gboolean
ping_biomd (void)
{
  GDBusProxy *proxy;
  GError *error = NULL;
  GVariant *result;
  gboolean ping_result = FALSE;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         BIOMD_DBUS_NAME,
                                         "/io/FuriOS/Biomd",
                                         "io.FuriOS.Biomd",
                                         NULL,
                                         &error);

  if (error) {
    g_warning ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  result = g_dbus_proxy_call_sync (proxy,
                                   "Ping",
                                   NULL,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   NULL,
                                   &error);

  if (error) {
    g_warning ("Error calling Ping: %s", error->message);
    g_clear_error (&error);
  } else {
    g_variant_get (result, "(b)", &ping_result);
    g_variant_unref (result);
  }

  g_debug ("biomd ping result: %s",
           ping_result ? "true" : "false");

  g_object_unref (proxy);

  return ping_result;
}

gboolean
pt_security_settings_fingerprint_available (void)
{
  return ping_biomd () && fingerprint_hardware_available ();
}

void
pt_security_settings_fingerprint_finalize (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);

  g_clear_object (&priv->fingerprint_proxy);
  g_clear_object (&priv->props_proxy);
}
