/*
 * salut-xmpp-writer.h - Header for SalutXmppWriter
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

#ifndef __SALUT_XMPP_WRITER_H__
#define __SALUT_XMPP_WRITER_H__

#include <glib-object.h>

#include "salut-xmpp-stanza.h"

G_BEGIN_DECLS

typedef struct _SalutXmppWriter SalutXmppWriter;
typedef struct _SalutXmppWriterClass SalutXmppWriterClass;

struct _SalutXmppWriterClass {
    GObjectClass parent_class;
};

struct _SalutXmppWriter {
    GObject parent;
};

GType salut_xmpp_writer_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_XMPP_WRITER \
  (salut_xmpp_writer_get_type())
#define SALUT_XMPP_WRITER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_XMPP_WRITER, SalutXmppWriter))
#define SALUT_XMPP_WRITER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_XMPP_WRITER, SalutXmppWriterClass))
#define SALUT_IS_XMPP_WRITER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_XMPP_WRITER))
#define SALUT_IS_XMPP_WRITER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_XMPP_WRITER))
#define SALUT_XMPP_WRITER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_XMPP_WRITER, SalutXmppWriterClass))


SalutXmppWriter *salut_xmpp_writer_new(void);

void salut_xmpp_writer_stream_open(SalutXmppWriter *writer, 
                                   const gchar *to, const gchar *from, 
                                   const guint8 **data, gsize *length);
void salut_xmpp_writer_stream_close(SalutXmppWriter *writer,
                                   const guint8 **data, gsize *length);

gboolean salut_xmpp_writer_write_stanza(SalutXmppWriter *writer, 
                                        SalutXmppStanza *stanza,
                                        const guint8 **data, gsize *length,
                                        GError **error);

void salut_xmpp_writer_flush(SalutXmppWriter *writer);

G_END_DECLS

#endif /* #ifndef __SALUT_XMPP_WRITER_H__*/
