/*
 * Copyright (C) 2025 Furi labs
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Bardia Moshiri <bardia@furilabs.com>
 *         Jesus Higueras <jesus@furilabs.com>
 */

#include "furios-initial-setup-config.h"
#include "pt-update-progress.h"
#include "pt-page.h"

#include <adwaita.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <libsoup/soup.h>
#include "ed25519/ed25519.h"

#define PROVISION_URL "http://provision.furios.io/"
/* PROVISION_URL being HTTP instead of HTTPS is OK because any script
 * that we need to run from that host will be signed with the key below
 * We use HTTP instead of HTTPS so that we can ensure we'll be able to
 * hot-patch any issues even if time sync fails, or Let's Encrypt stops
 * being trusted, or anything else that's out of our control. */
#define PROVISION_KEY "E4jOdnqXFR0mhBf6E+NAOxLvmAgteppg+b7CBJmy3j8="

enum
{
  PROP_0,
  PROP_READY,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

typedef enum
{
  TRANSACTION_TIME_SYNC,
  TRANSACTION_PROVISION_CHECK,
  TRANSACTION_UPDATE_CACHE,
  TRANSACTION_CHECK_UPDATES,
  TRANSACTION_INSTALL_UPDATES
} TransactionType;

typedef struct _PtUpdateProgressPrivate
{
  GDBusProxy     *aptkit_proxy;
  GDBusProxy     *transaction_proxy;
  GDBusProxy     *timedate_proxy;
  GtkProgressBar *progress;
  GtkLabel       *label;
  GtkButton      *reboot;
  gdouble         progress_value;
  gboolean        ready;
  gboolean        did_update_any;
  gboolean        had_error;
  gboolean        tried_safe_mode;
  gboolean        safe_mode;
  TransactionType current_transaction;
  guint           ntp_sync_attempts;
  guint           pulse_timeout_id;
} PtUpdateProgressPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PtUpdateProgress, pt_update_progress, ADW_TYPE_BIN)

static void
aptkit_transaction_signal_cb (GDBusProxy *proxy,
                              const gchar *sender_name,
                              const gchar *signal_name,
                              GVariant *parameters,
                              gpointer user_data);

static void
aptkit_update_cache_cb (GObject *source_object,
                        GAsyncResult *res,
                        gpointer user_data);

static void
set_ntp_cb (GObject *source_object,
            GAsyncResult *res,
            gpointer user_data);

static gboolean
pt_update_progress_check_valid_date (PtUpdateProgress *self)
{
  GDateTime *now;
  GDateTime *min_date;
  gboolean valid;

  now = g_date_time_new_now_local ();
  /* This code was written on 2025-03-10, so if we get a date before that, it means
   * we're not correctly synced up yet... */
  min_date = g_date_time_new_local (2025, 3, 10, 0, 0, 0);

  valid = g_date_time_compare (now, min_date) >= 0;

  g_date_time_unref (now);
  g_date_time_unref (min_date);

  return valid;
}

static void
pt_update_progress_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (object);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);

  switch (property_id) {
  case PROP_READY:
    priv->ready = g_value_get_boolean (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
pt_update_progress_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (object);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);

  switch (property_id) {
  case PROP_READY:
    g_value_set_boolean (value, priv->ready);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
on_reboot_clicked (GtkButton *button,
                   gpointer user_data)
{
  /* Ugleh, again. */
  g_spawn_command_line_async ("systemctl reboot", NULL);
}

static void
pt_update_progress_dispose (GObject *object)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (object);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);

  if (priv->pulse_timeout_id != 0) {
    g_source_remove (priv->pulse_timeout_id);
    priv->pulse_timeout_id = 0;
  }

  g_clear_object (&priv->aptkit_proxy);
  g_clear_object (&priv->transaction_proxy);
  g_clear_object (&priv->timedate_proxy);

  G_OBJECT_CLASS (pt_update_progress_parent_class)->dispose (object);
}

static void
pt_update_progress_class_init (PtUpdateProgressClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = pt_update_progress_set_property;
  object_class->get_property = pt_update_progress_get_property;
  object_class->dispose = pt_update_progress_dispose;

  props[PROP_READY] =
    g_param_spec_boolean ("ready",
                          "Ready",
                          "Whether we're good to go",
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/io/furios/InitialSetup/ui/pt-update-progress.ui");

  gtk_widget_class_bind_template_child_private (widget_class, PtUpdateProgress, progress);
  gtk_widget_class_bind_template_child_private (widget_class, PtUpdateProgress, label);
  gtk_widget_class_bind_template_child_private (widget_class, PtUpdateProgress, reboot);
  gtk_widget_class_bind_template_callback (widget_class, on_reboot_clicked);
}

static void
aptkit_proxy_setup_cb (GObject *source_object,
                       GAsyncResult *res,
                       gpointer user_data)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (user_data);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  GError *error = NULL;
  GDBusProxy *proxy;

  proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
  if (proxy == NULL) {
    g_warning ("Failed to connect to aptkit: %s", error->message);
    g_error_free (error);
    return;
  }

  priv->aptkit_proxy = proxy;
}

static void
pt_update_progress_init (PtUpdateProgress *self)
{
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  gtk_widget_init_template (GTK_WIDGET (self));

  priv->ready = TRUE;
  priv->aptkit_proxy = NULL;
  priv->transaction_proxy = NULL;
  priv->timedate_proxy = NULL;
  priv->ntp_sync_attempts = 0;
  priv->pulse_timeout_id = 0;

  priv->tried_safe_mode = FALSE;

  /* start with safe mode enabled, this will be changed if there is no upgrades with safe mode */
  priv->safe_mode = TRUE;

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            "org.aptkit",
                            "/org/aptkit",
                            "org.aptkit",
                            NULL,
                            aptkit_proxy_setup_cb,
                            self);
}

static gboolean
pt_update_progress_pulse_progress_cb (gpointer user_data)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (user_data);
  PtUpdateProgressPrivate *priv;
  GtkRoot *root;

  if (!PT_IS_UPDATE_PROGRESS (self))
    return G_SOURCE_REMOVE;

  priv = pt_update_progress_get_instance_private (self);

  if (priv->progress_value >= 0.001) {
    priv->pulse_timeout_id = 0;
    return G_SOURCE_REMOVE;
  }

  if (!GTK_IS_PROGRESS_BAR (priv->progress)) {
    priv->pulse_timeout_id = 0;
    return G_SOURCE_REMOVE;
  }

  /* Unfocus whatever is focused so the keyboard doesn't get stuck up */
  root = gtk_widget_get_root (GTK_WIDGET (self));
  if (GTK_IS_WINDOW (root))
    gtk_window_set_focus (GTK_WINDOW (root), GTK_WIDGET (priv->progress));

  gtk_progress_bar_pulse (priv->progress);

  return G_SOURCE_CONTINUE;
}

static void
pt_update_progress_finish (PtUpdateProgress *self)
{
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);

  if (priv->pulse_timeout_id != 0) {
    g_source_remove (priv->pulse_timeout_id);
    priv->pulse_timeout_id = 0;
  }

  priv->progress_value = 1.0;
  gtk_progress_bar_set_fraction (priv->progress, 1.0);

  if (!priv->had_error) {
    gtk_label_set_label (priv->label, _("Good to go!"));
    gtk_widget_set_visible (GTK_WIDGET (priv->label), TRUE);
  }

  if (priv->did_update_any) {
    gtk_widget_set_visible (GTK_WIDGET (priv->reboot), TRUE);
    gtk_widget_set_visible (GTK_WIDGET (priv->label), FALSE);
  }

  priv->ready = TRUE;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_READY]);
}

static void
pt_update_progress_start_update_cache (PtUpdateProgress *self);

static void
provision_message_complete_cb (GObject *source,
                               GAsyncResult *result,
                               gpointer user_data)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (user_data);
  SoupSession *session = SOUP_SESSION (source);
  SoupMessageHeaders *response_headers;
  const char *signature_base64 = NULL;
  guchar *signature = NULL;
  gsize signature_len = -1;
  guchar *key = NULL;
  gsize key_len = -1;
  SoupMessage *msg = NULL;
  GError *error = NULL;
  GBytes *bytes = soup_session_send_and_read_finish (SOUP_SESSION (source), result, &error);
  gconstpointer data;
  gsize length;
  gchar *script_path = NULL;
  guint status;

  msg = soup_session_get_async_result_message (session, result);
  status = soup_message_get_status (msg);

  if (status == SOUP_STATUS_NO_CONTENT) {
    /* 204 No Content - nothing to do */
    g_debug ("No provisioning script available");
    goto fail;
  } else if (status != SOUP_STATUS_OK) {
    /* Not 200 OK - just continue */
    g_warning ("Provision check failed with status %d", status);
    goto fail;
  }

  response_headers = soup_message_get_response_headers (msg);
  if (!response_headers) {
    g_debug ("No response headers");
    goto fail;
  }

  signature_base64 = soup_message_headers_get_one (response_headers, "X-Furi-Signature");
  if (!signature_base64 || !signature_base64[0]) {
    g_debug("Missing X-Furi-Signature");
    goto fail;
  }

  signature = g_base64_decode (signature_base64, &signature_len);
  if (!signature) {
    g_debug ("X-Furi-Signature was not a valid base64 string");
    goto fail;
  }

  if (signature_len != 64) {
    g_debug ("X-Furi-Signature has an invalid length (%ld)", signature_len);
    goto fail;
  }

  key = g_base64_decode (PROVISION_KEY, &key_len);

  /* 200 OK - guess we got something to do! */
  data = g_bytes_get_data (bytes, &length);

  if (length == 0) {
    g_debug ("Empty response from provision check");
    goto fail;
  }

  if (!ed25519_verify (signature, data, length, key)) {
    g_debug ("Provision script signature check failed");
    goto fail;
  } else {
    g_debug ("Provision script signature check PASSED");
  }

  /* Hell yeah, signature check passed too. It's time to RUN IT */
  script_path = g_build_filename (g_get_tmp_dir (), "furios-provision", NULL);
  close (g_mkstemp (script_path));

  g_file_set_contents (script_path, data, length, &error);
  if (error) {
    g_warning ("Failed to write provision script: %s", error->message);
    goto fail;
  }

  if (g_chmod (script_path, 0755) < 0) {
    g_warning ("Failed to make provision script executable: %s", g_strerror (errno));
    goto fail;
  }

  g_debug ("Executing provision script: %s", script_path);

  g_spawn_command_line_sync (script_path, NULL, NULL, NULL, &error);
  if (error)
    g_warning ("Failed to execute provision script: %s", error->message);

fail:
  if (script_path)
    g_free (script_path);

  pt_update_progress_start_update_cache (self);
  return;
}

static void
pt_update_progress_start_provision_check (PtUpdateProgress *self)
{
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  SoupSession *session;
  SoupMessage *msg;
  GBytes *bytes;
  g_autoptr(GError) error = NULL;
  GString *post_data;
  gchar *command_output = NULL;
  gint exit_status;

  priv->current_transaction = TRANSACTION_PROVISION_CHECK;
  gtk_label_set_label (priv->label, _("Checking for updates…"));

  post_data = g_string_new ("");

  if (g_spawn_command_line_sync ("uname -a", &command_output, NULL, &exit_status, &error) && exit_status == 0) {
    g_string_append_printf (post_data, "uname=%s&", g_uri_escape_string (g_strstrip (command_output), NULL, TRUE));
    g_free (command_output);
    command_output = NULL;
  } else {
    g_warning ("Failed to execute uname: %s", error ? error->message : "unknown error");
    g_clear_error (&error);
  }

  if (g_spawn_command_line_sync ("getprop ro.vendor.build.version.sdk", &command_output, NULL, &exit_status, &error) && exit_status == 0) {
    g_string_append_printf (post_data, "sdk_version=%s&", g_uri_escape_string (g_strstrip (command_output), NULL, TRUE));
    g_free (command_output);
    command_output = NULL;
  } else {
    g_warning ("Failed to get SDK version: %s", error ? error->message : "unknown error");
    g_clear_error (&error);
  }

  if (g_spawn_command_line_sync ("getprop ro.vendor.build.date.utc", &command_output, NULL, &exit_status, &error) && exit_status == 0) {
    g_string_append_printf (post_data, "build_date=%s&", g_uri_escape_string (g_strstrip (command_output), NULL, TRUE));
    g_free (command_output);
    command_output = NULL;
  } else {
    g_warning ("Failed to get build date: %s", error ? error->message : "unknown error");
    g_clear_error (&error);
  }

  if (g_spawn_command_line_sync ("getprop ro.vendor.build.fingerprint", &command_output, NULL, &exit_status, &error) && exit_status == 0) {
    g_string_append_printf (post_data, "build_fingerprint=%s&", g_uri_escape_string (g_strstrip (command_output), NULL, TRUE));
    g_free (command_output);
    command_output = NULL;
  } else {
    g_warning ("Failed to get build fingerprint: %s", error ? error->message : "unknown error");
    g_clear_error (&error);
  }

  if (g_file_get_contents ("/usr/share/furios-branding/furios-version", &command_output, NULL, &error)) {
    g_string_append_printf (post_data, "furios_version=%s", g_uri_escape_string (g_strstrip (command_output), NULL, TRUE));
    g_free (command_output);
    command_output = NULL;
  } else {
    g_warning ("Failed to read FuriOS version: %s", error ? error->message : "unknown error");
    g_clear_error (&error);
  }

  session = soup_session_new ();
  msg = soup_message_new ("POST", PROVISION_URL);

  bytes = g_bytes_new (post_data->str, post_data->len);
  soup_message_set_request_body_from_bytes (msg, "application/x-www-form-urlencoded", bytes);

  soup_session_send_and_read_async (session, msg, G_PRIORITY_DEFAULT, NULL,
                                    provision_message_complete_cb, self);
}

static void
pt_update_progress_start_update_cache (PtUpdateProgress *self)
{
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  gchar *error_message = NULL;

  priv->current_transaction = TRANSACTION_UPDATE_CACHE;
  gtk_label_set_label (priv->label, _("Checking for updates…"));

  if (priv->aptkit_proxy) {
    g_dbus_proxy_call (priv->aptkit_proxy,
                       "UpdateCache",
                       g_variant_new ("()"),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       aptkit_update_cache_cb,
                       self);
  } else {
    g_warning ("Cannot check for updates, aptkit proxy not available");
    priv->had_error = TRUE;
    error_message = g_strdup_printf ("%s: %s", _("Update failed"), _("aptkit not available"));
    gtk_label_set_label (priv->label, error_message);
    g_free (error_message);
    pt_update_progress_finish (self);
  }
}

static gboolean
retry_ntp_sync (gpointer user_data)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (user_data);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);

  g_dbus_proxy_call (priv->timedate_proxy,
                     "SetNTP",
                     g_variant_new ("(bb)", TRUE, TRUE),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     set_ntp_cb,
                     self);

  return G_SOURCE_REMOVE;
}

static void
set_ntp_cb (GObject *source_object,
            GAsyncResult *res,
            gpointer user_data)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (user_data);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  const guint MAX_NTP_ATTEMPTS = 15;
  GError *error = NULL;
  GVariant *result;
  GVariant *ntp_value;
  gboolean ntp_active = FALSE;

  result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);
  if (result == NULL) {
    g_warning ("Failed to set NTP: %s", error->message);
    g_error_free (error);

    if (priv->ntp_sync_attempts < MAX_NTP_ATTEMPTS) {
      priv->ntp_sync_attempts++;
      g_print ("Retrying NTP sync, attempt %d/%d\n", priv->ntp_sync_attempts, MAX_NTP_ATTEMPTS);

      /* Schedule the next attempt after 3 seconds */
      g_timeout_add_seconds (3, retry_ntp_sync, self);
      return;
    } else {
      priv->had_error = TRUE;
      g_warning ("Giving up after %d failed attempts to synchronize system clock", MAX_NTP_ATTEMPTS);
      gtk_label_set_label (priv->label, _("Couldn't synchronize system clock"));
      pt_update_progress_finish (self);
      return;
    }
  }

  g_variant_unref (result);

  ntp_value = g_dbus_proxy_get_cached_property (priv->timedate_proxy, "NTP");
  if (ntp_value != NULL) {
    ntp_active = g_variant_get_boolean (ntp_value);
    g_variant_unref (ntp_value);
  }

  if (ntp_active && pt_update_progress_check_valid_date (self)) {
    /* NTP is active and date is valid, proceed with updates */
    pt_update_progress_start_provision_check (self);
  } else if (priv->ntp_sync_attempts < MAX_NTP_ATTEMPTS) {
    priv->ntp_sync_attempts++;
    g_print ("Retrying NTP sync, attempt %d/%d\n", priv->ntp_sync_attempts, MAX_NTP_ATTEMPTS);

    /* Schedule the next attempt after 3 seconds */
    g_timeout_add_seconds (3, retry_ntp_sync, self);
  } else if (!pt_update_progress_check_valid_date (self)) {
    /* NTP failed MAX_NTP_ATTEMPTS times in a row AND our date still looks wrong... give up. */
    priv->had_error = TRUE;
    g_warning ("Giving up after %d attempts - couldn't synchronize system clock and date is invalid", MAX_NTP_ATTEMPTS);
    gtk_label_set_label (priv->label, _("Couldn't synchronize system clock"));
    pt_update_progress_finish (self);
  }
}

static void
timedate_proxy_setup_cb (GObject *source_object,
                         GAsyncResult *res,
                         gpointer user_data)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (user_data);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  GError *error = NULL;
  GDBusProxy *proxy;

  proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
  if (proxy == NULL) {
    g_warning ("Failed to connect to timedate1: %s", error->message);
    g_error_free (error);

    priv->current_transaction = TRANSACTION_PROVISION_CHECK;
    gtk_label_set_label (priv->label, _("Checking for updates…"));

    pt_update_progress_start_provision_check (self);
    return;
  }

  priv->timedate_proxy = proxy;

  gtk_label_set_label (priv->label, _("Synchronizing system clock…"));
  g_dbus_proxy_call (proxy,
                     "SetNTP",
                     g_variant_new ("(bb)", TRUE, TRUE),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     set_ntp_cb,
                     self);
}

static void
aptkit_transaction_run_cb (GObject *source_object,
                           GAsyncResult *res,
                           gpointer user_data)
{
  GError *error = NULL;
  GVariant *result;

  result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);
  if (result == NULL) {
    g_warning ("Failed to run transaction: %s", error->message);
    g_error_free (error);
    return;
  }

  g_variant_unref (result);
}

static void
aptkit_transaction_proxy_cb (GObject *source_object,
                             GAsyncResult *res,
                             gpointer user_data)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (user_data);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  GError *error = NULL;
  GDBusProxy *transaction_proxy;
  const gchar *method;

  transaction_proxy = g_dbus_proxy_new_finish (res, &error);
  if (transaction_proxy == NULL) {
    g_warning ("Failed to create transaction proxy: %s", error->message);
    g_error_free (error);
    pt_update_progress_finish (self);
    return;
  }

  g_clear_object (&priv->transaction_proxy);
  priv->transaction_proxy = transaction_proxy;

  g_signal_connect (transaction_proxy, "g-signal",
                    G_CALLBACK (aptkit_transaction_signal_cb),
                    self);

  /* use "Simulate" for checking, "Run" for actual operations */
  if (priv->current_transaction == TRANSACTION_CHECK_UPDATES)
    method = "Simulate";
  else
    method = "Run";

  g_debug ("Calling %s on transaction for action %d", method, priv->current_transaction);

  g_dbus_proxy_call (transaction_proxy,
                     method,
                     g_variant_new ("()"),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     aptkit_transaction_run_cb,
                     NULL);
}

static void
aptkit_upgrade_system_cb (GObject *source_object,
                          GAsyncResult *res,
                          gpointer user_data)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (user_data);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  GError *error = NULL;
  gchar *error_message = NULL;
  GVariant *result;
  const gchar *transaction_path;

  result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);
  if (result == NULL) {
    g_warning ("Failed to initiate system upgrade: %s", error->message);
    priv->had_error = TRUE;
    error_message = g_strdup_printf ("%s: %s", _("Update failed"), _(error->message));
    gtk_label_set_label (priv->label, error_message);
    g_free (error_message);
    g_error_free (error);
    pt_update_progress_finish (self);
    return;
  }

  g_variant_get (result, "(&s)", &transaction_path);
  g_variant_unref (result);

  g_dbus_proxy_new (g_dbus_proxy_get_connection (priv->aptkit_proxy),
                    G_DBUS_PROXY_FLAGS_NONE,
                    NULL,
                    "org.aptkit",
                    transaction_path,
                    "org.aptkit.transaction",
                    NULL,
                    aptkit_transaction_proxy_cb,
                    self);
}

static void
aptkit_update_cache_cb (GObject *source_object,
                        GAsyncResult *res,
                        gpointer user_data)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (user_data);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  GError *error = NULL;
  gchar *error_message = NULL;
  GVariant *result;
  const gchar *transaction_path;

  result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);
  if (result == NULL) {
    g_warning ("Failed to refresh cache: %s", error->message);
    priv->had_error = TRUE;
    error_message = g_strdup_printf ("%s: %s", _("Update failed"), _(error->message));
    gtk_label_set_label (priv->label, error_message);
    g_free (error_message);
    g_error_free (error);
    pt_update_progress_finish (self);
    return;
  }

  g_variant_get (result, "(&s)", &transaction_path);
  g_debug ("Got transaction path: %s", transaction_path);
  g_variant_unref (result);

  g_dbus_proxy_new (g_dbus_proxy_get_connection (priv->aptkit_proxy),
                    G_DBUS_PROXY_FLAGS_NONE,
                    NULL,
                    "org.aptkit",
                    transaction_path,
                    "org.aptkit.transaction",
                    NULL,
                    aptkit_transaction_proxy_cb,
                    self);
}

static void
aptkit_get_updates (PtUpdateProgress *self)
{
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);

  if (!priv->aptkit_proxy) {
    g_warning ("Cannot get updates, aptkit proxy not available");
    pt_update_progress_finish (self);
    return;
  }

  priv->current_transaction = TRANSACTION_CHECK_UPDATES;

  g_debug ("Checking for updates with safe mode: %s", priv->safe_mode ? "on" : "off");

  gtk_label_set_label (priv->label, _("Checking for updates…"));

  g_dbus_proxy_call (priv->aptkit_proxy,
                     "UpgradeSystem",
                     g_variant_new ("(b)", priv->safe_mode),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     aptkit_upgrade_system_cb,
                     self);
}

static gboolean
aptkit_check_for_updates (PtUpdateProgress *self,
                          GVariant *packages,
                          GVariant *dependencies)
{
  g_autoptr(GVariant) pkg_upgrades = NULL;
  g_autoptr(GVariant) dep_upgrades = NULL;
  g_autoptr(GVariant) pkg_downgrades = NULL;
  g_autoptr(GVariant) dep_downgrades = NULL;
  GVariantIter iter;

  pkg_upgrades = g_variant_get_child_value (packages, 4);
  dep_upgrades = g_variant_get_child_value (dependencies, 4);

  pkg_downgrades = g_variant_get_child_value (packages, 5);
  dep_downgrades = g_variant_get_child_value (dependencies, 5);

  /* Check for upgrades */
  g_variant_iter_init (&iter, pkg_upgrades);
  if (g_variant_iter_n_children (&iter) > 0)
    return TRUE;

  /* Check for package downgrades */
  g_variant_iter_init (&iter, pkg_downgrades);
  if (g_variant_iter_n_children (&iter) > 0)
    return TRUE;

  /* Check for dependency upgrades */
  g_variant_iter_init (&iter, dep_upgrades);
  if (g_variant_iter_n_children (&iter) > 0)
    return TRUE;

  /* Check for dependency downgrades */
  g_variant_iter_init (&iter, dep_downgrades);
  if (g_variant_iter_n_children (&iter) > 0)
    return TRUE;

  /* OK, nothing to see here */
  return FALSE;
}

static void
aptkit_transaction_signal_cb (GDBusProxy *proxy,
                              const gchar *sender_name,
                              const gchar *signal_name,
                              GVariant *parameters,
                              gpointer user_data)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (user_data);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);

  if (g_strcmp0 (signal_name, "PropertyChanged") == 0) {
    const gchar *property_name;
    GVariant *value;
    g_variant_get (parameters, "(&sv)", &property_name, &value);

    if (g_strcmp0 (property_name, "ExitState") == 0) {
      const gchar *exit_state;
      g_variant_get (value, "&s", &exit_state);
      g_debug ("Exit state changed to: %s", exit_state);

      if (g_strcmp0 (exit_state, "exit-success") == 0) {
        if (priv->current_transaction == TRANSACTION_UPDATE_CACHE) {
          /* after cache update, check for updates starting with safe mode (call Simulate, safe mode TRUE) */
          g_debug ("Cache update successful, checking for updates in safe mode");
          priv->safe_mode = TRUE;
          aptkit_get_updates (self);
        } else if (priv->current_transaction == TRANSACTION_CHECK_UPDATES) {
          /* after checking for updates in either mode */
          if (priv->did_update_any) {
            /* if we found and processed updates, we're done. only finish if we're in normal mode or already tried normal mode */
            if (!priv->safe_mode || priv->tried_safe_mode) {
              pt_update_progress_finish (self);
            } else {
              /* if in safe mode and updates were found, next try normal mode */
              g_debug ("Updates installed in safe mode, now checking normal mode");
              priv->tried_safe_mode = TRUE;
              priv->safe_mode = FALSE;
              aptkit_get_updates (self);
            }
          } else if (priv->safe_mode && !priv->tried_safe_mode) {
            /* if in safe mode and no updates found, try normal mode */
            g_debug ("No updates found in safe mode, trying without safe mode");
            priv->tried_safe_mode = TRUE;
            priv->safe_mode = FALSE;
            aptkit_get_updates (self);
          } else {
            /* no updates in either mode, or updates were processed in both modes */
            pt_update_progress_finish (self);
          }
        } else if (priv->current_transaction == TRANSACTION_INSTALL_UPDATES) {
          /* After installing updates, check if there are more in the same mode */
          g_debug ("Updates installed, checking for more updates");
          aptkit_get_updates (self);
        }
      } else if (g_strcmp0 (exit_state, "exit-failed") == 0 ||
                 g_strcmp0 (exit_state, "exit-cancelled") == 0 ||
                 g_strcmp0 (exit_state, "exit-previous-failed") == 0) {
        g_warning ("Transaction failed with state: %s", exit_state);
        priv->had_error = TRUE;
        gtk_label_set_label (priv->label, _("Update failed"));
        pt_update_progress_finish (self);
      }
    } else if (g_strcmp0 (property_name, "Progress") == 0 &&
               priv->current_transaction == TRANSACTION_INSTALL_UPDATES) {
      gint progress;
      g_variant_get (value, "i", &progress);
      if (progress > 0) {
        priv->progress_value = (gdouble) progress / 100.0;
        gtk_progress_bar_set_fraction (priv->progress, priv->progress_value);
      }
    } else if (g_strcmp0 (property_name, "Status") == 0) {
      const gchar *status;
      g_variant_get (value, "&s", &status);

      if (g_str_has_prefix (status, "downloading"))
        gtk_label_set_label (priv->label, _("Downloading updates…"));
      else if (g_str_has_prefix (status, "installing"))
        gtk_label_set_label (priv->label, _("Installing updates…"));
      else if (g_str_has_prefix (status, "refreshing"))
        gtk_label_set_label (priv->label, _("Checking for updates…"));
    } else if (g_strcmp0 (property_name, "Packages") == 0 ||
               g_strcmp0 (property_name, "Dependencies") == 0) {
      g_autoptr(GVariant) packages = NULL;
      g_autoptr(GVariant) dependencies = NULL;

      /* Get both properties - one will be the 'value' parameter, get the other from proxy */
      if (g_strcmp0 (property_name, "Packages") == 0) {
        packages = g_variant_ref (value);
        dependencies = g_dbus_proxy_get_cached_property (proxy, "Dependencies");
      } else {
        dependencies = g_variant_ref (value);
        packages = g_dbus_proxy_get_cached_property (proxy, "Packages");
      }

      if (packages != NULL && dependencies != NULL &&
          priv->current_transaction == TRANSACTION_CHECK_UPDATES) {
        gboolean have_updates = aptkit_check_for_updates (self, packages, dependencies);

        g_debug ("Updates check completed. Updates available: %s", have_updates ? "yes" : "no");

        if (have_updates) {
          /* found updates, install them */
          priv->current_transaction = TRANSACTION_INSTALL_UPDATES;
          priv->did_update_any = TRUE;

          gtk_label_set_label (priv->label, _("Installing updates…"));

          g_dbus_proxy_call (priv->aptkit_proxy,
                             "UpgradeSystem",
                             g_variant_new ("(b)", priv->safe_mode),
                             G_DBUS_CALL_FLAGS_NONE,
                             -1,
                             NULL,
                             aptkit_upgrade_system_cb,
                             self);
        } else if (priv->safe_mode && !priv->tried_safe_mode) {
          /* no updates in safe mode, try normal mode */
          g_debug ("No updates found in safe mode, trying without safe mode");
          priv->tried_safe_mode = TRUE;
          priv->safe_mode = FALSE;
          aptkit_get_updates (self);
        } else {
          /* No updates in either mode, we're done */
          g_debug ("No updates found in normal mode either");
          pt_update_progress_finish (self);
        }
      }
    }

    g_variant_unref (value);
  }
}

void
pt_update_progress_begin (PtUpdateProgress *self)
{
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);

  /* WTF: GTK progress bars need to be manually pumped for the pulse to move
   * ????????????????? what */
  priv->pulse_timeout_id = g_timeout_add (8, pt_update_progress_pulse_progress_cb, self);

  priv->ready = FALSE;
  priv->did_update_any = FALSE;
  priv->had_error = FALSE;
  priv->ntp_sync_attempts = 0;
  priv->current_transaction = TRANSACTION_TIME_SYNC;
  priv->progress_value = 0.0;

  /* start with safe mode enabled, this will be changed if there is no upgrades with safe mode */
  priv->safe_mode = TRUE;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_READY]);

  gtk_progress_bar_set_fraction (priv->progress, 0.0);
  gtk_widget_set_visible (GTK_WIDGET (priv->label), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (priv->reboot), FALSE);
  gtk_label_set_label (priv->label, _("Synchronizing system clock…"));

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            "org.freedesktop.timedate1",
                            "/org/freedesktop/timedate1",
                            "org.freedesktop.timedate1",
                            NULL,
                            timedate_proxy_setup_cb,
                            self);
}

void
pt_update_progress_skip (PtUpdateProgress *self)
{
  PtUpdateProgressPrivate *priv;

  g_return_if_fail (PT_IS_UPDATE_PROGRESS (self));

  priv = pt_update_progress_get_instance_private (self);

  if (priv->pulse_timeout_id != 0) {
    g_source_remove (priv->pulse_timeout_id);
    priv->pulse_timeout_id = 0;
  }

  /* Don't try to cancel in-flight transactions here; just stop blocking the flow. */
  priv->had_error = FALSE;
  priv->did_update_any = FALSE;
  priv->progress_value = 1.0;

  gtk_progress_bar_set_fraction (priv->progress, 1.0);
  gtk_widget_set_visible (GTK_WIDGET (priv->reboot), FALSE);

  gtk_widget_set_sensitive (GTK_WIDGET (self), FALSE);

  priv->ready = TRUE;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_READY]);
}

PtUpdateProgress *
pt_update_progress_new (void)
{
  return PT_UPDATE_PROGRESS (g_object_new (PT_TYPE_UPDATE_PROGRESS, NULL));
}
