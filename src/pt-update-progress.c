#include "furios-initial-setup-config.h"
#include "pt-update-progress.h"
#include "pt-page.h"

#include <adwaita.h>
#include <glib/gi18n.h>
#include <gio/gunixinputstream.h>

enum
{
  PROP_0,
  PROP_READY,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

typedef struct _PtUpdateProgressPrivate
{
  GtkProgressBar *progress;
  GtkLabel       *label;
  GtkButton      *reboot;
  gdouble        progress_value;
  gboolean       ready;
  gboolean       did_update_any;
  gboolean       is_truly_updating;
  GCancellable   *cancellable;
  GSubprocess    *subprocess;
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
pt_update_progress_finish (PtUpdateProgress *self)
{
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);

  priv->progress_value = 1.0;
  gtk_progress_bar_set_fraction (priv->progress, 1.0);
  gtk_label_set_label (priv->label, _("Good to go!"));

  if (!priv->did_update_any)
  {
    priv->ready = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_READY]);
  }
  else
  {
    gtk_widget_set_visible (GTK_WIDGET (priv->reboot), TRUE);
    gtk_widget_set_visible (GTK_WIDGET (priv->label), FALSE);
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
on_reboot_clicked (GtkButton *button,
                   gpointer user_data)
{
  // Ugleh, again.
  g_spawn_command_line_async ("systemctl reboot", NULL);
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
  gtk_widget_class_bind_template_child_private (widget_class, PtUpdateProgress, reboot);
  gtk_widget_class_bind_template_callback (widget_class, on_reboot_clicked);
}

static void
pt_update_progress_init (PtUpdateProgress *self)
{
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  gtk_widget_init_template (GTK_WIDGET (self));

  priv->ready = TRUE;
}

static void
update_progress_from_output (PtUpdateProgress *self, const gchar *line)
{
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  gchar **parts = g_strsplit (line, "|", 2);

  if (g_strv_length (parts) == 2)
  {
    const gchar *stage = parts[0];
    int progress = atoi (parts[1]);

    priv->progress_value = progress / 100.0;
    gtk_progress_bar_set_fraction (priv->progress, priv->progress_value);

    if (g_strcmp0 (stage, "check") == 0)
    {
      gtk_label_set_label (priv->label, _("Checking for updates…"));
    }
    else if (g_strcmp0 (stage, "download") == 0)
    {
      gtk_label_set_label (priv->label, _("Downloading updates…"));
    }
    else if (g_strcmp0 (stage, "install") == 0)
    {
      gtk_label_set_label (priv->label, _("Installing updates…"));
      priv->did_update_any = TRUE;
    }
  }
  else if (g_strcmp0 (line, "done") == 0)
  {
    pt_update_progress_finish (self);
  }

  g_strfreev (parts);
}

static gboolean
process_subprocess_output (GIOChannel *channel,
                           GIOCondition condition,
                           gpointer data)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (data);
  gchar *line;
  gsize length;
  GIOStatus status;

  if (condition & G_IO_HUP)
  {
    g_io_channel_unref (channel);
    return G_SOURCE_REMOVE;
  }

  status = g_io_channel_read_line (channel, &line, &length, NULL, NULL);

  if (status == G_IO_STATUS_NORMAL)
  {
    g_strstrip (line);
    update_progress_from_output (self, line);
    g_free (line);
  }

  return G_SOURCE_CONTINUE;
}

static void
update_helper_spawn_callback (GObject *source_object,
                              GAsyncResult *res,
                              gpointer user_data)
{
  PtUpdateProgress *self = PT_UPDATE_PROGRESS (user_data);
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  GError *error = NULL;

  if (!g_subprocess_wait_check_finish (G_SUBPROCESS (source_object), res, &error))
  {
    g_warning ("Helper process failed: %s", error->message);
    gtk_label_set_label (priv->label, _("Update failed"));
    g_error_free (error);
  }

  pt_update_progress_finish (self);
  g_clear_object (&priv->subprocess);
}

void
pt_update_progress_begin (PtUpdateProgress *self)
{
  PtUpdateProgressPrivate *priv = pt_update_progress_get_instance_private (self);
  GError *error = NULL;
  GInputStream *stdout_stream = NULL;
  GIOChannel *io_channel = NULL;

  priv->ready = FALSE;
  priv->did_update_any = FALSE;
  priv->progress_value = 0.0;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_READY]);

  gtk_progress_bar_set_fraction (priv->progress, 0.0);
  gtk_label_set_label (priv->label, _("Preparing to check for updates..."));
  gtk_widget_set_visible (GTK_WIDGET (priv->reboot), FALSE);

  if (priv->cancellable)
  {
    g_cancellable_cancel (priv->cancellable);
    g_clear_object (&priv->cancellable);
  }
  priv->cancellable = g_cancellable_new ();

  priv->subprocess = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
                                       &error,
                                       "pkexec",
                                       "/usr/libexec/furios-update-helper",
                                       NULL);

  if (error)
  {
    g_warning ("Failed to create subprocess: %s", error->message);
    g_error_free (error);
    return;
  }

  stdout_stream = g_subprocess_get_stdout_pipe (priv->subprocess);
  io_channel = g_io_channel_unix_new (g_unix_input_stream_get_fd (G_UNIX_INPUT_STREAM (stdout_stream)));
  
  g_io_channel_set_encoding (io_channel, NULL, NULL);
  g_io_channel_set_flags (io_channel, G_IO_FLAG_NONBLOCK, NULL);
  
  g_io_add_watch (io_channel, G_IO_IN | G_IO_HUP, process_subprocess_output, self);

  g_subprocess_wait_check_async (priv->subprocess,
                                 priv->cancellable,
                                 update_helper_spawn_callback,
                                 self);

  g_io_channel_unref (io_channel);
}

PtUpdateProgress *
pt_update_progress_new (void)
{
  return g_object_new (PT_TYPE_UPDATE_PROGRESS, NULL);
}
