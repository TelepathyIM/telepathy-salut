/*
 * gibber-xmpp-writer.c - Source for GibberXmppWriter
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

#include "gibber-xmpp-writer.h"

G_DEFINE_TYPE(GibberXmppWriter, gibber_xmpp_writer, G_TYPE_OBJECT)

#define DEBUG_FLAG DEBUG_XMPP_WRITER
#include "gibber-debug.h"

/* private structure */
typedef struct _GibberXmppWriterPrivate GibberXmppWriterPrivate;

struct _GibberXmppWriterPrivate
{
  gboolean dispose_has_run;
  xmlTextWriterPtr xmlwriter;
  GQuark current_ns;
  GQuark stream_ns;
  gboolean stream_mode;
  xmlBufferPtr buffer;
};

#define GIBBER_XMPP_WRITER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_XMPP_WRITER, GibberXmppWriterPrivate))

static void
gibber_xmpp_writer_init (GibberXmppWriter *obj)
{
  GibberXmppWriterPrivate *priv = GIBBER_XMPP_WRITER_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
  priv->current_ns = 0;
  priv->stream_ns = 0;
  priv->buffer = xmlBufferCreate();
  priv->xmlwriter = xmlNewTextWriterMemory(priv->buffer, 0);
  priv->stream_mode = TRUE;
  xmlTextWriterSetIndent(priv->xmlwriter, 1);
}

static void gibber_xmpp_writer_dispose (GObject *object);
static void gibber_xmpp_writer_finalize (GObject *object);

static void
gibber_xmpp_writer_class_init (GibberXmppWriterClass *gibber_xmpp_writer_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_xmpp_writer_class);

  g_type_class_add_private (gibber_xmpp_writer_class, sizeof (GibberXmppWriterPrivate));

  object_class->dispose = gibber_xmpp_writer_dispose;
  object_class->finalize = gibber_xmpp_writer_finalize;

}

void
gibber_xmpp_writer_dispose (GObject *object)
{
  GibberXmppWriter *self = GIBBER_XMPP_WRITER (object);
  GibberXmppWriterPrivate *priv = GIBBER_XMPP_WRITER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (gibber_xmpp_writer_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_xmpp_writer_parent_class)->dispose (object);
}

void
gibber_xmpp_writer_finalize (GObject *object)
{
  GibberXmppWriter *self = GIBBER_XMPP_WRITER (object);
  GibberXmppWriterPrivate *priv = GIBBER_XMPP_WRITER_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  xmlFreeTextWriter(priv->xmlwriter);
  xmlBufferFree(priv->buffer);

  G_OBJECT_CLASS (gibber_xmpp_writer_parent_class)->finalize (object);
}

GibberXmppWriter *
gibber_xmpp_writer_new(void) {
  return g_object_new(GIBBER_TYPE_XMPP_WRITER, NULL);
}

GibberXmppWriter *
gibber_xmpp_writer_new_no_stream(void) {
  GibberXmppWriter *result =  g_object_new(GIBBER_TYPE_XMPP_WRITER, NULL);
  GibberXmppWriterPrivate *priv = GIBBER_XMPP_WRITER_GET_PRIVATE (result);

  priv->stream_mode = FALSE;

  return result;
}

void 
gibber_xmpp_writer_stream_open(GibberXmppWriter *writer,
                              const gchar *to, const gchar *from,
                              const gchar *version,
                              const guint8 **data, gsize *length) {
  GibberXmppWriterPrivate *priv = GIBBER_XMPP_WRITER_GET_PRIVATE (writer);

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

  if (version != NULL) {
    xmlTextWriterWriteString(priv->xmlwriter, (xmlChar *)"\n  version=\"");
    xmlTextWriterFlush(priv->xmlwriter);
    xmlAttrSerializeTxtContent(priv->buffer, NULL, NULL, (xmlChar *)version);
    xmlTextWriterWriteString(priv->xmlwriter, (xmlChar *)"\"");
  }

  xmlTextWriterWriteString(priv->xmlwriter, (xmlChar *) ">\n");
  xmlTextWriterFlush(priv->xmlwriter);

  *data = (const guint8 *)priv->buffer->content;
  *length  = priv->buffer->use;

  /* Set the magic known namespaces */
  priv->current_ns = g_quark_from_string("jabber:client");
  priv->stream_ns = g_quark_from_string("http://etherx.jabber.org/streams");

  DEBUG("Writing xml: %.*s", *length, *data);
}

void gibber_xmpp_writer_stream_close(GibberXmppWriter *writer,
                                   const guint8 **data, gsize *length) {
  static const guint8 *close = (const guint8 *)"</stream:stream>\n";
  *data = close;
  *length = strlen((gchar *)close);
  DEBUG("Writing xml: %.*s", *length, *data);
}

static void
_xml_write_node(GibberXmppWriter *writer, GibberXmppNode *node);

gboolean
_write_attr(const gchar *key, const gchar *value, const gchar *ns,
            gpointer user_data) {
  GibberXmppWriter *self = GIBBER_XMPP_WRITER(user_data);
  GibberXmppWriterPrivate *priv = GIBBER_XMPP_WRITER_GET_PRIVATE (self);
  GQuark attrns = 0;

  if (ns != NULL) {
    attrns = g_quark_from_string(ns);
  }

  if (attrns == 0 || attrns == priv->current_ns) {
    xmlTextWriterWriteAttribute(priv->xmlwriter, 
                                     (const xmlChar *)key, 
                                     (const xmlChar *)value);
  } else if (attrns == priv->stream_ns) {
    xmlTextWriterWriteAttributeNS(priv->xmlwriter, 
                                     (const xmlChar *)"stream",
                                     (const xmlChar *)key, 
                                     (const xmlChar *)NULL,
                                     (const xmlChar *)value);
  } else {
    xmlTextWriterWriteAttributeNS(priv->xmlwriter, 
                                     (const xmlChar *)key,
                                     (const xmlChar *)key, 
                                     (const xmlChar *)ns,
                                     (const xmlChar *)value);
  }
  return TRUE;
}

gboolean 
_write_child(GibberXmppNode *node, gpointer user_data) {
  _xml_write_node(GIBBER_XMPP_WRITER(user_data), node);
  return TRUE;
}


static void
_xml_write_node(GibberXmppWriter *writer, GibberXmppNode *node) {
  const gchar *l;
  GQuark oldns;
  GibberXmppWriterPrivate *priv = GIBBER_XMPP_WRITER_GET_PRIVATE (writer);

  oldns = priv->current_ns;
  
  if (node->ns == 0 || oldns == node->ns) {
    /* Another element in the current namespace */ 
    xmlTextWriterStartElement(priv->xmlwriter, (const xmlChar*) node->name);
  } else if (node->ns == priv->stream_ns) {
    xmlTextWriterStartElementNS(priv->xmlwriter, 
                                (const xmlChar *) "stream",
                                (const xmlChar *) node->name,
                                NULL);

  } else {
    priv->current_ns = node->ns;
    xmlTextWriterStartElementNS(priv->xmlwriter, 
                                NULL,
                                (const xmlChar *) node->name,
                                (const xmlChar *) gibber_xmpp_node_get_ns(node));
  }

  gibber_xmpp_node_each_attribute(node, _write_attr, writer);

  l = gibber_xmpp_node_get_language(node);
  if (l != NULL) {
    xmlTextWriterWriteAttributeNS(priv->xmlwriter, 
                                  (const xmlChar *)"xml", 
                                  (const xmlChar *)"lang", 
                                  NULL,
                                  (const xmlChar *)l);

  }


  gibber_xmpp_node_each_child(node, _write_child, writer);

  if (node->content) {
    xmlTextWriterWriteString(priv->xmlwriter, (const xmlChar*)node->content);
  }
  xmlTextWriterEndElement(priv->xmlwriter);
  priv->current_ns = oldns;
}


gboolean 
gibber_xmpp_writer_write_stanza(GibberXmppWriter *writer, 
                               GibberXmppStanza *stanza,
                               const guint8 **data, gsize *length,
                               GError **error) {
  GibberXmppWriterPrivate *priv = GIBBER_XMPP_WRITER_GET_PRIVATE (writer);

  xmlBufferEmpty(priv->buffer);

  if (!priv->stream_mode) {
    xmlTextWriterStartDocument(priv->xmlwriter, "1.0", "utf-8", NULL);
  }
  _xml_write_node(writer, stanza->node);
  xmlTextWriterFlush(priv->xmlwriter);

  *data = (const guint8 *)priv->buffer->content;
  *length  = priv->buffer->use;

  DEBUG("Writing xml: %.*s", *length, *data);

  return TRUE;
}

void 
gibber_xmpp_writer_flush(GibberXmppWriter *writer) {
  GibberXmppWriterPrivate *priv = GIBBER_XMPP_WRITER_GET_PRIVATE (writer);

  xmlBufferFree(priv->buffer);
  priv->buffer = xmlBufferCreate();
}
