/*
 * gibber-xmpp-writer.h - Header for GibberXmppWriter
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

#ifndef __GIBBER_XMPP_WRITER_H__
#define __GIBBER_XMPP_WRITER_H__

#include <glib-object.h>

#include <wocky/wocky-xmpp-writer.h>

G_BEGIN_DECLS

typedef WockyXmppWriter GibberXmppWriter;
typedef WockyXmppWriterClass GibberXmppWriterClass;

#define GIBBER_TYPE_XMPP_WRITER         (WOCKY_TYPE_XMPP_WRITER)
#define GIBBER_XMPP_WRITER(o)           (WOCKY_XMPP_WRITER (o))
#define GIBBER_XMPP_WRITER_CLASS(c)     (WOCKY_XMPP_WRITER_CLASS (c))
#define GIBBER_IS_XMPP_WRITER(o)        (WOCKY_IS_XMPP_WRITER (o))
#define GIBBER_IS_XMPP_WRITER_CLASS(c)  (WOCKY_IS_XMPP_WRITER_CLASS (c))
#define GIBBER_XMPP_WRITER_GET_CLASS(o) (WOCKY_XMPP_WRITER_GET_CLASS (o))

#define gibber_xmpp_writer_new            wocky_xmpp_writer_new
#define gibber_xmpp_writer_new_no_stream  wocky_xmpp_writer_new_no_stream
#define gibber_xmpp_writer_stream_close   wocky_xmpp_writer_stream_close
#define gibber_xmpp_writer_flush          wocky_xmpp_writer_flush

static inline void
gibber_xmpp_writer_stream_open (GibberXmppWriter *writer,
    const gchar *to, const gchar *from, const gchar *version,
    const guint8 **data, gsize *length)
{
  /* set dummy language and ID */
  wocky_xmpp_writer_stream_open (writer, to, from, version, NULL, NULL, data,
      length);
}


static inline gboolean
gibber_xmpp_writer_write_stanza (GibberXmppWriter *writer,
    WockyStanza *stanza, const guint8 **data, gsize *length,
    GError **error)
{
  /* can't fail */
  wocky_xmpp_writer_write_stanza (writer, stanza, data, length);
  return TRUE;
}

G_END_DECLS

#endif /* #ifndef __GIBBER_XMPP_WRITER_H__*/
