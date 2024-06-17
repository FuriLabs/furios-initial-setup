#include "furios-initial-setup-config.h"
#include "pt-update-progress.h"
#include "pt-page.h"

#include <adwaita.h>
#include <glib/gi18n.h>
#include <PackageKit/packagekit-glib2/packagekit.h>

enum
{
  PROP_0,
  PROP_READY,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

typedef struct _PtUpdateProgressPrivate
{
  PkClient       *client;
  GtkProgressBar *progress;
  GtkLabel       *label;
  gdouble        progress_value;
  gboolean       ready;
} PtUpdateProgressPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PtUpdateProgress, pt_update_progress, ADW_TYPE_BIN)


static void
pt_update_progress_set_property (GObject *object,
                                  guint property_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (object);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);

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
pt_update_progress_get_property (GObject *object,
                                  guint property_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (object);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);

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
pt_update_progress_class_init (PtUpdateProgressClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = pt_update_progress_set_property;
  object_class->get_property = pt_update_progress_get_property;

  props[PROP_READY] =
    g_param_spec_boolean ("ready",
                         "Ready",
                         "Whether we're good to go",
                         TRUE,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                              "/mobi/phosh/PhoshTour/ui/pt-update-progress.ui");

  gtk_widget_class_bind_template_child_private (widget_class, PtUpdateProgress, progress);
  gtk_widget_class_bind_template_child_private (widget_class, PtUpdateProgress, label);
}

static void
pt_update_progress_init (PtUpdateProgress *self)
{
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  gtk_widget_init_template (GTK_WIDGET (self));

  priv->ready = TRUE;
  priv->client = pk_client_new ();
}

static void
pt_update_progress_progress_cb (PkProgress *progress,
                                PkProgressType type,
                                gpointer user_data)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (user_data);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  gdouble fraction = pk_progress_get_percentage (progress);

  if (fraction > 1) {
      priv->progress_value = fraction / 100.0;
      gtk_progress_bar_set_fraction (priv->progress, priv->progress_value);
  }

  switch (pk_progress_get_status (progress))
  {
    case PK_STATUS_ENUM_DOWNLOAD_REPOSITORY:
    case PK_STATUS_ENUM_DOWNLOAD_PACKAGELIST:
    case PK_STATUS_ENUM_DOWNLOAD_FILELIST:
    case PK_STATUS_ENUM_DOWNLOAD_CHANGELOG:
    case PK_STATUS_ENUM_DOWNLOAD_UPDATEINFO:
    case PK_STATUS_ENUM_DOWNLOAD_GROUP:
    case PK_STATUS_ENUM_LOADING_CACHE:
    case PK_STATUS_ENUM_REFRESH_CACHE:
      gtk_label_set_label (priv->label, _("Checking for updates…"));
      break;
    case PK_STATUS_ENUM_DOWNLOAD:
      gtk_label_set_label (priv->label, _("Downloading updates…"));
      break;
    case PK_STATUS_ENUM_INSTALL:
      gtk_label_set_label (priv->label, _("Installing updates…"));
      break;
    default:
      g_warning ("Unhandled progress status: %d", pk_progress_get_status (progress));
      break;
  }
}

static gboolean
pt_update_progress_pulse_progress_cb (gpointer user_data)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (user_data);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);

  if (priv->progress_value >= 0.001)
    return G_SOURCE_REMOVE;

  gtk_progress_bar_pulse (priv->progress);

  return G_SOURCE_CONTINUE;
}


static void
pt_update_progress_finish (PtUpdateProgress *self)
{
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);

  priv->progress_value = 1.0;
  gtk_progress_bar_set_fraction (priv->progress, 1.0);
  gtk_label_set_label (priv->label, _("Good to go!"));
  priv->ready = TRUE;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_READY]);
}

static void
pt_update_progress_upgrade_done_cb (GObject *client_obj,
                                    GAsyncResult *res,
                                    gpointer user_data)
{
  PkClient *client = PK_CLIENT (client_obj);
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (user_data);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  GError *error = NULL;
  gchar *final_message = NULL;

  pk_client_generic_finish (client, res, &error);

  if (error)
  {
    g_warning ("Failed to upgrade packages: %s", error->message);
    final_message = g_strdup_printf (_("%s: %s"), _("Update failed"), _(error->message));
    gtk_label_set_label (priv->label, final_message);
    g_free (final_message);
    g_error_free (error);
  }

  pt_update_progress_finish (self);
}

static void
pt_update_progress_get_updates_cb (GObject *client_obj,
                                   GAsyncResult *res,
                                   gpointer user_data)
{
  PkClient *client = PK_CLIENT (client_obj);
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (user_data);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  GError *error = NULL;
  PkResults *results = pk_client_generic_finish (client, res, &error);
  PkPackageSack *sack = pk_results_get_package_sack (results);
  gchar **ids;

  if (error)
  {
    g_warning ("Failed to get updates: %s", error->message);
    g_error_free (error);
    pt_update_progress_finish (self);
    return;
  }

  if (pk_package_sack_get_size (sack) == 0)
  {
    g_debug ("No updates available");
    pt_update_progress_finish (self);
    return;
  }

  ids = pk_package_sack_get_ids (sack);

  pk_client_update_packages_async (priv->client,
                                   pk_bitfield_from_enums (PK_TRANSACTION_FLAG_ENUM_ONLY_TRUSTED, -1),
                                   ids,
                                   NULL,
                                   pt_update_progress_progress_cb,
                                   self,
                                   pt_update_progress_upgrade_done_cb,
                                   self);
}

void
pt_update_progress_begin (PtUpdateProgress *self)
{
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);

  // WTF: GTK progress bars need to be manually pumped for the pulse to move
  // ????????????????? what
  g_timeout_add (8, pt_update_progress_pulse_progress_cb, self);

  priv->ready = FALSE;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_READY]);

  gtk_label_set_label (priv->label, _("Checking for updates…"));

  pk_client_get_updates_async (priv->client,
                               PK_FILTER_ENUM_NONE,
                               NULL,
                               NULL,
                               NULL,
                               pt_update_progress_get_updates_cb,
                               self);
}

PtUpdateProgress *
pt_update_progress_new (void)
{
  return PT_UPDATE_PROGRESS (g_object_new (PT_TYPE_UPDATE_PROGRESS, NULL));
}
