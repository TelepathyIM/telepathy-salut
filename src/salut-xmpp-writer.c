/*
 * salut-xmpp-writer.c - Source for SalutXmppWriter
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/xmlwriter.h>

#include "salut-xmpp-writer.h"

G_DEFINE_TYPE(SalutXmppWriter, salut_xmpp_writer, G_TYPE_OBJECT)

/* private structure */
typedef struct _SalutXmppWriterPrivate SalutXmppWriterPrivate;

struct _SalutXmppWriterPrivate
{
  gboolean dispose_has_run;
  xmlTextWriterPtr xmlwriter;
  GQuark current_ns;
  xmlBufferPtr buffer;
};

#define SALUT_XMPP_WRITER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SALUT_TYPE_XMPP_WRITER, SalutXmppWriterPrivate))

static void
salut_xmpp_writer_init (SalutXmppWriter *obj)
{
  SalutXmppWriterPrivate *priv = SALUT_XMPP_WRITER_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  priv->current_ns = g_quark_from_string("jabber:client");
  priv->buffer = xmlBufferCreate();
  priv->xmlwriter = xmlNewTextWriterMemory(priv->buffer, 0);
  xmlTextWriterSetIndent(priv->xmlwriter, 1);
}

static void salut_xmpp_writer_dispose (GObject *object);
static void salut_xmpp_writer_finalize (GObject *object);

static void
salut_xmpp_writer_class_init (SalutXmppWriterClass *salut_xmpp_writer_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (salut_xmpp_writer_class);

  g_type_class_add_private (salut_xmpp_writer_class, sizeof (SalutXmppWriterPrivate));

  object_class->dispose = salut_xmpp_writer_dispose;
  object_class->finalize = salut_xmpp_writer_finalize;

}

void
salut_xmpp_writer_dispose (GObject *object)
{
  SalutXmppWriter *self = SALUT_XMPP_WRITER (object);
  SalutXmppWriterPrivate *priv = SALUT_XMPP_WRITER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (salut_xmpp_writer_parent_class)->dispose)
    G_OBJECT_CLASS (salut_xmpp_writer_parent_class)->dispose (object);
}

void
salut_xmpp_writer_finalize (GObject *object)
{
  SalutXmppWriter *self = SALUT_XMPP_WRITER (object);
  SalutXmppWriterPrivate *priv = SALUT_XMPP_WRITER_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  xmlFreeTextWriter(priv->xmlwriter);
  xmlBufferFree(priv->buffer);

  G_OBJECT_CLASS (salut_xmpp_writer_parent_class)->finalize (object);
}

SalutXmppWriter *
salut_xmpp_writer_new(void) {
  return g_object_new(SALUT_TYPE_XMPP_WRITER, NULL);
}

void 
salut_xmpp_writer_stream_open(SalutXmppWriter *writer,
                              const gchar *to, const gchar *from,
                              const guint8 **data, gsize *length) {
  SalutXmppWriterPrivate *priv = SALUT_XMPP_WRITER_GET_PRIVATE (writer);

  xmlBufferEmpty(priv->buffer);
  xmlTextWriterWriteString(priv->xmlwriter, (xmlChar *) 
                    "<?xml version='1.0' encoding='UTF-8'?>\n"             \
                    "<stream:stream\n"                                      \
                    "  xmlns='jabber:client'\n"                             \
                    "  xmlns:stream='http://etherx.jabber.org/streams'");

  if (to != NULL) {
    xmlTextWriterWriteString(priv->xmlwriter, (xmlChar *)"\n  to=\"");
    xmlTextWriterFlush(priv->xmlwriter);
    xmlAttrSerializeTxtContent(priv->buffer, NULL, NULL, (xmlChar *)to);
    xmlTextWriterWriteString(priv->xmlwriter, (xmlChar *)"\"");
  }

  if (from != NULL) {
    xmlTextWriterWriteString(priv->xmlwriter, (xmlChar *)"\n  from=\"");
    xmlTextWriterFlush(priv->xmlwriter);
    xmlAttrSerializeTxtContent(priv->buffer, NULL, NULL, (xmlChar *)from);
    xmlTextWriterWriteString(priv->xmlwriter, (xmlChar *)"\"");
  }

 /*
  if (version != NULL) {
    xmlTextWriterWriteString(priv->xmlwriter, "\n  version=\"");
    xmlTextWriterFlush(priv->xmlwriter);
    xmlAttrSerializeTxtContent(priv->buffer, NULL, NULL, (xmlChar *)version);
    xmlTextWriterWriteString(priv->xmlwriter, "\"");
  }
  */
  xmlTextWriterWriteString(priv->xmlwriter, (xmlChar *) ">\n");
  xmlTextWriterFlush(priv->xmlwriter);

  *data = (const guint8 *)priv->buffer->content;
  *length  = priv->buffer->use;
}

void salut_xmpp_writer_stream_close(SalutXmppWriter *writer,
                                   const guint8 **data, gsize *length) {
  static const guint8 *close = (const guint8 *)"</stream:stream>\n";
  *data = close;
  *length = strlen((gchar *)close);
}

static void
_xml_write_node(SalutXmppWriter *writer, SalutXmppNode *node);

gboolean
_write_attr(const gchar *key, const gchar *value, const gchar *ns,
            gpointer user_data) {
  SalutXmppWriter *self = SALUT_XMPP_WRITER(user_data);
  SalutXmppWriterPrivate *priv = SALUT_XMPP_WRITER_GET_PRIVATE (self);


  if (ns != NULL && g_quark_from_string(ns) != priv->current_ns) {
    xmlTextWriterWriteAttributeNS(priv->xmlwriter, 
                                     (const xmlChar *)key,
                                     (const xmlChar *)key, 
                                     (const xmlChar *)ns,
                                     (const xmlChar *)value);
  } else {
    xmlTextWriterWriteAttribute(priv->xmlwriter, 
                                     (const xmlChar *)key, 
                                     (const xmlChar *)value);
  }
  return TRUE;
}

gboolean 
_write_child(SalutXmppNode *node, gpointer user_data) {
  _xml_write_node(SALUT_XMPP_WRITER(user_data), node);
  return TRUE;
}


static void
_xml_write_node(SalutXmppWriter *writer, SalutXmppNode *node) {
  const gchar *l;
  GQuark oldns;
  SalutXmppWriterPrivate *priv = SALUT_XMPP_WRITER_GET_PRIVATE (writer);

  oldns = priv->current_ns;
  
  if (node->ns == 0 || oldns == node->ns) {
    xmlTextWriterStartElement(priv->xmlwriter, (const xmlChar*) node->name);
  } else {
    priv->current_ns = node->ns;
    xmlTextWriterStartElementNS(priv->xmlwriter, 
                                NULL,
                                (const xmlChar*) node->name,
                                (const xmlChar *)salut_xmpp_node_get_ns(node));
  }

  salut_xmpp_node_each_attribute(node, _write_attr, writer);

  l = salut_xmpp_node_get_language(node);
  if (l != NULL) {
    xmlTextWriterWriteAttributeNS(priv->xmlwriter, 
                                  (const xmlChar *)"xml", 
                                  (const xmlChar *)"lang", 
                                  NULL,
                                  (const xmlChar *)l);

  }


  salut_xmpp_node_each_child(node, _write_child, writer);

  if (node->content) {
    xmlTextWriterWriteString(priv->xmlwriter, (const xmlChar*)node->content);
  }
  xmlTextWriterEndElement(priv->xmlwriter);
  priv->current_ns = oldns;
}


gboolean 
salut_xmpp_writer_write_stanza(SalutXmppWriter *writer, 
                               SalutXmppStanza *stanza,
                               const guint8 **data, gsize *length,
                               GError **error) {
  SalutXmppWriterPrivate *priv = SALUT_XMPP_WRITER_GET_PRIVATE (writer);

  xmlBufferEmpty(priv->buffer);

  _xml_write_node(writer, stanza->node);
  xmlTextWriterFlush(priv->xmlwriter);

  *data = (const guint8 *)priv->buffer->content;
  *length  = priv->buffer->use;

  return TRUE;
}

void 
salut_xmpp_writer_flush(SalutXmppWriter *writer) {
  SalutXmppWriterPrivate *priv = SALUT_XMPP_WRITER_GET_PRIVATE (writer);

  xmlBufferFree(priv->buffer);
  priv->buffer = xmlBufferCreate();
}
