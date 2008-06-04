#include <stdio.h>
#include <unistd.h>
#include <glib.h>

#include <gibber/gibber-xmpp-connection.h>
#include <gibber/gibber-transport.h>
#include "test-transport.h"

#include <check.h>

struct _FileChunker {
  gchar *contents;
  gsize length;
  gsize size;
  gsize offset;
};
typedef struct _FileChunker FileChunker;

void
file_chunker_destroy (FileChunker *fc) {
  g_free (fc->contents);
  g_free (fc);
}


FileChunker *
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

gboolean
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


START_TEST (test_instantiation)
{
  GibberXmppConnection *connection;
  TestTransport *transport;

  transport = test_transport_new (NULL, NULL);
  connection = gibber_xmpp_connection_new (GIBBER_TRANSPORT(transport));

  fail_if (connection == NULL);

  connection = gibber_xmpp_connection_new (NULL);

  fail_if (connection == NULL);
}
END_TEST

void
parse_error_cb (GibberXmppConnection *connection, gpointer user_data)
{
  gboolean *parse_error_found = user_data;
  *parse_error_found = TRUE;
}

START_TEST (test_simple_message)
{
  GibberXmppConnection *connection;
  TestTransport *transport;
  gchar *chunk;
  gsize chunk_length;
  gboolean parse_error_found = FALSE;
  const gchar *srcdir;
  gchar *file;

  srcdir = g_getenv ("srcdir");
  if (srcdir == NULL)
    {
      file = g_strdup ("inputs/simple-message.input");
    }
  else
    {
      file = g_strdup_printf ("%s/inputs/simple-message.input", srcdir);
    }

  FileChunker *fc = file_chunker_new (file, 10);
  fail_if (fc == NULL);

  transport = test_transport_new (NULL, NULL);
  connection = gibber_xmpp_connection_new (GIBBER_TRANSPORT(transport));

  g_signal_connect (connection, "parse-error",
      G_CALLBACK(parse_error_cb), &parse_error_found);

  while (!parse_error_found &&
      file_chunker_get_chunk (fc, &chunk, &chunk_length))
    {
      test_transport_write (transport, (guint8*)chunk, chunk_length);
    }

  fail_if (parse_error_found);

  g_free (file);
  file_chunker_destroy (fc);
} END_TEST

TCase *
make_gibber_xmpp_connection_tcase (void)
{
    TCase *tc = tcase_create ("XMPP Connection");
    tcase_add_test (tc, test_instantiation);
    tcase_add_test (tc, test_simple_message);
    return tc;
}
