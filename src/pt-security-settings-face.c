/*
 * Copyright (C) 2026 Furi Labs
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Bardia Moshiri <bardia@furilabs.com>
 */

#define _GNU_SOURCE
#define G_LOG_DOMAIN "pt-security"

#include "furios-initial-setup-config.h"
#include "pt-security-settings.h"
#include "pt-security-settings-private.h"
#include "pt-page.h"

#include <glib/gi18n.h>
#include <sys/mman.h>

#include <biomd/biomd_enums.h>
#include <fart/fart_enums.h>

#define BIOMD_DBUS_NAME              "io.FuriOS.Biomd"
#define BIOMD_DBUS_PATH              "/io/FuriOS/Biomd"
#define BIOMD_DBUS_INTERFACE         "io.FuriOS.Biomd"
#define BIOMD_DBUS_FACE_PATH         "/io/FuriOS/Biomd/Face"
#define BIOMD_DBUS_FACE_INTERFACE    "io.FuriOS.Biomd.Face"
#define BIOMD_DBUS_AGENT_INTERFACE   "io.FuriOS.Biomd.Face.Agent"

#define PT_FACE_FRAME_FORMAT_BGR     0u
#define PT_FACE_SUBMIT_INTERVAL_MS   200

typedef struct {
  PtSecuritySettings *self;
} FaceAsyncCtx;

static FaceAsyncCtx *
face_async_ctx_new (PtSecuritySettings *self)
{
  FaceAsyncCtx *ctx = g_new0 (FaceAsyncCtx, 1);
  ctx->self = g_object_ref (self);
  return ctx;
}

static void
face_async_ctx_free (FaceAsyncCtx *ctx)
{
  if (!ctx)
    return;

  g_clear_object (&ctx->self);
  g_free (ctx);
}

static void
show_toast (AdwToastOverlay *toast_overlay,
            const char      *format,
            ...)
{
  va_list args;
  char *message;
  AdwToast *toast;

  if (!toast_overlay)
    return;

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);

  toast = adw_toast_new (message);
  adw_toast_set_timeout (toast, 3);
  adw_toast_overlay_add_toast (toast_overlay, toast);

  g_free (message);
}

static gboolean
write_all (int           fd,
           const guint8 *buf,
           gsize         len)
{
  gsize off = 0;

  while (off < len) {
    ssize_t written = write (fd, buf + off, len - off);

    if (written < 0) {
      if (errno == EINTR)
        continue;
      return FALSE;
    }

    if (written == 0)
      return FALSE;

    off += (gsize) written;
  }

  return TRUE;
}

static gboolean
ping_biomd (void)
{
  GDBusProxy *proxy;
  GError *error = NULL;
  GVariant *result;
  gboolean ok = FALSE;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         BIOMD_DBUS_NAME,
                                         BIOMD_DBUS_PATH,
                                         BIOMD_DBUS_INTERFACE,
                                         NULL,
                                         &error);

  if (error) {
    g_warning ("Failed to create biomd root proxy: %s", error->message);
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
    g_warning ("Failed to ping biomd: %s", error->message);
    g_clear_error (&error);
    g_object_unref (proxy);
    return FALSE;
  }

  g_variant_get (result, "(b)", &ok);

  g_variant_unref (result);
  g_object_unref (proxy);

  return ok;
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

static const char *
enrollment_state_to_string (guint32 state)
{
  switch ((EnrollmentState) state) {
  case ENROLLMENT_IDLE:
    return _("Enrollment idle");
  case ENROLLMENT_FAIL:
    return _("Enrollment failed");
  case ENROLLMENT_IN_PROGRESS:
    return _("Enrollment in progress");
  case ENROLLMENT_COMPLETE:
    return _("Enrollment complete");
  case ENROLLMENT_SAVE_FAILED:
    return _("Enrollment completed, but saving failed");
  case ENROLLMENT_MULTIPLE_FACES:
    return _("Multiple faces detected");
  case ENROLLMENT_NO_FACE:
    return _("No face detected");
  case ENROLLMENT_BAD_LIGHTING:
    return _("Lighting is not good enough");
  default:
    return _("Unknown enrollment state");
  }
}

static gboolean
update_face_enrolled (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  GVariant *result = NULL;
  GVariant *value = NULL;
  GError *error = NULL;

  if (!priv->face_props_proxy)
    return FALSE;

  result = g_dbus_proxy_call_sync (priv->face_props_proxy,
                                   "Get",
                                   g_variant_new ("(ss)", BIOMD_DBUS_FACE_INTERFACE, "FaceEnrolled"),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   NULL,
                                   &error);

  if (error) {
    g_debug ("Failed to get face property FaceEnrolled: %s", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  g_variant_get (result, "(v)", &value);

  if (g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
    priv->face_enrolled = g_variant_get_boolean (value);

  g_variant_unref (value);
  g_variant_unref (result);

  return priv->face_enrolled;
}

static void
update_face_row (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);

  if (!priv->face_row)
    return;

  if (priv->face_enrolled) {
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (priv->face_row), _("Face Unlock"));
    adw_action_row_set_subtitle (priv->face_row, _("Already enrolled"));
    gtk_widget_set_sensitive (GTK_WIDGET (priv->face_row), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (priv->face_group), TRUE);
  } else {
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (priv->face_row), _("Register Face"));
    adw_action_row_set_subtitle (priv->face_row, _("Set up face unlock"));
    gtk_widget_set_sensitive (GTK_WIDGET (priv->face_row), TRUE);
  }
}

static void
destroy_face_agent (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  g_autofree gchar *path = NULL;

  priv->face_submit_frames_enabled = FALSE;
  priv->face_operation_started = FALSE;
  priv->face_start_call_in_flight = FALSE;
  priv->face_agent_has_access = FALSE;

  if (priv->face_agent_proxy)
    g_dbus_proxy_call (priv->face_agent_proxy,
                       "Cancel",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       NULL,
                       NULL);

  if (priv->face_proxy)
    g_dbus_proxy_call (priv->face_proxy,
                       "UnregisterEnrollmentAgent",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       NULL,
                       NULL);

  if (priv->face_agent_path && priv->face_proxy) {
    path = g_strdup (priv->face_agent_path);

    g_dbus_proxy_call (priv->face_proxy,
                       "DestroyAgent",
                       g_variant_new ("(o)", path),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       NULL,
                       NULL);
  }

  g_clear_object (&priv->face_agent_proxy);
  g_clear_pointer (&priv->face_agent_path, g_free);
}

static void
close_camera_step (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);

  priv->face_submit_frames_enabled = FALSE;
  priv->face_operation_started = FALSE;
  priv->face_start_call_in_flight = FALSE;
  priv->face_last_submit_us = 0;

  if (priv->face_camera_pipeline)
    gst_element_set_state (priv->face_camera_pipeline, GST_STATE_NULL);

  if (priv->face_bottom_sheet) {
    adw_bottom_sheet_set_open (priv->face_bottom_sheet, FALSE);
    gtk_widget_set_visible (GTK_WIDGET (priv->face_bottom_sheet), FALSE);
  }

  destroy_face_agent (self);
}

static void
finish_face_enrollment (PtSecuritySettings *self)
{
  update_face_row (self);
  close_camera_step (self);
}

static void
submit_frame_cb (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  PtSecuritySettings *self = PT_SECURITY_SETTINGS (user_data);
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  GUnixFDList *out_fd_list = NULL;
  GVariant *reply = NULL;
  GError *error = NULL;

  reply = g_dbus_proxy_call_with_unix_fd_list_finish (G_DBUS_PROXY (source),
                                                      &out_fd_list,
                                                      result,
                                                      &error);

  if (priv->face_submit_in_flight > 0)
    priv->face_submit_in_flight--;

  if (!reply && error) {
    g_debug ("SubmitFrame failed: %s", error->message);
    g_clear_error (&error);
  }

  if (out_fd_list)
    g_object_unref (out_fd_list);

  if (reply)
    g_variant_unref (reply);
}

static void
submit_frame (PtSecuritySettings *self,
              const guint8       *data,
              gsize               size,
              gint                width,
              gint                height,
              gint                channels)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  GUnixFDList *fd_list;
  GError *error = NULL;
  gint64 now_us;
  gsize expected;
  int fd;
  int handle;

  if (!priv->face_submit_frames_enabled ||
      !priv->face_operation_started ||
      !priv->face_agent_has_access ||
      !priv->face_agent_proxy)
    return;

  now_us = g_get_monotonic_time ();

  if (priv->face_last_submit_us &&
      now_us - priv->face_last_submit_us < (gint64) PT_FACE_SUBMIT_INTERVAL_MS * 1000)
    return;

  if (priv->face_submit_in_flight > 0)
    return;

  expected = (gsize) width * (gsize) height * (gsize) channels;

  if (size < expected)
    return;

  priv->face_last_submit_us = now_us;

  fd = memfd_create ("pt-face-frame", MFD_CLOEXEC);
  if (fd < 0) {
    g_debug ("memfd_create failed: %s", g_strerror (errno));
    return;
  }

  if (ftruncate (fd, (off_t) expected) != 0) {
    g_debug ("ftruncate failed: %s", g_strerror (errno));
    close (fd);
    return;
  }

  if (!write_all (fd, data, expected)) {
    g_debug ("Failed to write frame memfd: %s", g_strerror (errno));
    close (fd);
    return;
  }

  if (lseek (fd, 0, SEEK_SET) < 0) {
    g_debug ("Failed to rewind frame memfd: %s", g_strerror (errno));
    close (fd);
    return;
  }

  fd_list = g_unix_fd_list_new ();
  handle = g_unix_fd_list_append (fd_list, fd, &error);
  close (fd);

  if (handle < 0) {
    g_debug ("Failed to append fd to fd list: %s", error->message);
    g_clear_error (&error);
    g_object_unref (fd_list);
    return;
  }

  priv->face_submit_in_flight++;

  g_dbus_proxy_call_with_unix_fd_list (priv->face_agent_proxy,
                                       "SubmitFrame",
                                       g_variant_new ("(hiiiu)",
                                                      handle,
                                                      width,
                                                      height,
                                                      channels,
                                                      PT_FACE_FRAME_FORMAT_BGR),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1,
                                       fd_list,
                                       NULL,
                                       submit_frame_cb,
                                       self);

  g_object_unref (fd_list);
}

static GstFlowReturn
on_new_sample (GstElement *sink,
               gpointer    user_data)
{
  PtSecuritySettings *self = PT_SECURITY_SETTINGS (user_data);
  GstSample *sample = NULL;
  GstBuffer *buffer;
  GstCaps *caps;
  GstStructure *structure;
  GstMapInfo map;
  gint width = 0;
  gint height = 0;

  g_signal_emit_by_name (sink, "pull-sample", &sample);

  if (!sample)
    return GST_FLOW_OK;

  buffer = gst_sample_get_buffer (sample);
  caps = gst_sample_get_caps (sample);

  if (!buffer || !caps) {
    gst_sample_unref (sample);
    return GST_FLOW_OK;
  }

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height)) {
    gst_sample_unref (sample);
    return GST_FLOW_OK;
  }

  if (gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    submit_frame (self, map.data, map.size, width, height, 3);
    gst_buffer_unmap (buffer, &map);
  }

  gst_sample_unref (sample);

  return GST_FLOW_OK;
}

static gboolean
start_camera (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  GstElement *sink;
  GdkPaintable *paintable = NULL;
  GError *error = NULL;

  if (priv->face_camera_pipeline) {
    gst_element_set_state (priv->face_camera_pipeline, GST_STATE_PLAYING);
    return TRUE;
  }

  priv->face_camera_pipeline = gst_parse_launch (
    "droidcamsrc camera_device=1 mode=2 ! "
    "queue max-size-buffers=1 leaky=downstream ! "
    "video/x-raw,width=640,height=480 ! "
    "videoconvert ! videoflip video-direction=auto ! tee name=t "
    "t. ! queue ! videoconvert ! gtk4paintablesink name=viewsink sync=false "
    "t. ! queue max-size-buffers=1 leaky=downstream ! "
    "videoconvert ! video/x-raw,format=BGR ! "
    "appsink name=appsink max-buffers=1 drop=true emit-signals=true sync=false",
    &error);

  if (!priv->face_camera_pipeline) {
    show_toast (priv->toast_overlay,
                _("Unable to start camera: %s"),
                error ? error->message : _("Unknown error"));
    g_clear_error (&error);
    return FALSE;
  }

  sink = gst_bin_get_by_name (GST_BIN (priv->face_camera_pipeline), "viewsink");
  if (!sink) {
    show_toast (priv->toast_overlay, _("Unable to create camera viewfinder"));
    gst_clear_object (&priv->face_camera_pipeline);
    return FALSE;
  }

  g_object_get (sink, "paintable", &paintable, NULL);

  if (paintable) {
    gtk_picture_set_paintable (priv->face_viewfinder_picture, paintable);
    g_object_unref (paintable);
  }

  gst_object_unref (sink);

  priv->face_appsink = gst_bin_get_by_name (GST_BIN (priv->face_camera_pipeline), "appsink");
  if (!priv->face_appsink) {
    show_toast (priv->toast_overlay, _("Unable to create camera frame sink"));
    gst_clear_object (&priv->face_camera_pipeline);
    return FALSE;
  }

  g_signal_connect (priv->face_appsink,
                    "new-sample",
                    G_CALLBACK (on_new_sample),
                    self);

  gst_element_set_state (priv->face_camera_pipeline, GST_STATE_PLAYING);

  return TRUE;
}

static void
start_enrollment_cb (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  FaceAsyncCtx *ctx = user_data;
  PtSecuritySettings *self = ctx->self;
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  GVariant *reply = NULL;
  GError *error = NULL;
  gboolean success = FALSE;

  priv->face_start_call_in_flight = FALSE;

  reply = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

  if (reply) {
    if (g_variant_is_of_type (reply, G_VARIANT_TYPE ("(b)")))
      g_variant_get (reply, "(b)", &success);
    else
      success = TRUE;

    g_variant_unref (reply);
  }

  if (!success) {
    show_toast (priv->toast_overlay,
                _("Failed to start face enrollment: %s"),
                error ? error->message : _("operation returned false"));
    g_clear_error (&error);
    close_camera_step (self);
    face_async_ctx_free (ctx);
    return;
  }

  priv->face_operation_started = TRUE;
  priv->face_submit_frames_enabled = TRUE;
  priv->face_last_submit_us = 0;

  show_toast (priv->toast_overlay, _("Face enrollment started"));

  face_async_ctx_free (ctx);
}

static void
start_pending_enrollment (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  FaceAsyncCtx *ctx;

  if (!priv->face_agent_proxy ||
      !priv->face_agent_has_access ||
      priv->face_operation_started ||
      priv->face_start_call_in_flight)
    return;

  priv->face_start_call_in_flight = TRUE;
  ctx = face_async_ctx_new (self);

  g_dbus_proxy_call (priv->face_agent_proxy,
                     "StartEnrollment",
                     NULL,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     start_enrollment_cb,
                     ctx);
}

static void
on_agent_signal (GDBusProxy *proxy,
                 gchar      *sender_name,
                 gchar      *signal_name,
                 GVariant   *parameters,
                 gpointer    user_data)
{
  PtSecuritySettings *self = PT_SECURITY_SETTINGS (user_data);
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);

  if (g_strcmp0 (signal_name, "AccessChanged") == 0) {
    gboolean has_access = FALSE;

    g_variant_get (parameters, "(b)", &has_access);
    priv->face_agent_has_access = has_access;

    if (has_access && !priv->face_operation_started)
      start_pending_enrollment (self);
    else if (!has_access)
      show_toast (priv->toast_overlay, _("Face agent access was revoked"));
  } else if (g_strcmp0 (signal_name, "EnrollmentProgressChanged") == 0) {
    gint32 progress = 0;

    g_variant_get (parameters, "(i)", &progress);
    progress = CLAMP (progress, 0, 100);

    gtk_widget_set_visible (GTK_WIDGET (priv->face_enroll_progress), TRUE);
    gtk_progress_bar_set_fraction (priv->face_enroll_progress, progress / 100.0);

    priv->face_last_enrollment_progress = progress;
  } else if (g_strcmp0 (signal_name, "EnrollmentStateChanged") == 0) {
    guint32 state = 0;

    g_variant_get (parameters, "(u)", &state);

    if (state != priv->face_last_enrollment_state) {
      priv->face_last_enrollment_state = state;

      if (state != ENROLLMENT_IDLE &&
          state != ENROLLMENT_IN_PROGRESS &&
          state != ENROLLMENT_FAIL)
        show_toast (priv->toast_overlay, "%s", enrollment_state_to_string (state));
    }
  }
}

static void
register_agent_cb (GObject      *source,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  FaceAsyncCtx *ctx = user_data;
  PtSecuritySettings *self = ctx->self;
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  GVariant *reply = NULL;
  GError *error = NULL;
  gboolean success = FALSE;

  reply = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

  if (error) {
    if (g_strstr_len (error->message, -1, "already registered")) {
      g_debug ("Face enrollment agent already registered, waiting for access handoff");
      g_clear_error (&error);
      face_async_ctx_free (ctx);
      return;
    }

    show_toast (priv->toast_overlay,
                _("Failed to register face enrollment agent: %s"),
                error->message);
    g_clear_error (&error);
    close_camera_step (self);
    face_async_ctx_free (ctx);
    return;
  }

  if (reply) {
    if (g_variant_is_of_type (reply, G_VARIANT_TYPE ("(b)")))
      g_variant_get (reply, "(b)", &success);
    else
      success = TRUE;

    g_variant_unref (reply);
  }

  if (!success) {
    show_toast (priv->toast_overlay,
                _("Failed to register face enrollment agent: %s"),
                _("operation returned false"));
    close_camera_step (self);
    face_async_ctx_free (ctx);
    return;
  }

  if (priv->face_agent_has_access)
    start_pending_enrollment (self);

  face_async_ctx_free (ctx);
}

static void
continue_face_enrollment (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  FaceAsyncCtx *ctx = face_async_ctx_new (self);

  priv->face_operation_started = FALSE;
  priv->face_start_call_in_flight = FALSE;
  priv->face_submit_frames_enabled = FALSE;

  g_dbus_proxy_call (priv->face_proxy,
                     "RegisterEnrollmentAgent",
                     g_variant_new ("(o)", priv->face_agent_path),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     register_agent_cb,
                     ctx);
}

static void
agent_proxy_created_cb (GObject      *source,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  FaceAsyncCtx *ctx = user_data;
  PtSecuritySettings *self = ctx->self;
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  GError *error = NULL;

  priv->face_agent_proxy = g_dbus_proxy_new_for_bus_finish (result, &error);

  if (!priv->face_agent_proxy) {
    show_toast (priv->toast_overlay,
                _("Failed to create face agent proxy: %s"),
                error ? error->message : _("operation returned false"));
    g_clear_error (&error);
    close_camera_step (self);
    face_async_ctx_free (ctx);
    return;
  }

  g_signal_connect (priv->face_agent_proxy,
                    "g-signal",
                    G_CALLBACK (on_agent_signal),
                    self);

  continue_face_enrollment (self);
  face_async_ctx_free (ctx);
}

static void
create_agent_cb (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  FaceAsyncCtx *ctx = user_data;
  PtSecuritySettings *self = ctx->self;
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  GVariant *reply = NULL;
  GError *error = NULL;
  const char *path = NULL;

  reply = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

  if (!reply) {
    show_toast (priv->toast_overlay,
                _("Failed to create face agent: %s"),
                error ? error->message : _("operation returned false"));
    g_clear_error (&error);
    close_camera_step (self);
    face_async_ctx_free (ctx);
    return;
  }

  g_variant_get (reply, "(&o)", &path);

  g_free (priv->face_agent_path);
  priv->face_agent_path = g_strdup (path);
  priv->face_agent_has_access = FALSE;

  g_variant_unref (reply);

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            BIOMD_DBUS_NAME,
                            priv->face_agent_path,
                            BIOMD_DBUS_AGENT_INTERFACE,
                            NULL,
                            agent_proxy_created_cb,
                            ctx);
}

static void
ensure_face_agent (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);

  if (priv->face_agent_proxy && priv->face_agent_path) {
    continue_face_enrollment (self);
    return;
  }

  g_dbus_proxy_call (priv->face_proxy,
                     "CreateAgent",
                     NULL,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     create_agent_cb,
                     face_async_ctx_new (self));
}

static void
on_face_signal (GDBusProxy *proxy,
                gchar      *sender_name,
                gchar      *signal_name,
                GVariant   *parameters,
                gpointer    user_data)
{
  PtSecuritySettings *self = PT_SECURITY_SETTINGS (user_data);
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);

  if (g_strcmp0 (signal_name, "FaceEnrolledChanged") == 0) {
    g_variant_get (parameters, "(b)", &priv->face_enrolled);
    update_face_row (self);

    if (priv->face_enrolled) {
      show_toast (priv->toast_overlay, _("Face enrolled"));
      finish_face_enrollment (self);
    }
  } else if (g_strcmp0 (signal_name, "StateChanged") == 0) {
    gint32 state = 0;

    g_variant_get (parameters, "(i)", &state);

    if (state == STATE_IDLE)
      g_debug ("Face state changed: IDLE");
    else if (state == STATE_ENROLLING)
      g_debug ("Face state changed: ENROLLING");
    else if (state == STATE_IDENTIFYING)
      g_debug ("Face state changed: IDENTIFYING");
    else
      g_debug ("Face state changed: %d", state);
  }
}

static gboolean
init_face_dbus_proxies (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  GError *error = NULL;

  if (priv->face_proxy && priv->face_props_proxy)
    return TRUE;

  priv->face_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                    G_DBUS_PROXY_FLAGS_NONE,
                                                    NULL,
                                                    BIOMD_DBUS_NAME,
                                                    BIOMD_DBUS_FACE_PATH,
                                                    BIOMD_DBUS_FACE_INTERFACE,
                                                    NULL,
                                                    &error);

  if (error) {
    g_warning ("Failed to create face proxy: %s", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  priv->face_props_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                          G_DBUS_PROXY_FLAGS_NONE,
                                                          NULL,
                                                          BIOMD_DBUS_NAME,
                                                          BIOMD_DBUS_FACE_PATH,
                                                          "org.freedesktop.DBus.Properties",
                                                          NULL,
                                                          &error);

  if (error) {
    g_warning ("Failed to create face properties proxy: %s", error->message);
    g_clear_error (&error);
    g_clear_object (&priv->face_proxy);
    return FALSE;
  }

  g_signal_connect (priv->face_proxy,
                    "g-signal",
                    G_CALLBACK (on_face_signal),
                    self);

  return TRUE;
}

static gboolean
update_face_available (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);
  GVariant *result = NULL;
  GVariant *value = NULL;
  GError *error = NULL;
  gint32 implementation_type = 0;

  if (!priv->face_props_proxy)
    return FALSE;

  result = g_dbus_proxy_call_sync (priv->face_props_proxy,
                                   "Get",
                                   g_variant_new ("(ss)", BIOMD_DBUS_FACE_INTERFACE, "ImplementationType"),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   NULL,
                                   &error);

  if (error) {
    g_debug ("Failed to get face property ImplementationType: %s", error->message);
    g_clear_error (&error);
    priv->face_available = FALSE;
    return FALSE;
  }

  g_variant_get (result, "(v)", &value);

  if (!g_variant_is_of_type (value, G_VARIANT_TYPE_INT32)) {
    g_variant_unref (value);
    g_variant_unref (result);
    priv->face_available = FALSE;
    return FALSE;
  }

  implementation_type = g_variant_get_int32 (value);
  priv->face_available = implementation_type != 0;

  g_variant_unref (value);
  g_variant_unref (result);

  return priv->face_available;
}

gboolean
pt_security_settings_face_available (PtSecuritySettings *self)
{
  gboolean available;

  available = ping_biomd () &&
              init_face_dbus_proxies (self) &&
              update_face_available (self);

  if (available) {
    update_face_enrolled (self);
    update_face_row (self);
  }

  return available;
}

static void
on_face_cancel_clicked (GtkButton          *button,
                        PtSecuritySettings *self)
{
  close_camera_step (self);
}

static void
on_face_enroll_clicked (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);

  update_face_enrolled (self);
  update_face_row (self);

  if (priv->face_enrolled)
    return;

  priv->face_agent_has_access = FALSE;
  priv->face_operation_started = FALSE;
  priv->face_start_call_in_flight = FALSE;
  priv->face_submit_frames_enabled = FALSE;
  priv->face_last_enrollment_state = G_MAXUINT32;
  priv->face_last_enrollment_progress = -1;

  gtk_progress_bar_set_fraction (priv->face_enroll_progress, 0.0);
  gtk_widget_set_visible (GTK_WIDGET (priv->face_bottom_sheet), TRUE);
  adw_bottom_sheet_set_open (priv->face_bottom_sheet, TRUE);

  if (!start_camera (self)) {
    close_camera_step (self);
    return;
  }

  ensure_face_agent (self);
}

void
pt_security_settings_register_face (PtSecuritySettings *self)
{
  GtkRoot *root;
  GtkWidget *root_widget;
  GtkWidget *cancel_button;
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);

  root = gtk_widget_get_root (GTK_WIDGET (self));
  root_widget = GTK_WIDGET (root);

  if (!priv->toast_overlay)
    priv->toast_overlay = ADW_TOAST_OVERLAY (find_child_by_name (root_widget, "toast_overlay"));

  priv->face_bottom_sheet = ADW_BOTTOM_SHEET (find_child_by_name (root_widget, "face_bottom_sheet"));
  priv->face_viewfinder_picture = GTK_PICTURE (find_child_by_name (root_widget, "face_viewfinder_picture"));
  priv->face_enroll_progress = GTK_PROGRESS_BAR (find_child_by_name (root_widget, "face_enroll_progress"));

  cancel_button = find_child_by_name (root_widget, "face_cancel_button");
  if (cancel_button)
    g_signal_connect (cancel_button,
                      "clicked",
                      G_CALLBACK (on_face_cancel_clicked),
                      self);

  on_face_enroll_clicked (self);
}

void
pt_security_settings_face_finalize (PtSecuritySettings *self)
{
  PtSecuritySettingsPrivate *priv = pt_security_settings_get_priv (self);

  destroy_face_agent (self);

  if (priv->face_camera_pipeline)
    gst_element_set_state (priv->face_camera_pipeline, GST_STATE_NULL);

  if (priv->face_appsink) {
    gst_object_unref (priv->face_appsink);
    priv->face_appsink = NULL;
  }

  gst_clear_object (&priv->face_camera_pipeline);

  g_clear_object (&priv->face_proxy);
  g_clear_object (&priv->face_props_proxy);
  g_clear_object (&priv->face_agent_proxy);
  g_clear_pointer (&priv->face_agent_path, g_free);
}
