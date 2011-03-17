#include <stdio.h>
#include <unistd.h>
#include <glib.h>

#include <gibber/gibber-xmpp-connection.h>
#include <gibber/gibber-transport.h>
#include "test-transport.h"

struct _FileChunker {
  gchar *contents;
  gsize length;
  gsize size;
  gsize offset;
};
typedef struct _FileChunker FileChunker;

static void
file_chunker_destroy (FileChunker *fc) {
  g_free (fc->contents);
  g_free (fc);
}


static FileChunker *
file_chunker_new (const gchar *filename, gsize chunk_size) {
  FileChunker *fc;
  fc = g_new0 (FileChunker, 1);

  fc->size = chunk_size;
  if (!g_file_get_contents (filename, &fc->contents, &fc->length, NULL)) {
    file_chunker_destroy (fc);
    return NULL;
  }
  return fc;
}

static gboolean
file_chunker_get_chunk (FileChunker *fc,
                        gchar **chunk,
                        gsize *chunk_size) {
  if (fc->offset < fc->length) {
    *chunk_size = MIN (fc->length - fc->offset, fc->size);
    *chunk = fc->contents + fc->offset;
    fc->offset += *chunk_size;
    return TRUE;
  }
  return FALSE;
}


static void
test_instantiation (void)
{
  GibberXmppConnection *connection;
  TestTransport *transport;

  transport = test_transport_new (NULL, NULL);
  connection = gibber_xmpp_connection_new (GIBBER_TRANSPORT(transport));

  g_assert (connection != NULL);

  connection = gibber_xmpp_connection_new (NULL);

  g_assert (connection != NULL);
}

static void
parse_error_cb (GibberXmppConnection *connection, gpointer user_data)
{
  gboolean *parse_error_found = user_data;
  *parse_error_found = TRUE;
}

static void
test_simple_message (void)
{
  GibberXmppConnection *connection;
  TestTransport *transport;
  gchar *chunk;
  gsize chunk_length;
  gboolean parse_error_found = FALSE;
  const gchar *srcdir;
  gchar *file;
  FileChunker *fc;

  srcdir = g_getenv ("srcdir");
  if (srcdir == NULL)
    {
      file = g_strdup ("inputs/simple-message.input");
    }
  else
    {
      file = g_strdup_printf ("%s/inputs/simple-message.input", srcdir);
    }

  fc = file_chunker_new (file, 10);
  g_assert (fc != NULL);

  transport = test_transport_new (NULL, NULL);
  connection = gibber_xmpp_connection_new (GIBBER_TRANSPORT(transport));

  g_signal_connect (connection, "parse-error",
      G_CALLBACK(parse_error_cb), &parse_error_found);

  while (!parse_error_found &&
      file_chunker_get_chunk (fc, &chunk, &chunk_length))
    {
      test_transport_write (transport, (guint8 *) chunk, chunk_length);
    }

  g_assert (!parse_error_found);

  g_free (file);
  file_chunker_destroy (fc);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_type_init ();

  /* Kill tests in 20 seconds */
  alarm (20);

  g_test_add_func ("/gibber/xmpp-connection/instantiation",
      test_instantiation);
  g_test_add_func ("/gibber/xmpp-connection/simple-message",
      test_simple_message);

  return g_test_run ();
}

#include "test-transport.c"
