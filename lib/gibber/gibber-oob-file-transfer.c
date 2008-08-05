/*
 * gibber-oob-file-transfer.c - Source for GibberOobFileTransfer
 * Copyright (C) 2007 Marco Barisione <marco@barisione.org>
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

#include <libsoup/soup.h>
#include <libsoup/soup-server.h>
#include <libsoup/soup-server-message.h>

#include "gibber-xmpp-stanza.h"
#include "gibber-oob-file-transfer.h"
#include "gibber-fd-transport.h"
#include "gibber-namespaces.h"

#define DEBUG_FLAG DEBUG_FILE_TRANSFER
#include "gibber-debug.h"


G_DEFINE_TYPE(GibberOobFileTransfer, gibber_oob_file_transfer, GIBBER_TYPE_FILE_TRANSFER)

/* private structure */
struct _GibberOobFileTransferPrivate
{
  /* HTTP server used to send files (only when sending files) */
  SoupServer *server;
  /* object used to send file chunks (only when sending files) */
  SoupMessage *msg;
  /* The unescaped served path passed to libsoup, i.e.
   * "/salut-ft-12/hello world" (only when sending files) */
  gchar *served_name;
  /* The full escaped URL, such as
   * "http://192.168.1.2/salut-ft-12/hello%20world" */
  gchar *url;
  /* Input/output channel */
  GIOChannel *channel;
  /* Current number of transferred bytes */
  guint64 transferred_bytes;
  /* whether the transfer has been cancelled */
  gboolean cancelled;
};

static void
gibber_oob_file_transfer_init (GibberOobFileTransfer *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GIBBER_TYPE_OOB_FILE_TRANSFER, GibberOobFileTransferPrivate);
}

static void gibber_oob_file_transfer_finalize (GObject *object);
static void gibber_oob_file_transfer_offer (GibberFileTransfer *ft);
static void gibber_oob_file_transfer_send (GibberFileTransfer *ft,
    GIOChannel *src);
static void gibber_oob_file_transfer_receive (GibberFileTransfer *ft,
    GIOChannel *dest);
static void gibber_oob_file_transfer_cancel (GibberFileTransfer *ft,
    guint error_code);
static void gibber_oob_file_transfer_received_stanza (GibberFileTransfer *ft,
    GibberXmppStanza *stanza);

static void
gibber_oob_file_transfer_class_init (GibberOobFileTransferClass *gibber_oob_file_transfer_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_oob_file_transfer_class);
  GibberFileTransferClass *ft_class =
    GIBBER_FILE_TRANSFER_CLASS(gibber_oob_file_transfer_class);

  g_type_class_add_private (gibber_oob_file_transfer_class,
      sizeof (GibberOobFileTransferPrivate));

  object_class->finalize = gibber_oob_file_transfer_finalize;

  ft_class->offer = gibber_oob_file_transfer_offer;
  ft_class->send = gibber_oob_file_transfer_send;
  ft_class->receive = gibber_oob_file_transfer_receive;
  ft_class->cancel = gibber_oob_file_transfer_cancel;
  ft_class->received_stanza = gibber_oob_file_transfer_received_stanza;
}

static void
gibber_oob_file_transfer_finalize (GObject *object)
{
  GibberOobFileTransfer *self = GIBBER_OOB_FILE_TRANSFER (object);

  if (self->priv->server != NULL)
    g_object_unref (G_OBJECT (self->priv->server));

  if (self->priv->msg)
    g_object_unref (G_OBJECT (self->priv->msg));

  g_free (self->priv->served_name);
  g_free (self->priv->url);

  G_OBJECT_CLASS (gibber_oob_file_transfer_parent_class)->finalize (object);
}

gboolean
gibber_oob_file_transfer_is_file_offer (GibberXmppStanza *stanza)
{
  GibberStanzaType type;
  GibberStanzaSubType sub_type;
  GibberXmppNode *query;
  GibberXmppNode *url;

  gibber_xmpp_stanza_get_type_info (stanza, &type, &sub_type);
  if (type != GIBBER_STANZA_TYPE_IQ ||
      sub_type != GIBBER_STANZA_SUB_TYPE_SET)
    {
      return FALSE;
    }

  query = gibber_xmpp_node_get_child (stanza->node, "query");
  if (query == NULL)
    return FALSE;

  url = gibber_xmpp_node_get_child (query, "url");
  if (url == NULL || url->content == NULL || strcmp (url->content, "") == 0)
    return FALSE;

  return TRUE;
}

static gchar *
escape_filename (const char *unescaped);
static gchar *
unescape_filename (const char *escaped);

GibberFileTransfer *
gibber_oob_file_transfer_new_from_stanza (GibberXmppStanza *stanza,
                                          GibberXmppConnection *connection)
{
  GibberOobFileTransfer *self;
  GibberXmppNode *query;
  GibberXmppNode *url_node;
  const gchar *self_id;
  const gchar *peer_id;
  const gchar *type;
  const gchar *id;
  const gchar *size;
  gchar *url;
  gchar *filename;

  if (strcmp (stanza->node->name, "iq") != 0)
    return NULL;

  peer_id = gibber_xmpp_node_get_attribute (stanza->node, "from");
  self_id = gibber_xmpp_node_get_attribute (stanza->node, "to");
  if (peer_id == NULL || self_id == NULL)
    return NULL;

  type = gibber_xmpp_node_get_attribute (stanza->node, "type");
  if (type == NULL || strcmp (type, "set") != 0)
    return NULL;

  id = gibber_xmpp_node_get_attribute (stanza->node, "id");
  if (id == NULL)
    return NULL;

  query = gibber_xmpp_node_get_child (stanza->node, "query");
  if (query == NULL)
    return NULL;

  url_node = gibber_xmpp_node_get_child (query, "url");
  if (url_node == NULL || url_node->content == NULL)
    return NULL;

  /* The file name is extracted from the address */
  url = g_strdup (url_node->content);
  g_strstrip (url);
  filename = g_strrstr (url, "/");
  if (filename == NULL)
    {
      g_free (url);
      return NULL;
    }
  filename++; /* move after the last "/" */
  filename = unescape_filename (filename);
 
  self = g_object_new (GIBBER_TYPE_OOB_FILE_TRANSFER,
      "id", id,
      "self-id", self_id,
      "peer-id", peer_id,
      "filename", filename,
      "connection", connection,
      "direction", GIBBER_FILE_TRANSFER_DIRECTION_INCOMING,
      NULL);

  size = gibber_xmpp_node_get_attribute (url_node, "size");
  if (size != NULL)
    GIBBER_FILE_TRANSFER (self)->size = g_ascii_strtoull (size, NULL, 0);

  self->priv->url = url;

  self->priv->transferred_bytes = 0;

  g_free (filename);

  return GIBBER_FILE_TRANSFER (self);
}

static void
transferred_chunk (GibberOobFileTransfer *self,
                guint64 bytes_read)
{
  g_signal_emit_by_name (self, "transferred-chunk", bytes_read);
  self->priv->transferred_bytes += bytes_read;
}

/*
 * Data received from the HTTP server.
 */
static void
http_client_chunk_cb (SoupMessage *msg,
                      gpointer user_data)
{
  GibberOobFileTransfer *self = user_data;

  /* Don't write anything if it's been cancelled */
  if (self->priv->cancelled)
    return;

  /* FIXME make async */
  g_io_channel_write_chars (self->priv->channel, msg->response.body,
      msg->response.length, NULL, NULL);
  transferred_chunk (self, (guint64) msg->response.length);
}

/*
 * Received all the file from the HTTP server.
 */
static void
http_client_finished_chunks_cb (SoupMessage *msg,
                                gpointer user_data)
{
  GibberOobFileTransfer *self = user_data;
  GibberXmppStanza *stanza;
  GError *error = NULL;

  /* disconnect from the "got_chunk" signal */
  g_signal_handlers_disconnect_by_func (msg, http_client_chunk_cb, user_data);

  g_io_channel_unref (self->priv->channel);
  self->priv->channel = NULL;

  /* Is the transfer actually incomplete? */
  if (GIBBER_FILE_TRANSFER (self)->size > self->priv->transferred_bytes)
    {
      DEBUG ("File transfer incomplete (size is %llu and only got %llu)",
          GIBBER_FILE_TRANSFER (self)->size, self->priv->transferred_bytes);
      g_signal_emit_by_name (self, "canceled");
      return;
    }

  DEBUG ("Finished HTTP chunked file transfer");

  if (msg->status_code != 200)
    {
      GError *error = NULL;
      const gchar *reason_phrase;

      if (msg->reason_phrase != NULL)
        reason_phrase = msg->reason_phrase;
      else
        reason_phrase = "Unknown HTTP error";
      g_set_error (&error, GIBBER_FILE_TRANSFER_ERROR,
          GIBBER_FILE_TRANSFER_ERROR_NOT_FOUND, reason_phrase);
      gibber_file_transfer_emit_error (GIBBER_FILE_TRANSFER (self), error);
      return;
    }

  stanza = gibber_xmpp_stanza_new ("iq");
  gibber_xmpp_node_set_attribute (stanza->node, "type", "result");
  gibber_xmpp_node_set_attribute (stanza->node, "from",
      GIBBER_FILE_TRANSFER (self)->self_id);
  gibber_xmpp_node_set_attribute (stanza->node, "to",
      GIBBER_FILE_TRANSFER (self)->peer_id);
  gibber_xmpp_node_set_attribute (stanza->node, "id",
      GIBBER_FILE_TRANSFER (self)->id);

  if (gibber_file_transfer_send_stanza (GIBBER_FILE_TRANSFER (self), stanza,
        &error))
    {
      /* Send one last TransferredBytes signal. This will definitely get
       * through, even if it has been < 1s since the last emission, so that
       * clients will show 100% for sure.
       */
      transferred_chunk (self, 0);
      g_signal_emit_by_name (self, "finished");
    }
  else
    {
      gibber_file_transfer_emit_error (GIBBER_FILE_TRANSFER (self), error);
    }
}

static void
gibber_oob_file_transfer_receive (GibberFileTransfer *ft,
                                  GIOChannel *dest)
{
  GibberOobFileTransfer *self = GIBBER_OOB_FILE_TRANSFER (ft);
  SoupSession *session;
  SoupMessage *msg;

  session = soup_session_async_new ();
  msg = soup_message_new (SOUP_METHOD_GET, self->priv->url);
  if (msg == NULL)
    {
      GError *error = NULL;

      gibber_file_transfer_cancel (ft, 404);
      g_set_error (&error, GIBBER_FILE_TRANSFER_ERROR,
          GIBBER_FILE_TRANSFER_ERROR_NOT_FOUND, "Couldn't get the file");
      gibber_file_transfer_emit_error (GIBBER_FILE_TRANSFER (self), error);

      return;
    }

  self->priv->channel = g_io_channel_ref (dest);

  soup_message_set_flags (msg, SOUP_MESSAGE_OVERWRITE_CHUNKS);
  g_signal_connect (msg, "got_chunk", G_CALLBACK (http_client_chunk_cb), self);
  soup_session_queue_message (session, msg, http_client_finished_chunks_cb,
      self);
}

static GibberXmppStanza *
create_transfer_offer (GibberOobFileTransfer *self,
                       GError **error)
{
  GibberXmppConnection *connection;
  GibberFdTransport *transport;

  /* local host name */
  gchar host_name[50];
  struct sockaddr name_addr;
  socklen_t name_addr_len = sizeof (name_addr);

  GibberXmppStanza *stanza;
  GibberXmppNode *query_node;
  GibberXmppNode *url_node;

  gchar *filename_escaped;
  gchar *url;
  gchar *served_name;

  g_object_get (GIBBER_FILE_TRANSFER (self), "connection", &connection, NULL);
  transport = GIBBER_FD_TRANSPORT (connection->transport);
  if (transport == NULL)
    {
      g_set_error (error, GIBBER_FILE_TRANSFER_ERROR,
          GIBBER_FILE_TRANSFER_ERROR_NOT_CONNECTED, "Null transport");
      return NULL;
    }

  getsockname (transport->fd, &name_addr, &name_addr_len);
  g_object_unref (connection);
  getnameinfo (&name_addr, name_addr_len, host_name, sizeof (host_name), NULL,
      0, NI_NUMERICHOST);

  filename_escaped = escape_filename (GIBBER_FILE_TRANSFER (self)->filename);
  url = g_strdup_printf ("http://%s:%d/%s/%s", host_name,
      soup_server_get_port (self->priv->server),
      GIBBER_FILE_TRANSFER (self)->id, filename_escaped);
  g_free (filename_escaped);
  served_name = g_strdup_printf ("/%s/%s", GIBBER_FILE_TRANSFER (self)->id,
      GIBBER_FILE_TRANSFER (self)->filename);

  stanza = gibber_xmpp_stanza_new ("iq");
  gibber_xmpp_node_set_attribute (stanza->node, "type", "set");
  gibber_xmpp_node_set_attribute (stanza->node, "id",
      GIBBER_FILE_TRANSFER (self)->id);
  gibber_xmpp_node_set_attribute (stanza->node, "from",
      GIBBER_FILE_TRANSFER (self)->self_id);
  gibber_xmpp_node_set_attribute (stanza->node, "to",
      GIBBER_FILE_TRANSFER (self)->peer_id);

  query_node = gibber_xmpp_node_add_child_ns (stanza->node, "query",
      GIBBER_XMPP_NS_OOB);

  url_node = gibber_xmpp_node_add_child_with_content (query_node, "url", url);
  gibber_xmpp_node_set_attribute (url_node, "type", "file");
  /* FIXME 0 could be a valid size */
  if (GIBBER_FILE_TRANSFER (self)->size > 0)
    {
      gchar *size_str = g_strdup_printf ("%" G_GUINT64_FORMAT,
          GIBBER_FILE_TRANSFER (self)->size);
      gibber_xmpp_node_set_attribute (url_node, "size", size_str);
      g_free (size_str);
    }

  self->priv->url = url;
  self->priv->served_name = served_name;

  return stanza;
}

/*
 * Data is available from the channel so we can send it.
 */
static gboolean
input_channel_readable_cb (GIOChannel *source,
                           GIOCondition condition,
                           gpointer user_data)
{
  GibberOobFileTransfer *self = user_data;
  GIOStatus status;
  gchar *buff;
  gsize bytes_read;

#define BUFF_SIZE 4096

  if (condition & G_IO_IN)
    {
      buff = g_malloc (BUFF_SIZE);
      status = g_io_channel_read_chars (source, buff, BUFF_SIZE,
          &bytes_read, NULL);
      switch (status)
        {
        case G_IO_STATUS_NORMAL:
          soup_message_add_chunk (self->priv->msg, SOUP_BUFFER_SYSTEM_OWNED,
              buff, bytes_read);
          soup_message_io_unpause (self->priv->msg);
          DEBUG("Data available, writing a %d bytes chunk", bytes_read);
          transferred_chunk (self, (guint64) bytes_read);
          return FALSE;
        case G_IO_STATUS_AGAIN:
          DEBUG("Data available, try again");
          g_free (buff);
          return TRUE;
        case G_IO_STATUS_EOF:
          DEBUG("EOF received on input");
          break;
        default:
          DEBUG ("Read from the channel failed");
      }
      g_free (buff);
    }

#undef BUFF_SIZE

  DEBUG("Closing HTTP chunked transfer");
  soup_message_add_final_chunk (self->priv->msg);
  soup_message_io_unpause (self->priv->msg);

  g_io_channel_unref (self->priv->channel);
  self->priv->channel = NULL;

  soup_server_remove_handler (self->priv->server, self->priv->served_name);

  return FALSE;
}

static void
http_server_cb (SoupServerContext *context,
                SoupMessage *msg,
                gpointer user_data)
{
  const SoupUri *uri = soup_message_get_uri (msg);
  GibberOobFileTransfer *self = user_data;
  const gchar *accept_encoding;

  if (context->method_id != SOUP_METHOD_ID_GET)
    {
      DEBUG ("A HTTP client tried to use an unsupported method");
      soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);
      return;
    }

  if (strcmp (uri->path, self->priv->served_name) != 0)
    {
      soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
      return;
    }

  DEBUG ("Serving '%s'", uri->path);

  soup_message_set_status (msg, SOUP_STATUS_OK);
  soup_server_message_set_encoding (SOUP_SERVER_MESSAGE (msg),
      SOUP_TRANSFER_CHUNKED);
  self->priv->msg = g_object_ref (msg);

  /* iChat accepts only AppleSingle encoding, i.e. file's contents and
   * attributes are stored in the same stream */
  accept_encoding = soup_message_get_header (msg->request_headers,
      "Accept-Encoding");
  if (accept_encoding != NULL && strcmp (accept_encoding, "AppleSingle") == 0)
    {
      DEBUG ("Using AppleSingle encoding");

      /* FIXME this is not working at the moment */
      /* the header contains a magic number (4 bytes), a version number
       * (4 bytes), a filler (16 bytes, all zeros) and the number of
       * entries (2 bytes) */
      static gchar buff[26] = {0};
      if (buff[1] == 0)
        {
          /* magic number */
          ((gint32*) buff)[0] = htonl (0x51600);
          /* version */
          ((gint32*) buff)[1] = htonl (0x20000);
        }

      soup_message_add_header (msg->response_headers, "Content-encoding",
          "AppleSingle");

      soup_message_add_chunk (self->priv->msg, SOUP_BUFFER_STATIC, buff,
          sizeof (buff));
      soup_message_io_unpause (self->priv->msg);
    }

  g_signal_emit_by_name (self, "remote-accepted");
}

static void
gibber_oob_file_transfer_offer (GibberFileTransfer *ft)
{
  GibberOobFileTransfer *self = GIBBER_OOB_FILE_TRANSFER (ft);
  GibberXmppStanza *stanza;
  GError *error = NULL;

  /* start the server if not running */
  /* FIXME we should have only a single server */
  if (self->priv->server == NULL)
    {
      self->priv->server = soup_server_new (NULL, NULL);
      soup_server_run_async (self->priv->server);
    }

  stanza = create_transfer_offer (self, &error);
  if (stanza == NULL)
    {
      gibber_file_transfer_emit_error (GIBBER_FILE_TRANSFER (self), error);
      return;
    }

  soup_server_add_handler (self->priv->server, self->priv->served_name, NULL,
      http_server_cb, NULL, self);

  if (!gibber_file_transfer_send_stanza (GIBBER_FILE_TRANSFER (self),
        stanza, &error))
    {
      gibber_file_transfer_emit_error (GIBBER_FILE_TRANSFER (self), error);
    }
}

static void
http_server_wrote_chunk_cb (SoupMessage *msg,
                            gpointer user_data)
{
  GibberOobFileTransfer *self = user_data;

  DEBUG("Chunk written, adding a watch to get more input (%s)", self->priv->cancelled ? "cancelled" : "not cancelled");
  if (self->priv->channel && !self->priv->cancelled)
    {
      g_io_add_watch (self->priv->channel, G_IO_IN | G_IO_HUP,
          input_channel_readable_cb, self);
    }
}

static void
gibber_oob_file_transfer_send (GibberFileTransfer *ft,
                               GIOChannel *src)
{
  GibberOobFileTransfer *self = GIBBER_OOB_FILE_TRANSFER (ft);

  DEBUG("Starting HTTP chunked file transfer");
  self->priv->channel = src;
  g_io_channel_ref (src);
  g_signal_connect (self->priv->msg, "wrote-chunk",
      G_CALLBACK (http_server_wrote_chunk_cb), self);
  http_server_wrote_chunk_cb (self->priv->msg, self);
}

static void
gibber_oob_file_transfer_cancel (GibberFileTransfer *ft,
                                 guint error_code)
{
  GibberOobFileTransfer *self = GIBBER_OOB_FILE_TRANSFER (ft);
  GibberXmppStanza *stanza;
  GibberXmppNode *query;
  GibberXmppNode *error_node;
  GibberXmppNode *error_desc;

  if (self->priv->cancelled)
    return;

  stanza = gibber_xmpp_stanza_new ("iq");
  gibber_xmpp_node_set_attribute (stanza->node, "type", "error");
  gibber_xmpp_node_set_attribute (stanza->node, "from", ft->self_id);
  gibber_xmpp_node_set_attribute (stanza->node, "to", ft->peer_id);
  gibber_xmpp_node_set_attribute (stanza->node, "id", ft->id);

  query = gibber_xmpp_node_add_child_ns (stanza->node, "query",
      GIBBER_XMPP_NS_OOB);
  gibber_xmpp_node_add_child_with_content (query, "url", self->priv->url);

  error_node = gibber_xmpp_node_add_child (stanza->node, "error");
  switch (error_code)
    {
      case 404:
        gibber_xmpp_node_set_attribute (error_node, "code", "404");
        gibber_xmpp_node_set_attribute (error_node, "type", "cancel");
        error_desc = gibber_xmpp_node_add_child_ns (error_node, "not-found",
            GIBBER_XMPP_NS_STANZAS);
        break;
      case 406:
        gibber_xmpp_node_set_attribute (error_node, "code", "406");
        gibber_xmpp_node_set_attribute (error_node, "type", "modify");
        error_desc = gibber_xmpp_node_add_child_ns (error_node,
            "not-acceptable", GIBBER_XMPP_NS_STANZAS);
        break;
      default:
        g_assert_not_reached ();
    }

  gibber_file_transfer_send_stanza (ft, stanza, NULL);

  self->priv->cancelled = TRUE;
  g_signal_emit_by_name (self, "canceled");
}

static void
gibber_oob_file_transfer_received_stanza (GibberFileTransfer *ft,
                                          GibberXmppStanza *stanza)
{
  GibberOobFileTransfer *self = GIBBER_OOB_FILE_TRANSFER (ft);
  const gchar *type;
  GibberXmppNode *error_node;

  if (strcmp (stanza->node->name, "iq") != 0)
    return;

  type = gibber_xmpp_node_get_attribute (stanza->node, "type");
  if (type == NULL)
    return;

  if (strcmp (type, "result") == 0)
    {
      g_signal_emit_by_name (self, "finished");
      return;
    }

  error_node = gibber_xmpp_node_get_child (stanza->node, "error");
  if (error_node != NULL)
    {
      GError *error = NULL;
      const gchar *error_code_str;
      guint error_code;
      const gchar *error_descr;

      /* FIXME copy the error handling code from gabble */
      error_code_str = gibber_xmpp_node_get_attribute (error_node, "code");
      if (g_ascii_strtoll (error_code_str, NULL, 10) == 406)
        {
          error_code = GIBBER_FILE_TRANSFER_ERROR_NOT_ACCEPTABLE;
          error_descr = "Remote user stopped the transfer";
        }
      else
        {
          error_code = GIBBER_FILE_TRANSFER_ERROR_NOT_FOUND;
          error_descr = "Remote user is not able to retrieve the file";
        }

      g_set_error (&error, GIBBER_FILE_TRANSFER_ERROR, error_code,
          error_descr);
      gibber_file_transfer_emit_error (GIBBER_FILE_TRANSFER (self), error);
      return;
    }
}


/*
 * Escape/unescape file names according to RFC-2396, copied and modified
 * from glib/gconvert.c.
 *
 * Original copyright:
 *   Copyright Red Hat Inc., 2000
 *   Authors: Havoc Pennington, Owen Taylor
 */

static const gboolean acceptable[96] =
{
  /*     !      "      #      $      %      &      '      (      )      *   */
  FALSE, TRUE,  FALSE, FALSE, TRUE,  FALSE, TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
  /* +   ,      -      .      /      0      1      2      3      4      5   */
  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
  /* 6   7      8      9      :      ;      <      =      >      ?      @   */
  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  FALSE, FALSE, TRUE,  FALSE, FALSE, TRUE,
  /* A   B      C      D      E      F      G      H      I      J      K   */
  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
  /* L   M      N      O      P      Q      R      S      T      U      V   */
  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
  /* W   X      Y      Z      [      \      ]      ^      _      `      a   */
  TRUE,  TRUE,  TRUE,  TRUE,  FALSE, FALSE, FALSE, FALSE, TRUE,  FALSE, TRUE,
  /* b   c      d      e      f      g      h      i      j      k      l   */
  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
  /* m   n      o      p      q      r      s      t      u      v      w   */
  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
  /* x   y      z      {      |      }      ~      DEL */
  TRUE,  TRUE,  TRUE,  FALSE, FALSE, FALSE, TRUE,  FALSE
};

static const gchar hex[16] = "0123456789ABCDEF";

static gchar *
escape_filename (const gchar *unescaped)
{
  const gchar *p;
  gchar *q;
  gchar *result;
  int c;
  gint unacceptable;

#define ACCEPTABLE(a) ((a) >= 32 && (a) < 128 && acceptable[(a) - 32])

  unacceptable = 0;
  for (p = unescaped; *p != '\0'; p++)
    {
      c = (guchar) *p;
      if (!ACCEPTABLE (c))
        unacceptable++;
    }

  result = g_malloc (p - unescaped + unacceptable * 2 + 1);

  for (q = result, p = unescaped; *p != '\0'; p++)
    {
      c = (guchar) *p;

      if (!ACCEPTABLE (c))
        {
          *q++ = '%'; /* means hex coming */
          *q++ = hex[c >> 4];
          *q++ = hex[c & 15];
        }
      else
        {
          *q++ = *p;
        }
    }

#undef ACCEPTABLE

  *q = '\0';

  return result;
}

static int
unescape_character (const char *scanner)
{
  int first_digit;
  int second_digit;

  first_digit = g_ascii_xdigit_value (scanner[0]);
  if (first_digit < 0)
    return -1;

  second_digit = g_ascii_xdigit_value (scanner[1]);
  if (second_digit < 0)
    return -1;

  return (first_digit << 4) | second_digit;
}

static gchar *
unescape_filename (const char *escaped)
{
  int len;
  const gchar *in, *in_end;
  gchar *out, *result;
  int c;

  len = strlen (escaped);

  result = g_malloc (len + 1);

  out = result;
  for (in = escaped, in_end = escaped + len; in < in_end; in++)
    {
      c = *in;

      if (c == '%')
        {
          /* catch partial escape sequences past the end of the substring */
          if (in + 3 > in_end)
            break;

          c = unescape_character (in + 1);
          /* catch bad escape sequences and NUL characters */
          if (c <= 0)
            break;

          in += 2;
        }

      *out++ = c;
    }

  *out = '\0';

  return result;
}

