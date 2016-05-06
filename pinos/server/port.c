/* Pinos
 * Copyright (C) 2015 Wim Taymans <wim.taymans@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <string.h>

#include <gst/gst.h>
#include <gio/gio.h>

#include "pinos/client/pinos.h"
#include "pinos/client/enumtypes.h"

#include "pinos/server/port.h"
#include "pinos/server/node.h"

#include "pinos/dbus/org-pinos.h"

#define PINOS_PORT_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), PINOS_TYPE_PORT, PinosPortPrivate))

struct _PinosPortPrivate
{
  PinosNode *node;
  PinosPort1 *iface;
  gchar *object_path;

  gchar *name;
  PinosDirection direction;
  GBytes *possible_formats;
  PinosProperties *properties;

  GList *channels;
};

G_DEFINE_TYPE (PinosPort, pinos_port, G_TYPE_OBJECT);

enum
{
  PROP_0,
  PROP_NODE,
  PROP_OBJECT_PATH,
  PROP_NAME,
  PROP_DIRECTION,
  PROP_POSSIBLE_FORMATS,
  PROP_PROPERTIES
};

enum
{
  SIGNAL_FORMAT_REQUEST,
  SIGNAL_CHANNEL_ADDED,
  SIGNAL_CHANNEL_REMOVED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
pinos_port_get_property (GObject    *_object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  PinosPort *port = PINOS_PORT (_object);
  PinosPortPrivate *priv = port->priv;

  switch (prop_id) {
    case PROP_NODE:
      g_value_set_object (value, priv->node);
      break;

    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;

    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;

    case PROP_DIRECTION:
      g_value_set_enum (value, priv->direction);
      break;

    case PROP_POSSIBLE_FORMATS:
      g_value_set_boxed (value, priv->possible_formats);
      break;

    case PROP_PROPERTIES:
      g_value_set_boxed (value, priv->properties);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (port, prop_id, pspec);
      break;
  }
}

static void
set_possible_formats (PinosPort *port, GBytes *formats)
{
  PinosPortPrivate *priv;
  GList *walk;

  g_return_if_fail (PINOS_IS_PORT (port));
  priv = port->priv;

  if (priv->possible_formats)
    g_bytes_unref (priv->possible_formats);
  priv->possible_formats = formats ? g_bytes_ref (formats) : NULL;

  if (priv->iface)
    g_object_set (priv->iface, "possible-formats",
                  g_bytes_get_data (formats, NULL),
                  NULL);

  for (walk = priv->channels; walk; walk = g_list_next (walk))
    g_object_set (walk->data, "possible-formats", formats, NULL);
}

static void
pinos_port_set_property (GObject      *_object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  PinosPort *port = PINOS_PORT (_object);
  PinosPortPrivate *priv = port->priv;

  switch (prop_id) {
    case PROP_NODE:
      priv->node = g_value_dup_object (value);
      break;

    case PROP_NAME:
      priv->name = g_value_dup_string (value);
      break;

    case PROP_DIRECTION:
      priv->direction = g_value_get_enum (value);
      break;

    case PROP_POSSIBLE_FORMATS:
      set_possible_formats (port, g_value_get_boxed (value));
      break;

    case PROP_PROPERTIES:
      if (priv->properties)
        pinos_properties_free (priv->properties);
      priv->properties = g_value_dup_boxed (value);
      if (priv->iface)
        g_object_set (priv->iface,
            "properties", priv->properties ?
                      pinos_properties_to_variant (priv->properties) : NULL,
            NULL);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (port, prop_id, pspec);
      break;
  }
}

static void
port_register_object (PinosPort *port)
{
  PinosPortPrivate *priv = port->priv;
  GBytes *formats;
  GVariant *variant;
  PinosObjectSkeleton *skel;
  gchar *name;

  name = g_strdup_printf ("%s/port", pinos_node_get_object_path (priv->node));
  skel = pinos_object_skeleton_new (name);
  g_free (name);

  formats = pinos_port_get_formats (port, NULL, NULL);

  if (priv->properties)
    variant = pinos_properties_to_variant (priv->properties);
  else
    variant = NULL;

  priv->iface = pinos_port1_skeleton_new ();
  g_object_set (priv->iface, "name", priv->name,
                             "node", pinos_node_get_object_path (priv->node),
                             "direction", priv->direction,
                             "properties", variant,
                             "possible-formats", g_bytes_get_data (formats, NULL),
                             NULL);
  g_bytes_unref (formats);
  pinos_object_skeleton_set_port1 (skel, priv->iface);

  g_free (priv->object_path);
  priv->object_path = pinos_daemon_export_uniquely (pinos_node_get_daemon (priv->node),
      G_DBUS_OBJECT_SKELETON (skel));

  g_object_unref (skel);

  pinos_node_add_port (priv->node, port);

  return;
}

static void
port_unregister_object (PinosPort *port)
{
  PinosPortPrivate *priv = port->priv;

  pinos_node_remove_port (priv->node, port);
  g_clear_object (&priv->iface);
}

static void
pinos_port_constructed (GObject * object)
{
  PinosPort *port = PINOS_PORT (object);

  port_register_object (port);

  G_OBJECT_CLASS (pinos_port_parent_class)->constructed (object);
}

static void
do_remove_channel (PinosChannel *channel,
                  gpointer       user_data)
{
  pinos_channel_remove (channel);
}

static void
pinos_port_dispose (GObject * object)
{
  PinosPort *port = PINOS_PORT (object);
  PinosPortPrivate *priv = port->priv;

  g_list_foreach (priv->channels, (GFunc) do_remove_channel, port);
  port_unregister_object (port);

  G_OBJECT_CLASS (pinos_port_parent_class)->dispose (object);
}

static void
pinos_port_finalize (GObject * object)
{
  PinosPort *port = PINOS_PORT (object);
  PinosPortPrivate *priv = port->priv;

  g_free (priv->name);
  if (priv->possible_formats)
    g_bytes_unref (priv->possible_formats);
  if (priv->properties)
    pinos_properties_free (priv->properties);

  G_OBJECT_CLASS (pinos_port_parent_class)->finalize (object);
}

static void
handle_remove_channel (PinosChannel *channel,
                       gpointer      user_data)
{
  PinosPort *port = user_data;

  pinos_port_release_channel (port, channel);
}

static PinosChannel *
default_create_channel (PinosPort       *port,
                        const gchar     *client_path,
                        GBytes          *format_filter,
                        PinosProperties *props,
                        GError          **error)
{
  PinosPortPrivate *priv = port->priv;
  PinosChannel *channel;
  GBytes *possible_formats;

  possible_formats = pinos_port_get_formats (port, format_filter, error);
  if (possible_formats == NULL)
    return NULL;

  channel = g_object_new (PINOS_TYPE_CHANNEL, "daemon", pinos_node_get_daemon (priv->node),
                                              "client-path", client_path,
                                              "direction", priv->direction,
                                              "port-path", pinos_port_get_object_path (port),
                                              "possible-formats", possible_formats,
                                              "properties", props,
                                              NULL);
  g_bytes_unref (possible_formats);

  if (channel == NULL)
    goto no_channel;

  g_signal_connect (channel,
                    "remove",
                    (GCallback) handle_remove_channel,
                    port);

  priv->channels = g_list_prepend (priv->channels, channel);

  return g_object_ref (channel);

  /* ERRORS */
no_channel:
  {
    if (error)
      *error = g_error_new (G_IO_ERROR,
                            G_IO_ERROR_FAILED,
                            "Could not create channel");
    return NULL;
  }
}

static gboolean
default_release_channel (PinosPort  *port,
                         PinosChannel *channel)
{
  PinosPortPrivate *priv = port->priv;
  GList *find;

  find = g_list_find (priv->channels, channel);
  if (find == NULL)
    return FALSE;

  priv->channels = g_list_delete_link (priv->channels, find);
  g_object_unref (channel);

  return TRUE;
}

static void
pinos_port_class_init (PinosPortClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (PinosPortPrivate));

  gobject_class->constructed = pinos_port_constructed;
  gobject_class->dispose = pinos_port_dispose;
  gobject_class->finalize = pinos_port_finalize;
  gobject_class->set_property = pinos_port_set_property;
  gobject_class->get_property = pinos_port_get_property;

  g_object_class_install_property (gobject_class,
                                   PROP_NODE,
                                   g_param_spec_object ("node",
                                                        "Node",
                                                        "The Node",
                                                        PINOS_TYPE_NODE,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_OBJECT_PATH,
                                   g_param_spec_string ("object-path",
                                                        "Object Path",
                                                        "The object path",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_NAME,
                                   g_param_spec_string ("name",
                                                        "Name",
                                                        "The port name",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_DIRECTION,
                                   g_param_spec_enum ("direction",
                                                      "Direction",
                                                      "The direction of the port",
                                                      PINOS_TYPE_DIRECTION,
                                                      PINOS_DIRECTION_INVALID,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_CONSTRUCT_ONLY |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_POSSIBLE_FORMATS,
                                   g_param_spec_boxed ("possible-formats",
                                                       "Possible Formats",
                                                       "The possbile formats of the port",
                                                       G_TYPE_BYTES,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_CONSTRUCT |
                                                       G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_PROPERTIES,
                                   g_param_spec_boxed ("properties",
                                                       "Properties",
                                                       "The properties of the port",
                                                       PINOS_TYPE_PROPERTIES,
                                                       G_PARAM_READWRITE |
                                                       G_PARAM_CONSTRUCT |
                                                       G_PARAM_STATIC_STRINGS));


  signals[SIGNAL_FORMAT_REQUEST] = g_signal_new ("format-request",
                                                 G_TYPE_FROM_CLASS (klass),
                                                 G_SIGNAL_RUN_LAST,
                                                 0,
                                                 NULL,
                                                 NULL,
                                                 g_cclosure_marshal_generic,
                                                 G_TYPE_NONE,
                                                 0,
                                                 G_TYPE_NONE);
  signals[SIGNAL_CHANNEL_ADDED] = g_signal_new ("channel-added",
                                                G_TYPE_FROM_CLASS (klass),
                                                G_SIGNAL_RUN_LAST,
                                                0,
                                                NULL,
                                                NULL,
                                                g_cclosure_marshal_generic,
                                                G_TYPE_NONE,
                                                1,
                                                PINOS_TYPE_CHANNEL);
  signals[SIGNAL_CHANNEL_REMOVED] = g_signal_new ("channel-removed",
                                                  G_TYPE_FROM_CLASS (klass),
                                                  G_SIGNAL_RUN_LAST,
                                                  0,
                                                  NULL,
                                                  NULL,
                                                  g_cclosure_marshal_generic,
                                                  G_TYPE_NONE,
                                                  1,
                                                  PINOS_TYPE_CHANNEL);


  klass->create_channel = default_create_channel;
  klass->release_channel = default_release_channel;
}

static void
pinos_port_init (PinosPort * port)
{
  PinosPortPrivate *priv = port->priv = PINOS_PORT_GET_PRIVATE (port);

  priv->direction = PINOS_DIRECTION_INVALID;
}

/**
 * pinos_port_new:
 *
 *
 * Returns: a new #PinosPort
 */
PinosPort *
pinos_port_new (PinosNode       *node,
                PinosDirection   direction,
                const gchar     *name,
		GBytes          *possible_formats,
                PinosProperties *props)
{
  g_return_val_if_fail (PINOS_IS_NODE (node), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  return g_object_new (PINOS_TYPE_PORT,
                       "node", node,
                       "direction", direction,
                       "name", name,
                       "possible-formats", possible_formats,
                       "properties", props,
                       NULL);
}

const gchar *
pinos_port_get_object_path (PinosPort *port)
{
  PinosPortPrivate *priv;

  g_return_val_if_fail (PINOS_IS_PORT (port), NULL);
  priv = port->priv;

  return priv->object_path;
}

/**
 * pinos_port_get_formats:
 * @port: a #PinosPort
 * @filter: a #GBytes
 * @error: a #GError or %NULL
 *
 * Get all the currently supported formats for @port and filter the
 * results with @filter.
 *
 * Returns: the list of supported format. If %NULL is returned, @error will
 * be set.
 */
GBytes *
pinos_port_get_formats (PinosPort  *port,
                        GBytes     *filter,
                        GError    **error)
{
  GstCaps *tmp, *caps, *cfilter;
  gchar *str;
  PinosPortPrivate *priv;

  g_return_val_if_fail (PINOS_IS_PORT (port), NULL);
  priv = port->priv;

  if (filter) {
    cfilter = gst_caps_from_string (g_bytes_get_data (filter, NULL));
    if (cfilter == NULL)
      goto invalid_filter;
  } else {
    cfilter = NULL;
  }

  g_signal_emit (port, signals[SIGNAL_FORMAT_REQUEST], 0, NULL);

  if (priv->possible_formats)
    caps = gst_caps_from_string (g_bytes_get_data (priv->possible_formats, NULL));
  else
    caps = gst_caps_new_any ();

  if (caps && cfilter) {
    tmp = gst_caps_intersect_full (caps, cfilter, GST_CAPS_INTERSECT_FIRST);
    g_clear_pointer (&cfilter, gst_caps_unref);
    gst_caps_take (&caps, tmp);
  }
  if (caps == NULL || gst_caps_is_empty (caps))
    goto no_format;

  str = gst_caps_to_string (caps);
  gst_caps_unref (caps);

  return g_bytes_new_take (str, strlen (str) + 1);

invalid_filter:
  {
    if (error)
      *error = g_error_new (G_IO_ERROR,
                            G_IO_ERROR_INVALID_ARGUMENT,
                            "Invalid filter received");
    return NULL;
  }
no_format:
  {
    if (error)
      *error = g_error_new (G_IO_ERROR,
                            G_IO_ERROR_NOT_FOUND,
                            "No compatible format found");
    if (cfilter)
      gst_caps_unref (cfilter);
    if (caps)
      gst_caps_unref (caps);
    return NULL;
  }
}


/**
 * pinos_port_get_channels:
 * @port: a #PinosPort
 *
 * Get all the channels in @port.
 *
 * Returns: a #GList of #PinosChannel objects.
 */
GList *
pinos_port_get_channels (PinosPort *port)
{
  PinosPortPrivate *priv;

  g_return_val_if_fail (PINOS_IS_PORT (port), NULL);
  priv = port->priv;

  return priv->channels;
}

/**
 * pinos_port_create_channel:
 * @port: a #PinosPort
 * @client_path: the client path
 * @format_filter: a #GBytes
 * @props: #PinosProperties
 * @prefix: a prefix
 * @error: a #GError or %NULL
 *
 * Create a new #PinosChannel for @port.
 *
 * Returns: a new #PinosChannel or %NULL, in wich case @error will contain
 *          more information about the error.
 */
PinosChannel *
pinos_port_create_channel (PinosPort       *port,
                           const gchar     *client_path,
                           GBytes          *format_filter,
                           PinosProperties *props,
                           GError          **error)
{
  PinosPortClass *klass;
  PinosChannel *channel;

  g_return_val_if_fail (PINOS_IS_PORT (port), NULL);

  klass = PINOS_PORT_GET_CLASS (port);

  if (klass->create_channel) {
    channel = klass->create_channel (port, client_path, format_filter, props, error);
    if (channel)
      g_signal_emit (port, signals[SIGNAL_CHANNEL_ADDED], 0, channel);
  } else {
    if (error) {
      *error = g_error_new (G_IO_ERROR,
                            G_IO_ERROR_NOT_SUPPORTED,
                            "CreateChannel not implemented");
    }
    channel = NULL;
  }

  return channel;
}

/**
 * pinos_port_release_channel:
 * @port: a #PinosPort
 * @channel: a #PinosChannel
 *
 * Release the @channel in @port.
 *
 * Returns: %TRUE on success.
 */
gboolean
pinos_port_release_channel (PinosPort    *port,
                            PinosChannel *channel)
{
  PinosPortClass *klass;
  gboolean res;

  g_return_val_if_fail (PINOS_IS_PORT (port), FALSE);
  g_return_val_if_fail (PINOS_IS_CHANNEL (channel), FALSE);

  klass = PINOS_PORT_GET_CLASS (port);

  if (klass->release_channel) {
    g_signal_emit (port, signals[SIGNAL_CHANNEL_REMOVED], 0, channel);
    res = klass->release_channel (port, channel);
  }
  else
    res = FALSE;

  return res;
}
