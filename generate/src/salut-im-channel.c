/*
 * salut-im-channel.c - Source for SalutIMChannel
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "salut-im-channel.h"
#include "salut-im-channel-signals-marshal.h"

#include "salut-im-channel-glue.h"

G_DEFINE_TYPE(SalutIMChannel, salut_im_channel, G_TYPE_OBJECT)

/* signal enum */
enum
{
    CLOSED,
    LOST_MESSAGE,
    RECEIVED,
    SEND_ERROR,
    SENT,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _SalutIMChannelPrivate SalutIMChannelPrivate;

struct _SalutIMChannelPrivate
{
  gboolean dispose_has_run;
};

#define SALUT_IM_CHANNEL_GET_PRIVATE(obj) \
    ((SalutIMChannelPrivate *)obj->priv)

static void
salut_im_channel_init (SalutIMChannel *self)
{
  SalutIMChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_IM_CHANNEL, SalutIMChannelPrivate);

  self->priv = priv;

  /* allocate any data required by the object here */
}

static void salut_im_channel_dispose (GObject *object);
static void salut_im_channel_finalize (GObject *object);

static void
salut_im_channel_class_init (SalutIMChannelClass *salut_im_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_im_channel_class);

  g_type_class_add_private (salut_im_channel_class, sizeof (SalutIMChannelPrivate));

  object_class->dispose = salut_im_channel_dispose;
  object_class->finalize = salut_im_channel_finalize;

  signals[CLOSED] =
    g_signal_new ("closed",
                  G_OBJECT_CLASS_TYPE (salut_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[LOST_MESSAGE] =
    g_signal_new ("lost-message",
                  G_OBJECT_CLASS_TYPE (salut_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[RECEIVED] =
    g_signal_new ("received",
                  G_OBJECT_CLASS_TYPE (salut_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_im_channel_marshal_VOID__UINT_UINT_UINT_UINT_UINT_STRING,
                  G_TYPE_NONE, 6, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  signals[SEND_ERROR] =
    g_signal_new ("send-error",
                  G_OBJECT_CLASS_TYPE (salut_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_im_channel_marshal_VOID__UINT_UINT_UINT_STRING,
                  G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  signals[SENT] =
    g_signal_new ("sent",
                  G_OBJECT_CLASS_TYPE (salut_im_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  salut_im_channel_marshal_VOID__UINT_UINT_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING);

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (salut_im_channel_class), &dbus_glib_salut_im_channel_object_info);
}

void
salut_im_channel_dispose (GObject *object)
{
  SalutIMChannel *self = SALUT_IM_CHANNEL (object);
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_im_channel_parent_class)->dispose)
    G_OBJECT_CLASS (salut_im_channel_parent_class)->dispose (object);
}

void
salut_im_channel_finalize (GObject *object)
{
  SalutIMChannel *self = SALUT_IM_CHANNEL (object);
  SalutIMChannelPrivate *priv = SALUT_IM_CHANNEL_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (salut_im_channel_parent_class)->finalize (object);
}



/**
 * salut_im_channel_acknowledge_pending_messages
 *
 * Implements D-Bus method AcknowledgePendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_im_channel_acknowledge_pending_messages (SalutIMChannel *self,
                                               const GArray *ids,
                                               GError **error)
{
  return TRUE;
}


/**
 * salut_im_channel_close
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_im_channel_close (SalutIMChannel *self,
                        GError **error)
{
  return TRUE;
}


/**
 * salut_im_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_im_channel_get_channel_type (SalutIMChannel *self,
                                   gchar **ret,
                                   GError **error)
{
  return TRUE;
}


/**
 * salut_im_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_im_channel_get_handle (SalutIMChannel *self,
                             guint *ret,
                             guint *ret1,
                             GError **error)
{
  return TRUE;
}


/**
 * salut_im_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_im_channel_get_interfaces (SalutIMChannel *self,
                                 gchar ***ret,
                                 GError **error)
{
  return TRUE;
}


/**
 * salut_im_channel_get_message_types
 *
 * Implements D-Bus method GetMessageTypes
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_im_channel_get_message_types (SalutIMChannel *self,
                                    GArray **ret,
                                    GError **error)
{
  return TRUE;
}


/**
 * salut_im_channel_list_pending_messages
 *
 * Implements D-Bus method ListPendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_im_channel_list_pending_messages (SalutIMChannel *self,
                                        gboolean clear,
                                        GPtrArray **ret,
                                        GError **error)
{
  return TRUE;
}


/**
 * salut_im_channel_send
 *
 * Implements D-Bus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occurred, D-Bus will throw the error only if this
 *         function returns FALSE.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
gboolean
salut_im_channel_send (SalutIMChannel *self,
                       guint type,
                       const gchar *text,
                       GError **error)
{
  return TRUE;
}

