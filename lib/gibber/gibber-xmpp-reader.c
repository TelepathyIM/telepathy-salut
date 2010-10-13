/*
 * gibber-xmpp-reader.c - Source for GibberXmppReader
 * Copyright (C) 2006 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd@luon.net>
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

#include "gibber-xmpp-reader.h"

#include "gibber-signals-marshal.h"

#include "gibber-xmpp-stanza.h"

#define XMPP_STREAM_NAMESPACE "http://etherx.jabber.org/streams"

#define DEBUG_FLAG DEBUG_XMPP_READER
#include "gibber-debug.h"

G_DEFINE_TYPE (GibberXmppReader, gibber_xmpp_reader, WOCKY_TYPE_XMPP_READER)

/* signal enum */
enum {
  RECEIVED_STANZA,
  STREAM_OPENED,
  STREAM_CLOSED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct _GibberXmppReaderPrivate GibberXmppReaderPrivate;

struct _GibberXmppReaderPrivate
{
  gboolean emitted_opened;
  gboolean emitted_closed;
};

#define GIBBER_XMPP_READER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_XMPP_READER, \
   GibberXmppReaderPrivate))

static void
gibber_xmpp_reader_init (GibberXmppReader *obj)
{
  GibberXmppReaderPrivate *priv = GIBBER_XMPP_READER_GET_PRIVATE (obj);

  priv->emitted_opened = FALSE;
  priv->emitted_closed = FALSE;
}

static void
gibber_xmpp_reader_class_init (GibberXmppReaderClass *gibber_xmpp_reader_class)
{
  g_type_class_add_private (gibber_xmpp_reader_class,
      sizeof (GibberXmppReaderPrivate));

  signals[RECEIVED_STANZA] = g_signal_new ("received-stanza",
      G_OBJECT_CLASS_TYPE(gibber_xmpp_reader_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, GIBBER_TYPE_XMPP_STANZA);

  signals[STREAM_OPENED] = g_signal_new ("stream-opened",
      G_OBJECT_CLASS_TYPE(gibber_xmpp_reader_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL,
      _gibber_signals_marshal_VOID__STRING_STRING_STRING,
      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

  signals[STREAM_CLOSED] = g_signal_new ("stream-closed",
      G_OBJECT_CLASS_TYPE(gibber_xmpp_reader_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);
}

GibberXmppReader *
gibber_xmpp_reader_new (void)
{
  return g_object_new (GIBBER_TYPE_XMPP_READER,
      NULL);
}

GibberXmppReader *
gibber_xmpp_reader_new_no_stream (void)
{
  return g_object_new (GIBBER_TYPE_XMPP_READER,
      "streaming-mode", FALSE,
      NULL);
}

gboolean
gibber_xmpp_reader_push (GibberXmppReader *reader, const guint8 *data,
    gsize length, GError **error)
{
  WockyXmppReader *wocky = WOCKY_XMPP_READER (reader);
  GibberXmppReaderPrivate *priv = GIBBER_XMPP_READER_GET_PRIVATE (reader);
  WockyStanza *stanza;
  GError *e = NULL;
  gboolean streaming_mode;

  g_return_val_if_fail (wocky_xmpp_reader_get_state (wocky) !=
      WOCKY_XMPP_READER_STATE_ERROR, FALSE);

  g_object_get (wocky,
      "streaming-mode", &streaming_mode,
      NULL);

  wocky_xmpp_reader_push (wocky, data, length);

  if (wocky_xmpp_reader_get_state (wocky) == WOCKY_XMPP_READER_STATE_OPENED
      && !priv->emitted_opened)
    {
      gchar *from = NULL, *to = NULL, *version = NULL;

      g_object_get (wocky,
          "from", &from,
          "to", &to,
          "version", &version,
          NULL);

      priv->emitted_opened = TRUE;
      g_signal_emit (reader, signals[STREAM_OPENED], 0, to, from, version);
      g_free (to);
      g_free (from);
      g_free (version);
    }

  for (stanza = wocky_xmpp_reader_pop_stanza (wocky);
      stanza != NULL;
      stanza = wocky_xmpp_reader_pop_stanza (wocky))
    {
      g_signal_emit (reader, signals[RECEIVED_STANZA], 0, stanza);
      g_object_unref (stanza);
    }

  if (wocky_xmpp_reader_get_state (wocky) == WOCKY_XMPP_READER_STATE_CLOSED
      && !priv->emitted_closed)
    {
      priv->emitted_closed = TRUE;
      g_signal_emit (reader, signals[STREAM_CLOSED], 0);
    }

  if (!streaming_mode)
    {
      wocky_xmpp_reader_reset (wocky);
    }

  e = wocky_xmpp_reader_get_error (wocky);

  if (e == NULL)
    {
      return TRUE;
    }
  else
    {
      g_propagate_error (error, e);
      return FALSE;
    }
}

void
gibber_xmpp_reader_reset (GibberXmppReader *reader)
{
  wocky_xmpp_reader_reset (WOCKY_XMPP_READER (reader));
}

