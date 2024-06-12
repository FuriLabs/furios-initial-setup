/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Red Hat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 *     Matthias Clasen <mclasen@redhat.com>
 */

#include "furios-initial-setup-config.h"
#include "cc-network-list.h"

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <NetworkManager.h>


enum
{
  PROP_0,
  PROP_SIGNAL_INDICATOR_PICTURE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _CcNetworkListClass
{
  AdwBinClass parent_class;
};

struct _CcNetworkList
{
  AdwBin parent;
};

struct _CcNetworkListPrivate
{
  GtkWidget *network_list;
  NMClient *nm_client;
  NMDevice *nm_device;
  gboolean refreshing;
  GtkSizeGroup *icons;

  guint refresh_timeout_id;

  const char *signal_indicator_picture;
};

typedef struct _CcNetworkListPrivate CcNetworkListPrivate;
G_DEFINE_TYPE_WITH_PRIVATE (CcNetworkList, cc_network_list, ADW_TYPE_BIN);


static GPtrArray *
get_strongest_unique_aps (const GPtrArray *aps)
{
  GBytes *ssid;
  GBytes *ssid_tmp;
  GPtrArray *unique = NULL;
  gboolean add_ap;
  guint i;
  guint j;
  NMAccessPoint *ap;
  NMAccessPoint *ap_tmp;

  /* we will have multiple entries for typical hotspots,
   * just keep the one with the strongest signal
   */
  unique = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
  if (aps == NULL)
    goto out;

  for (i = 0; i < aps->len; i++) {
    ap = NM_ACCESS_POINT (g_ptr_array_index (aps, i));
    ssid = nm_access_point_get_ssid (ap);
    add_ap = TRUE;

    if (!ssid)
      continue;

    /* get already added list */
    for (j = 0; j < unique->len; j++) {
      ap_tmp = NM_ACCESS_POINT (g_ptr_array_index (unique, j));
      ssid_tmp = nm_access_point_get_ssid (ap_tmp);

      /* is this the same type and data? */
      if (ssid_tmp &&
          nm_utils_same_ssid (g_bytes_get_data (ssid, NULL), g_bytes_get_size (ssid),
            g_bytes_get_data (ssid_tmp, NULL), g_bytes_get_size (ssid_tmp), TRUE)) {
        /* the new access point is stronger */
        if (nm_access_point_get_strength (ap) >
            nm_access_point_get_strength (ap_tmp)) {
          g_ptr_array_remove (unique, ap_tmp);
          add_ap = TRUE;
        } else {
          add_ap = FALSE;
        }
        break;
      }
    }
    if (add_ap) {
      g_ptr_array_add (unique, g_object_ref (ap));
    }
  }

out:
  return unique;
}

static gint
ap_sort (GtkListBoxRow *a,
         GtkListBoxRow *b,
         gpointer data)
{
  GtkWidget *wa, *wb;
  guint sa, sb;

  wa = gtk_list_box_row_get_child (a);
  wb = gtk_list_box_row_get_child (b);

  sa = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (wa), "strength"));
  sb = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (wb), "strength"));
  if (sa > sb) return -1;
  if (sb > sa) return 1;

  return 0;
}

static void
add_access_point (CcNetworkList *self, NMAccessPoint *ap, NMAccessPoint *active)
{
  CcNetworkListPrivate *priv = cc_network_list_get_instance_private (self);
  GBytes *ssid;
  GBytes *ssid_active = NULL;
  gchar *ssid_text;
  const gchar *object_path;
  gboolean activated, activating;
  guint strength;
  const gchar *icon_name;
  GtkWidget *row;
  GtkWidget *widget;
  GtkWidget *grid;
  GtkWidget *state_widget = NULL;

  ssid = nm_access_point_get_ssid (ap);
  object_path = nm_object_get_path (NM_OBJECT (ap));

  if (ssid == NULL)
    return;
  ssid_text = nm_utils_ssid_to_utf8 (g_bytes_get_data (ssid, NULL), g_bytes_get_size (ssid));

  if (active)
    ssid_active = nm_access_point_get_ssid (active);
  if (ssid_active && nm_utils_same_ssid (g_bytes_get_data (ssid, NULL), g_bytes_get_size (ssid),
    g_bytes_get_data (ssid_active, NULL), g_bytes_get_size (ssid_active), TRUE)) {
    switch (nm_device_get_state (priv->nm_device)) {
    case NM_DEVICE_STATE_PREPARE:
    case NM_DEVICE_STATE_CONFIG:
    case NM_DEVICE_STATE_NEED_AUTH:
    case NM_DEVICE_STATE_IP_CONFIG:
    case NM_DEVICE_STATE_IP_CHECK:
    case NM_DEVICE_STATE_SECONDARIES:
    case NM_DEVICE_STATE_DEACTIVATING:
      activated = FALSE;
      activating = TRUE;
      break;
    case NM_DEVICE_STATE_ACTIVATED:
      activated = TRUE;
      activating = FALSE;
      break;
    case NM_DEVICE_STATE_UNKNOWN:
    case NM_DEVICE_STATE_FAILED:
    case NM_DEVICE_STATE_UNMANAGED:
    case NM_DEVICE_STATE_UNAVAILABLE:
    case NM_DEVICE_STATE_DISCONNECTED:
    default:
      activated = FALSE;
      activating = FALSE;
      break;
    }
  } else {
    activated = FALSE;
    activating = FALSE;
  }

  strength = nm_access_point_get_strength (ap);

  row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_start (row, 12);
  gtk_widget_set_margin_end (row, 12);

  grid = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
  gtk_grid_set_column_homogeneous (GTK_GRID (grid), TRUE);
  gtk_widget_set_valign (grid, GTK_ALIGN_CENTER);
  gtk_size_group_add_widget (priv->icons, grid);
  gtk_box_append (GTK_BOX (row), grid);

  widget = gtk_label_new (ssid_text);
  gtk_widget_set_margin_top (widget, 12);
  gtk_widget_set_margin_bottom (widget, 12);
  gtk_widget_set_hexpand (widget, TRUE);
  gtk_widget_set_halign (widget, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (row), widget);

  if (activated) {
    state_widget = gtk_image_new_from_icon_name ("object-select-symbolic");
  } else if (activating) {
    state_widget = gtk_spinner_new ();
    gtk_spinner_start (GTK_SPINNER (state_widget));
  }

  if (state_widget) {
    gtk_widget_set_halign (state_widget, GTK_ALIGN_START);
    gtk_widget_set_valign (state_widget, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (row), state_widget);
  }

  if (strength < 20)
    icon_name = "network-wireless-signal-none-symbolic";
  else if (strength < 40)
    icon_name = "network-wireless-signal-weak-symbolic";
  else if (strength < 50)
    icon_name = "network-wireless-signal-ok-symbolic";
  else if (strength < 80)
    icon_name = "network-wireless-signal-good-symbolic";
  else
    icon_name = "network-wireless-signal-excellent-symbolic";
  widget = gtk_image_new_from_icon_name (icon_name);
  gtk_widget_set_halign (widget, GTK_ALIGN_END);
  gtk_grid_attach (GTK_GRID (grid), widget, 1, 0, 1, 1);

  /* if this connection is the active one or is being activated, then make sure
   * it's sorted before all others */
  if (activating || activated)
    strength = G_MAXUINT;

  if (activated)
  {
    g_object_set (self, "signal-indicator", "resource:///mobi/phosh/PhoshTour/pages/connected-good.svg", NULL);
  }
  else if (activating)
  {
    g_object_set (self, "signal-indicator", "resource:///mobi/phosh/PhoshTour/pages/connected-ok.svg", NULL);
  }

  g_object_set_data (G_OBJECT (row), "object-path", (gpointer) object_path);
  g_object_set_data (G_OBJECT (row), "ssid", (gpointer) ssid);
  g_object_set_data (G_OBJECT (row), "strength", GUINT_TO_POINTER (strength));

  gtk_list_box_append (GTK_LIST_BOX (priv->network_list), row);
}

static gboolean refresh_wireless_list (CcNetworkList *self);

static void
cancel_periodic_refresh (CcNetworkList *self)
{
  CcNetworkListPrivate *priv = cc_network_list_get_instance_private (self);

  if (priv->refresh_timeout_id == 0)
    return;

  g_debug ("Stopping periodic/scheduled Wi-Fi list refresh");

  g_clear_handle_id (&priv->refresh_timeout_id, g_source_remove);
}

static gboolean
refresh_again (gpointer user_data)
{
  CcNetworkList *self = CC_NETWORK_LIST (user_data);
  refresh_wireless_list (self);
  return G_SOURCE_REMOVE;
}

static void
start_periodic_refresh (CcNetworkList *self)
{
  CcNetworkListPrivate *priv = cc_network_list_get_instance_private (self);
  static const guint periodic_wifi_refresh_timeout_sec = 5;

  cancel_periodic_refresh (self);

  g_debug ("Starting periodic Wi-Fi list refresh (every %u secs)",
           periodic_wifi_refresh_timeout_sec);
  priv->refresh_timeout_id = g_timeout_add_seconds (periodic_wifi_refresh_timeout_sec,
                                                    refresh_again, self);
}

static gboolean
refresh_wireless_list (CcNetworkList *self)
{
  CcNetworkListPrivate *priv = cc_network_list_get_instance_private (self);
  NMAccessPoint *active_ap = NULL;
  NMAccessPoint *ap;
  const GPtrArray *aps;
  GPtrArray *unique_aps;
  GtkWidget *child;
  guint i;

  g_debug ("Refreshing Wi-Fi networks list");

  priv->refreshing = TRUE;
  g_object_set (self, "signal-indicator", "resource:///mobi/phosh/PhoshTour/pages/not-connected.svg", NULL);

  g_assert (NM_IS_DEVICE_WIFI (priv->nm_device));

  cancel_periodic_refresh (self);

  nm_device_wifi_request_scan_async (NM_DEVICE_WIFI (priv->nm_device), NULL, NULL, NULL);
  active_ap = nm_device_wifi_get_active_access_point (NM_DEVICE_WIFI (priv->nm_device));

  while ((child = gtk_widget_get_first_child (priv->network_list)) != NULL)
    gtk_list_box_remove (GTK_LIST_BOX (priv->network_list), child);

  aps = nm_device_wifi_get_access_points (NM_DEVICE_WIFI (priv->nm_device));

  if (aps == NULL || aps->len == 0) {
    goto out;

  }

  unique_aps = get_strongest_unique_aps (aps);
  for (i = 0; i < unique_aps->len; i++) {
    ap = NM_ACCESS_POINT (g_ptr_array_index (unique_aps, i));
    add_access_point (self, ap, active_ap);
  }

  g_object_notify (G_OBJECT (self), "signal-indicator");
  g_ptr_array_unref (unique_aps);

out:

  start_periodic_refresh (self);

  priv->refreshing = FALSE;

  return G_SOURCE_REMOVE;
}


static void
connection_activate_cb (GObject *object,
                        GAsyncResult *result,
                        gpointer user_data)
{
  NMClient *client = NM_CLIENT (object);
  NMActiveConnection *connection = NULL;
  g_autoptr (GError) error = NULL;

  refresh_wireless_list (CC_NETWORK_LIST (user_data));

  connection = nm_client_activate_connection_finish (client, result, &error);
  if (connection != NULL) {
    g_clear_object (&connection);
  } else {
    /* failed to activate */
    g_warning ("Failed to activate a connection: %s", error->message);
  }
}

static void
connection_add_activate_cb (GObject *object,
                            GAsyncResult *result,
                            gpointer user_data)
{
  NMClient *client = NM_CLIENT (object);
  NMActiveConnection *connection = NULL;
  g_autoptr (GError) error = NULL;

  connection = nm_client_add_and_activate_connection_finish (client, result, &error);
  if (connection != NULL) {
    g_clear_object (&connection);
  } else {
    /* failed to activate */
    g_warning ("Failed to add and activate a connection: %s", error->message);
  }
}


static void
row_activated (GtkListBox *box,
               GtkListBoxRow *row,
               CcNetworkList *self)
{
  CcNetworkListPrivate *priv = cc_network_list_get_instance_private (self);
  gchar *object_path;
  const GPtrArray *list;
  GPtrArray *filtered;
  NMConnection *connection;
  NMConnection *connection_to_activate;
  NMSettingWireless *setting;
  GBytes *ssid;
  GBytes *ssid_target;
  GtkWidget *child;
  int i;

  if (priv->refreshing)
    return;

  child = gtk_list_box_row_get_child (row);
  object_path = g_object_get_data (G_OBJECT (child), "object-path");
  ssid_target = g_object_get_data (G_OBJECT (child), "ssid");

  list = nm_client_get_connections (priv->nm_client);
  filtered = nm_device_filter_connections (priv->nm_device, list);

  connection_to_activate = NULL;

  for (i = 0; i < filtered->len; i++) {
    connection = NM_CONNECTION (filtered->pdata[i]);
    setting = nm_connection_get_setting_wireless (connection);
    if (!NM_IS_SETTING_WIRELESS (setting))
      continue;

    ssid = nm_setting_wireless_get_ssid (setting);
    if (ssid == NULL)
      continue;

    if (nm_utils_same_ssid (g_bytes_get_data (ssid, NULL), g_bytes_get_size (ssid),
      g_bytes_get_data (ssid_target, NULL), g_bytes_get_size (ssid_target), TRUE)) {
      connection_to_activate = connection;
      break;
    }
  }
  g_ptr_array_unref (filtered);

  if (connection_to_activate != NULL) {
    nm_client_activate_connection_async (priv->nm_client,
                                         connection_to_activate,
                                         priv->nm_device, NULL,
                                         NULL,
                                         connection_activate_cb, self);
    return;
  }

  nm_client_add_and_activate_connection_async (priv->nm_client,
                                               NULL,
                                               priv->nm_device, object_path,
                                               NULL,
                                               connection_add_activate_cb, self);

  refresh_wireless_list (self);
}

static void
connection_state_changed (NMActiveConnection *c, GParamSpec *pspec, CcNetworkList *self)
{
  refresh_wireless_list (self);
}

static void
active_connections_changed (NMClient *client, GParamSpec *pspec, CcNetworkList *self)
{
  const GPtrArray *connections;
  guint i;

  connections = nm_client_get_active_connections (client);
  for (i = 0; connections && (i < connections->len); i++) {
    NMActiveConnection *connection;

    connection = g_ptr_array_index (connections, i);
    if (!g_object_get_data (G_OBJECT (connection), "has-state-changed-handler")) {
      g_signal_connect (connection, "notify::state",
                        G_CALLBACK (connection_state_changed), self);
      g_object_set_data (G_OBJECT (connection), "has-state-changed-handler", GINT_TO_POINTER (1));
    }
  }
}

static void
sync_complete (CcNetworkList *self)
{
  CcNetworkListPrivate *priv = cc_network_list_get_instance_private (self);

  if (priv->nm_device != NULL)
    refresh_wireless_list (self);
}

static void
device_state_changed (GObject *object, GParamSpec *param, CcNetworkList *self)
{
  sync_complete (self);
}

static void
find_best_device (CcNetworkList *self)
{
  CcNetworkListPrivate *priv = cc_network_list_get_instance_private (self);
  const GPtrArray *devices;
  guint i;

  /* FIXME: deal with multiple devices and devices being removed */
  if (priv->nm_device != NULL) {
    g_debug ("Already showing network device %s",
             nm_device_get_description (priv->nm_device));
    return;
  }

  devices = nm_client_get_devices (priv->nm_client);
  g_return_if_fail (devices != NULL);
  for (i = 0; i < devices->len; i++) {
    NMDevice *device = g_ptr_array_index (devices, i);

    if (!nm_device_get_managed (device))
      continue;

    if (nm_device_get_device_type (device) == NM_DEVICE_TYPE_WIFI) {
      /* FIXME deal with multiple, dynamic devices */
      priv->nm_device = g_object_ref (device);
      g_debug ("Showing network device %s",
               nm_device_get_description (priv->nm_device));

      g_signal_connect (priv->nm_device, "notify::state",
                        G_CALLBACK (device_state_changed), self);
      g_signal_connect (priv->nm_client, "notify::active-connections",
                        G_CALLBACK (active_connections_changed), self);

      break;
    }
  }

  sync_complete (self);
}

static void
cc_network_list_constructed (GObject *object)
{
  CcNetworkList *self = CC_NETWORK_LIST (object);
  CcNetworkListPrivate *priv = cc_network_list_get_instance_private (self);
  g_autoptr (GError) error = NULL;

  G_OBJECT_CLASS (cc_network_list_parent_class)->constructed (object);

  priv->icons = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

  gtk_list_box_set_selection_mode (GTK_LIST_BOX (priv->network_list), GTK_SELECTION_NONE);
  gtk_list_box_set_sort_func (GTK_LIST_BOX (priv->network_list), ap_sort, NULL, NULL);
  g_signal_connect (priv->network_list, "row-activated",
                  G_CALLBACK (row_activated), self);

  priv->nm_client = nm_client_new (NULL, &error);
  if (!priv->nm_client) {
    g_warning ("Can't create NetworkManager client, hiding network page: %s",
            error->message);
    sync_complete (self);
    return;
  }

  find_best_device (self);
}

static void
cc_network_list_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_network_list_parent_class)->finalize (object);
}


static void
cc_network_list_get_property (GObject *object,
                              guint prop_id,
                              GValue *value,
                              GParamSpec *pspec)
{
  CcNetworkList *self = CC_NETWORK_LIST (object);
  CcNetworkListPrivate *priv = cc_network_list_get_instance_private (self);

  switch (prop_id) {
  case PROP_SIGNAL_INDICATOR_PICTURE:
    g_value_set_string (value, priv->signal_indicator_picture);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}


static void
cc_network_list_set_property (GObject *object,
                              guint prop_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
  CcNetworkList *self = CC_NETWORK_LIST (object);
  CcNetworkListPrivate *priv = cc_network_list_get_instance_private (self);

  switch (prop_id) {
  case PROP_SIGNAL_INDICATOR_PICTURE:
    priv->signal_indicator_picture = g_value_get_string (value);
    g_object_notify (object, "signal-indicator");
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}


static void
cc_network_list_class_init (CcNetworkListClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->get_property = cc_network_list_get_property;
  object_class->set_property = cc_network_list_set_property;

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/control-center/network-list.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), CcNetworkList, network_list);
  
  props[PROP_SIGNAL_INDICATOR_PICTURE] =
    g_param_spec_string ("signal-indicator",
                         "Signal Indicator",
                         "The signal indicator picture",
                         NULL,
                         G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  object_class->finalize = cc_network_list_finalize;
  object_class->constructed = cc_network_list_constructed;
}

static void
cc_network_list_init (CcNetworkList *list)
{
  gtk_widget_init_template (GTK_WIDGET (list));
}


const char *
cc_network_list_get_signal_indicator_picture (CcNetworkList *self)
{
  CcNetworkListPrivate *priv = cc_network_list_get_instance_private (self);
  return priv->signal_indicator_picture;
}
