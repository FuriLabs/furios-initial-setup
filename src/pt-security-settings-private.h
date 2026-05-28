/*
 * Copyright (C) 2026 Furi Labs
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Bardia Moshiri <bardia@furilabs.com>
 */

#pragma once

#include "pt-security-settings.h"

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _PtSecuritySettingsPrivate
{
  AdwPasswordEntryRow *password_entry;
  AdwPasswordEntryRow *verify_entry;
  gboolean ready;
  ApplyCallback apply_cb;
  gpointer apply_user_data;

  GDBusProxy *fingerprint_proxy;
  GDBusProxy *props_proxy;
  GtkWidget *fingerprint_group;
  AdwToastOverlay *toast_overlay;
  GtkListBox *finger_list;
  GtkWidget *enroll_step;
  GtkProgressBar *enroll_progress;

  GDBusProxy *face_proxy;
  GDBusProxy *face_props_proxy;
  GDBusProxy *face_agent_proxy;
  gchar *face_agent_path;

  GtkWidget *face_group;
  AdwActionRow *face_row;
  AdwBottomSheet *face_bottom_sheet;
  GtkPicture *face_viewfinder_picture;
  GtkProgressBar *face_enroll_progress;
  GtkButton *face_cancel_button;

  GstElement *face_camera_pipeline;
  GstElement *face_appsink;

  gboolean face_available;
  gboolean face_enrolled;
  gboolean face_agent_has_access;
  gboolean face_operation_started;
  gboolean face_start_call_in_flight;
  gboolean face_submit_frames_enabled;
  gboolean face_closing_sheet;

  guint face_submit_in_flight;
  gint64 face_last_submit_us;
  guint32 face_last_enrollment_state;
  gint32 face_last_enrollment_progress;

  gulong face_cancel_clicked_id;
  gulong face_bottom_sheet_notify_id;
} PtSecuritySettingsPrivate;

PtSecuritySettingsPrivate *pt_security_settings_get_priv (PtSecuritySettings *self);

void pt_security_settings_register_fingerprint (PtSecuritySettings *self);
gboolean pt_security_settings_fingerprint_available (void);
void pt_security_settings_fingerprint_finalize (PtSecuritySettings *self);

void pt_security_settings_register_face (PtSecuritySettings *self);
gboolean pt_security_settings_face_available (PtSecuritySettings *self);
void pt_security_settings_face_finalize (PtSecuritySettings *self);

G_END_DECLS
