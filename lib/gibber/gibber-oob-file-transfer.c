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
#include <libsoup/soup-message.h>

#include <wocky/wocky-stanza.h>
#include <wocky/wocky-meta-porter.h>
#include <wocky/wocky-namespaces.h>

#include "gibber-oob-file-transfer.h"
#include "gibber-fd-transport.h"
#include "gibber-util.h"

#define DEBUG_FLAG DEBUG_FILE_TRANSFER
#include "gibber-debug.h"

enum {
  HTTP_STATUS_CODE_OK = 200,
  HTTP_STATUS_CODE_NOT_FOUND = 404,
  HTTP_STATUS_CODE_NOT_ACCEPTABLE = 406
};

G_DEFINE_TYPE(GibberOobFileTransfer, gibber_oob_file_transfer,
    GIBBER_TYPE_FILE_TRANSFER)

/* private structure */
struct _GibberOobFileTransferPrivate
{
  /* HTTP server used to send files (only when sending files) */
  SoupServer *server;
  /* object used to send file chunks (when sending files) or to
   * get the file (when receiving file) */
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
  /* the watch id on the channel */
  guint watch_id;
  /* session used to receive the file */
  SoupSession *session;
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
    WockyStanza *stanza);

static void
gibber_oob_file_transfer_class_init (
    GibberOobFileTransferClass *gibber_oob_file_transfer_class)
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

  if (self->priv->watch_id != 0)
      g_source_remove (self->priv->watch_id);

  if (self->priv->server != NULL)
    {
      soup_server_quit (self->priv->server);
      g_object_unref (G_OBJECT (self->priv->server));
    }

  if (self->priv->session != NULL)
    g_object_unref (self->priv->session);

  if (self->priv->channel != NULL)
    g_io_channel_unref (self->priv->channel);

  g_free (self->priv->served_name);
  g_free (self->priv->url);

  G_OBJECT_CLASS (gibber_oob_file_transfer_parent_class)->finalize (object);
}

gboolean
gibber_oob_file_transfer_is_file_offer (WockyStanza *stanza)
{
  WockyStanzaType type;
  WockyStanzaSubType sub_type;
  WockyNode *query;
  WockyNode *url;
  const gchar *url_content;

  wocky_stanza_get_type_info (stanza, &type, &sub_type);
  if (type != WOCKY_STANZA_TYPE_IQ ||
      sub_type != WOCKY_STANZA_SUB_TYPE_SET)
    {
      return FALSE;
    }

  query = wocky_node_get_child (wocky_stanza_get_top_node (stanza),
      "query");
  if (query == NULL)
    return FALSE;

  url = wocky_node_get_child (query, "url");
  if (url == NULL)
    return FALSE;

  url_content = url->content;
  if (url_content == NULL || strcmp (url_content, "") == 0)
    return FALSE;

  if (url_content[0] == '\n')
    /* iChat prefixes url with '\n' */
    url_content++;

  /* We only support file transfer over HTTP */
  if (!g_str_has_prefix (url_content, "http://"))
    return FALSE;

  return TRUE;
}

GibberFileTransfer *
gibber_oob_file_transfer_new_from_stanza_with_from (
    WockyStanza *stanza,
    WockyPorter *porter,
    WockyContact *contact,
    const gchar *peer_id,
    GError **error)
{
  WockyNode *node = wocky_stanza_get_top_node (stanza);
  GibberOobFileTransfer *self;
  WockyNode *query;
  WockyNode *url_node;
  WockyNode *desc_node;
  const gchar *self_id;
  const gchar *type;
  const gchar *id;
  const gchar *size;
  const gchar *description = NULL;
  const gchar *content_type;
  const gchar *ft_type;
  gchar *url;
  gchar *filename;

  g_return_val_if_fail (WOCKY_IS_PORTER (porter), NULL);

  if (strcmp (node->name, "iq") != 0)
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "Not an IQ: %s", node->name);
      return NULL;
    }

  self_id = wocky_node_get_attribute (node, "to");

  if (peer_id == NULL)
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "No 'from' attribute");
      return NULL;
    }

  if (self_id == NULL)
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "No 'to' attribute");
      return NULL;
    }

  type = wocky_node_get_attribute (node, "type");

  if (type == NULL || strcmp (type, "set") != 0)
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "type != 'set': '%s'", (type == NULL ? "(null)" : type));
      return NULL;
    }

  id = wocky_node_get_attribute (node, "id");

  if (id == NULL)
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "no 'id' attribute");
      return NULL;
    }

  query = wocky_node_get_child (node, "query");

  if (query == NULL)
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "no <query> node");
      return NULL;
    }

  url_node = wocky_node_get_child (query, "url");

  if (url_node == NULL)
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "no <query><url> node");
      return NULL;
    }

  if (url_node->content == NULL)
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "<query><url> node has no content");
      return NULL;
    }

  ft_type = wocky_node_get_attribute (url_node, "type");

  if (ft_type != NULL && gibber_strdiff (ft_type, "file"))
    {
      /* We don't support directory transfer */
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "<url> has a 'type' attribute other than 'file': '%s'", ft_type);
      return NULL;
    }

  /* The file name is extracted from the address */
  url = g_strdup (url_node->content);
  g_strstrip (url);
  filename = g_strrstr (url, "/");
  if (filename == NULL)
    {
      g_set_error (error, WOCKY_XMPP_ERROR, WOCKY_XMPP_ERROR_BAD_REQUEST,
          "<url> has no '/': '%s'", url);
      g_free (url);
      return NULL;
    }
  filename++; /* move after the last "/" */
  filename = g_uri_unescape_string (filename, NULL);

  desc_node = wocky_node_get_child (query, "desc");
  if (desc_node != NULL)
    {
      description = desc_node->content;
    }

  content_type = wocky_node_get_attribute (url_node, "mimeType");
  if (content_type == NULL)
    {
      content_type = "application/octet-stream";
    }

  self = g_object_new (GIBBER_TYPE_OOB_FILE_TRANSFER,
      "id", id,
      "self-id", self_id,
      "peer-id", peer_id,
      "filename", filename,
      "porter", porter,
      "contact", contact,
      "direction", GIBBER_FILE_TRANSFER_DIRECTION_INCOMING,
      "description", description,
      "content-type", content_type,
      NULL);

  size = wocky_node_get_attribute (url_node, "size");
  if (size != NULL)
    gibber_file_transfer_set_size (GIBBER_FILE_TRANSFER (self),
      g_ascii_strtoull (size, NULL, 0));

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
                      SoupBuffer *chunk,
                      gpointer user_data)
{
  GibberOobFileTransfer *self = user_data;

  /* Don't write anything if it's been cancelled */
  if (self->priv->cancelled)
    return;

  /* FIXME make async */
  g_io_channel_write_chars (self->priv->channel, chunk->data,
      chunk->length, NULL, NULL);

  if (msg->status_code != HTTP_STATUS_CODE_OK)
    {
      /* Something did wrong, so it's not file data. Don't fire the
       * transferred-chunk signal. */
      self->priv->transferred_bytes += chunk->length;
      return;
    }

  transferred_chunk (self, (guint64) chunk->length);
}

/*
 * Received all the file from the HTTP server.
 */
static void
http_client_finished_chunks_cb (SoupSession *session,
                                SoupMessage *msg,
                                gpointer user_data)
{
  GibberOobFileTransfer *self = user_data;
  WockyStanza *stanza;
  GError *error = NULL;
  guint64 size;

  /* disconnect from the "got-chunk" signal */
  g_signal_handlers_disconnect_by_func (msg, http_client_chunk_cb, user_data);

  /* message has been unreffed by libsoup */
  self->priv->msg = NULL;

  g_io_channel_unref (self->priv->channel);
  self->priv->channel = NULL;

  size = gibber_file_transfer_get_size (GIBBER_FILE_TRANSFER (self));

  /* Is the transfer actually incomplete? */
  if (size > self->priv->transferred_bytes)
    {
      DEBUG ("File transfer incomplete (size is %"G_GUINT64_FORMAT
             " and only got %"G_GUINT64_FORMAT")",
             size, self->priv->transferred_bytes);
      g_signal_emit_by_name (self, "cancelled");
      return;
    }

  DEBUG ("Finished HTTP chunked file transfer");

  if (msg->status_code != HTTP_STATUS_CODE_OK)
    {
      const gchar *reason_phrase;

      if (msg->reason_phrase != NULL)
        reason_phrase = msg->reason_phrase;
      else
        reason_phrase = "Unknown HTTP error";

      DEBUG ("HTTP error %d: %s", msg->status_code, reason_phrase);
      error = g_error_new_literal (GIBBER_FILE_TRANSFER_ERROR,
        GIBBER_FILE_TRANSFER_ERROR_NOT_FOUND, reason_phrase);
      gibber_file_transfer_emit_error (GIBBER_FILE_TRANSFER (self), error);
      g_error_free (error);
      return;
    }

  stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_RESULT,
      GIBBER_FILE_TRANSFER (self)->self_id,
      GIBBER_FILE_TRANSFER (self)->peer_id,
      WOCKY_NODE_ATTRIBUTE, "id", GIBBER_FILE_TRANSFER (self)->id,
      NULL);

  if (!gibber_file_transfer_send_stanza (GIBBER_FILE_TRANSFER (self), stanza,
        &error))
    {
      DEBUG ("Wasn't able to send IQ result; ignoring: %s", error->message);
      g_error_free (error);
    }

  /* Send one last TransferredBytes signal. This will definitely get
   * through, even if it has been < 1s since the last emission, so that
   * clients will show 100% for sure.
   */
  transferred_chunk (self, 0);
  g_signal_emit_by_name (self, "finished");

  g_object_unref (stanza);
}

static void
gibber_oob_file_transfer_receive (GibberFileTransfer *ft,
                                  GIOChannel *dest)
{
  GibberOobFileTransfer *self = GIBBER_OOB_FILE_TRANSFER (ft);

  self->priv->session = soup_session_async_new ();
  self->priv->msg = soup_message_new (SOUP_METHOD_GET, self->priv->url);
  if (self->priv->msg == NULL)
    {
      GError *error = NULL;

      gibber_file_transfer_cancel (ft, HTTP_STATUS_CODE_NOT_FOUND);
      g_set_error (&error, GIBBER_FILE_TRANSFER_ERROR,
          GIBBER_FILE_TRANSFER_ERROR_NOT_FOUND, "Couldn't get the file");
      gibber_file_transfer_emit_error (GIBBER_FILE_TRANSFER (self), error);

      return;
    }

  self->priv->channel = g_io_channel_ref (dest);

  soup_message_body_set_accumulate (self->priv->msg->response_body, FALSE);
  g_signal_connect (self->priv->msg, "got-chunk",
      G_CALLBACK (http_client_chunk_cb), self);
  soup_session_queue_message (self->priv->session, self->priv->msg,
      http_client_finished_chunks_cb, self);
}

static WockyStanza *
create_transfer_offer (GibberOobFileTransfer *self,
                       GError **error)
{
  WockyMetaPorter *porter;
  WockyContact *contact;
  GSocketConnection *conn;
  GSocketAddress *address;
  GInetAddress *addr;
  GSocketFamily family;

  /* local host name */
  gchar *host_name;
  gchar *host_escaped;

  WockyStanza *stanza;
  WockyNode *node;
  WockyNode *query_node;
  WockyNode *url_node;

  gchar *filename_escaped;
  gchar *url;
  gchar *served_name;

  guint64 size;

  g_object_get (GIBBER_FILE_TRANSFER (self),
      "porter", &porter,
      "contact", &contact,
      NULL);

  conn = wocky_meta_porter_borrow_connection (porter, WOCKY_LL_CONTACT (contact));

  g_object_unref (porter);
  g_object_unref (contact);

  if (conn == NULL)
    {
      g_set_error (error, GIBBER_FILE_TRANSFER_ERROR,
          GIBBER_FILE_TRANSFER_ERROR_NOT_CONNECTED, "Null transport");
      return NULL;
    }

  address = g_socket_connection_get_local_address (conn, NULL);
  address = gibber_normalize_socket_address (address);
  addr = g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (address));
  family = g_socket_address_get_family (address);

  host_name = g_inet_address_to_string (addr);

  g_object_unref (address);

  if (family == G_SOCKET_FAMILY_IPV6)
    {
      /* put brackets around the IP6 */
      host_escaped = g_strdup_printf ("[%s]", host_name);
    }
  else
    {
      /* IPv4: No need to modify the host_name */
      host_escaped = g_strdup (host_name);
    }

  g_free (host_name);

  filename_escaped = g_uri_escape_string (GIBBER_FILE_TRANSFER (self)->filename,
      NULL, FALSE);
  url = g_strdup_printf ("http://%s:%d/%s/%s", host_escaped,
      soup_server_get_port (self->priv->server),
      GIBBER_FILE_TRANSFER (self)->id, filename_escaped);
  g_free (host_escaped);
  g_free (filename_escaped);
  served_name = g_strdup_printf ("/%s/%s", GIBBER_FILE_TRANSFER (self)->id,
      GIBBER_FILE_TRANSFER (self)->filename);

  stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_SET,
      GIBBER_FILE_TRANSFER (self)->self_id,
      GIBBER_FILE_TRANSFER (self)->peer_id,
      WOCKY_NODE_ATTRIBUTE, "id", GIBBER_FILE_TRANSFER (self)->id,
      NULL);
  node = wocky_stanza_get_top_node (stanza);

  query_node = wocky_node_add_child_ns (node, "query",
      WOCKY_XMPP_NS_IQ_OOB);

  url_node = wocky_node_add_child_with_content (query_node, "url", url);
  wocky_node_set_attribute (url_node, "type", "file");
  wocky_node_set_attribute (url_node, "mimeType",
      GIBBER_FILE_TRANSFER (self)->content_type);

  wocky_node_add_child_with_content (query_node, "desc",
      GIBBER_FILE_TRANSFER (self)->description);

  size = gibber_file_transfer_get_size (GIBBER_FILE_TRANSFER (self));

  /* FIXME 0 could be a valid size */
  if (size > 0)
    {
      gchar *size_str = g_strdup_printf ("%" G_GUINT64_FORMAT,
          size);
      wocky_node_set_attribute (url_node, "size", size_str);
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
          soup_message_body_append (self->priv->msg->response_body,
              SOUP_MEMORY_TAKE, buff, bytes_read);
          soup_server_unpause_message (self->priv->server, self->priv->msg);
          DEBUG("Data available, writing a %"G_GSIZE_FORMAT" bytes chunk",
              bytes_read);
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
  soup_message_body_complete (self->priv->msg->response_body);
  soup_server_unpause_message (self->priv->server, self->priv->msg);

  g_io_channel_unref (self->priv->channel);
  self->priv->channel = NULL;

  soup_server_remove_handler (self->priv->server, self->priv->served_name);

  return FALSE;
}

static void
http_server_cb (SoupServer *server,
                SoupMessage *msg,
                const char *path,
                GHashTable *query,
                SoupClientContext *context,
                gpointer user_data)
{
  const SoupURI *uri = soup_message_get_uri (msg);
  GibberOobFileTransfer *self = user_data;
  const gchar *accept_encoding;
  guint64 size;
  gchar *size_str;

  if (msg->method != SOUP_METHOD_GET)
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
  soup_message_headers_set_encoding (msg->response_headers,
    SOUP_ENCODING_CHUNKED);

  soup_message_headers_append (msg->response_headers, "Content-Type",
      GIBBER_FILE_TRANSFER (self)->content_type);

  size = gibber_file_transfer_get_size (GIBBER_FILE_TRANSFER (self));
  size_str = g_strdup_printf ("%" G_GUINT64_FORMAT, size);
  soup_message_headers_append (msg->response_headers, "Content-Length",
      size_str);
  g_free (size_str);

  self->priv->msg = msg;

  /* iChat accepts only AppleSingle encoding, i.e. file's contents and
   * attributes are stored in the same stream */
  accept_encoding = soup_message_headers_get (msg->request_headers,
      "Accept-Encoding");
  if (accept_encoding != NULL && strcmp (accept_encoding, "AppleSingle") == 0)
    {
      guint32 uint32;
      guint16 uint16;
      GByteArray *array;
      gchar *buff;
      guint len;

      DEBUG ("Using AppleSingle encoding");

      array = g_byte_array_sized_new (38);
      /* magic number */
      uint32 = htonl (0x51600);
      g_byte_array_append (array, (guint8 *) &uint32, 4);
      /* version */
      uint32 = htonl (0x20000);
      g_byte_array_append (array, (guint8 *) &uint32, 4);
      /* filler */
      uint32 = 0;
      g_byte_array_append (array, (guint8 *) &uint32, 4);
      g_byte_array_append (array, (guint8 *) &uint32, 4);
      g_byte_array_append (array, (guint8 *) &uint32, 4);
      g_byte_array_append (array, (guint8 *) &uint32, 4);
      /* nb entry */
      uint16 = htons (1);
      g_byte_array_append (array, (guint8 *) &uint16, 2);
       /* data fork */
      uint32 = htonl (1);
      g_byte_array_append (array, (guint8 *) &uint32, 4);
       /* data fork offset is the length of this header */
      uint32 = htonl (38);
      g_byte_array_append (array, (guint8 *) &uint32, 4);
       /* data fork size is the size of the file */
      uint32 = htonl (size);
      g_byte_array_append (array, (guint8 *) &uint32, 4);

      soup_message_headers_append (msg->response_headers, "Content-encoding",
          "AppleSingle");

      /* libsoup will free the date once they are written */
      len = array->len;
      buff = (gchar *) g_byte_array_free (array, FALSE);
      soup_message_body_append (self->priv->msg->response_body,
        SOUP_MEMORY_TAKE, buff, len);

      soup_server_unpause_message (self->priv->server, self->priv->msg);
    }

  g_signal_emit_by_name (self, "remote-accepted");
}

static void
create_and_send_transfer_offer (GibberOobFileTransfer *self)
{
  GError *error = NULL;
  WockyStanza *stanza;

  stanza = create_transfer_offer (self, &error);
  if (stanza == NULL)
    {
      gibber_file_transfer_emit_error (GIBBER_FILE_TRANSFER (self), error);
      return;
    }

  soup_server_add_handler (self->priv->server, self->priv->served_name,
      http_server_cb, self, NULL);

  if (!gibber_file_transfer_send_stanza (GIBBER_FILE_TRANSFER (self),
        stanza, &error))
    {
      gibber_file_transfer_emit_error (GIBBER_FILE_TRANSFER (self), error);
    }

  g_object_unref (stanza);
}

static void
porter_open_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  WockyMetaPorter *porter = WOCKY_META_PORTER (source_object);
  GibberOobFileTransfer *self = GIBBER_OOB_FILE_TRANSFER (user_data);
  GError *error = NULL;

  WockyContact *contact;

  GSocketConnection *conn;
  GSocketAddress *address;
  GSocketFamily family;

  if (!wocky_meta_porter_open_finish (porter, result, &error))
    {
      DEBUG ("Failed to open connection: %s", error->message);
      g_clear_error (&error);

      g_set_error (&error, GIBBER_FILE_TRANSFER_ERROR,
          GIBBER_FILE_TRANSFER_ERROR_NOT_CONNECTED, "Couldn't open connection");
      gibber_file_transfer_emit_error (GIBBER_FILE_TRANSFER (self),
          error);
      g_error_free (error);
      return;
    }

  g_object_get (GIBBER_FILE_TRANSFER (self),
      "contact", &contact,
      NULL);

  /* FIXME we should have only a single server */

  /* FIXME: libsoup can't listen on IPv4 and IPv6 interfaces at the same
   * time. http://bugzilla.gnome.org/show_bug.cgi?id=522519
   * We have to check which IP will be send when creating the stanza. */

  conn = wocky_meta_porter_borrow_connection (porter,
      WOCKY_LL_CONTACT (contact));

  if (conn == NULL)
    {
      g_set_error (&error, GIBBER_FILE_TRANSFER_ERROR,
          GIBBER_FILE_TRANSFER_ERROR_NOT_CONNECTED, "Null transport");
      gibber_file_transfer_emit_error (GIBBER_FILE_TRANSFER (self),
          error);
      g_error_free (error);
      goto out;
    }

  address = g_socket_connection_get_remote_address (conn, NULL);
  family = g_socket_address_get_family (address);
  g_object_unref (address);

  if (family == G_SOCKET_FAMILY_IPV6)
    {
      /* IPv6 server */
      SoupAddress *addr;

      addr = soup_address_new_any (SOUP_ADDRESS_FAMILY_IPV6, 0);
      self->priv->server = soup_server_new (SOUP_SERVER_INTERFACE,
          addr, NULL);

      g_object_unref (addr);
    }
  else
    {
      /* IPv4 server */
      self->priv->server = soup_server_new (NULL, NULL);
    }

  soup_server_run_async (self->priv->server);

  create_and_send_transfer_offer (self);

out:
  /* this was reffed when calling open_async */
  wocky_meta_porter_unhold (porter, contact);
  g_object_unref (contact);
}

static void
gibber_oob_file_transfer_offer (GibberFileTransfer *ft)
{
  GibberOobFileTransfer *self = GIBBER_OOB_FILE_TRANSFER (ft);
  WockyMetaPorter *porter;
  WockyContact *contact;

  if (self->priv->server != NULL)
    {
      create_and_send_transfer_offer (self);
      return;
    }

  /* we need to create the soup server */

  g_object_get (ft,
      "porter", &porter,
      "contact", &contact,
      NULL);

  wocky_meta_porter_open_async (porter, WOCKY_LL_CONTACT (contact),
      NULL, porter_open_cb, ft);

  g_object_unref (contact);
  g_object_unref (porter);
}

static void
http_server_wrote_chunk_cb (SoupMessage *msg,
                            gpointer user_data)
{
  GibberOobFileTransfer *self = user_data;

  DEBUG("Chunk written, adding a watch to get more input (%s)",
      self->priv->cancelled ? "cancelled" : "not cancelled");
  if (self->priv->channel && !self->priv->cancelled)
    {
      self->priv->watch_id = g_io_add_watch (self->priv->channel,
          G_IO_IN | G_IO_HUP, input_channel_readable_cb, self);
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

  /* The transfer only starts because an initial chunk has been sent, so call
   * the callback.*/
  http_server_wrote_chunk_cb (self->priv->msg, self);
}

static void
gibber_oob_file_transfer_cancel (GibberFileTransfer *ft,
                                 guint error_code)
{
  GibberOobFileTransfer *self = GIBBER_OOB_FILE_TRANSFER (ft);
  WockyStanza *stanza;
  WockyNode *node;
  WockyNode *query;
  WockyNode *error_node;
  gchar *code_string;

  if (self->priv->cancelled)
    return;
  self->priv->cancelled = TRUE;

  if (ft->direction == GIBBER_FILE_TRANSFER_DIRECTION_OUTGOING)
    /* The OOB XEP doesn't have protocol to inform the receiver that the
     * sender cancelled the transfer. */
    return;

  stanza = wocky_stanza_build (WOCKY_STANZA_TYPE_IQ,
      WOCKY_STANZA_SUB_TYPE_ERROR,
      GIBBER_FILE_TRANSFER (self)->self_id,
      GIBBER_FILE_TRANSFER (self)->peer_id,
      WOCKY_NODE_ATTRIBUTE, "id", GIBBER_FILE_TRANSFER (self)->id,
      NULL);
  node = wocky_stanza_get_top_node (stanza);

  query = wocky_node_add_child_ns (node, "query",
      WOCKY_XMPP_NS_IQ_OOB);
  wocky_node_add_child_with_content (query, "url", self->priv->url);

  error_node = wocky_node_add_child (node, "error");
  code_string = g_strdup_printf ("%d", error_code);

  switch (error_code)
    {
      case HTTP_STATUS_CODE_NOT_FOUND:
        wocky_node_set_attribute (error_node, "code", code_string);
        wocky_node_set_attribute (error_node, "type", "cancel");
        wocky_node_add_child_ns (error_node,
            "item-not-found", WOCKY_XMPP_NS_STANZAS);
        break;
      case HTTP_STATUS_CODE_NOT_ACCEPTABLE:
        wocky_node_set_attribute (error_node, "code", code_string);
        wocky_node_set_attribute (error_node, "type", "modify");
        wocky_node_add_child_ns (error_node,
            "not-acceptable", WOCKY_XMPP_NS_STANZAS);
        break;
      default:
        g_assert_not_reached ();
    }

  g_free (code_string);

  gibber_file_transfer_send_stanza (ft, stanza, NULL);

  g_object_unref (stanza);
}

static void
gibber_oob_file_transfer_received_stanza (GibberFileTransfer *ft,
                                          WockyStanza *stanza)
{
  GibberOobFileTransfer *self = GIBBER_OOB_FILE_TRANSFER (ft);
  WockyNode *node = wocky_stanza_get_top_node (stanza);
  const gchar *type;
  WockyNode *error_node;

  if (strcmp (node->name, "iq") != 0)
    return;

  type = wocky_node_get_attribute (node, "type");
  if (type == NULL)
    return;

  if (strcmp (type, "result") == 0)
    {
      g_signal_emit_by_name (self, "finished");
      return;
    }

  error_node = wocky_node_get_child (node, "error");
  if (error_node != NULL)
    {
      GError *error = NULL;
      const gchar *error_code_str;

      self->priv->cancelled = TRUE;

      /* FIXME copy the error handling code from gabble */
      error_code_str = wocky_node_get_attribute (error_node, "code");
      if (error_code_str == NULL)
        /* iChat uses the 'type' attribute to transmit the error code */
        error_code_str = wocky_node_get_attribute (error_node, "type");

      if (error_code_str != NULL && g_ascii_strtoll (error_code_str, NULL,
            10) == HTTP_STATUS_CODE_NOT_ACCEPTABLE)
        {
          g_signal_emit_by_name (self, "cancelled");
          return;
        }

      g_set_error (&error, GIBBER_FILE_TRANSFER_ERROR,
          GIBBER_FILE_TRANSFER_ERROR_NOT_FOUND,
          "Remote user is not able to retrieve the file");
      gibber_file_transfer_emit_error (GIBBER_FILE_TRANSFER (self), error);
      g_error_free (error);
      return;
    }
}
