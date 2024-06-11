#pragma once

#include <gtk/gtk.h>
#include <glib-object.h>

#include <adwaita.h>

G_BEGIN_DECLS

#define CC_TYPE_NETWORK_LIST            (cc_network_list_get_type ())

G_DECLARE_FINAL_TYPE (CcNetworkList, cc_network_list, CC, NETWORK_LIST, AdwBin)

G_END_DECLS

const char *cc_network_list_get_signal_indicator_picture (CcNetworkList *self);
