
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <telepathy-glib/debug.h>

#include "debug.h"

void
debug_set_log_file_from_env (void)
{
  const gchar *output_file;
  int out;

  output_file = g_getenv ("SALUT_LOGFILE");
  if (output_file == NULL)
    return;

  out = g_open (output_file, O_WRONLY | O_CREAT, 0644);
  if (out == -1)
    {
      g_warning ("Can't open logfile '%s': %s", output_file,
          g_strerror (errno));
      return;
    }

  if (dup2 (out, STDOUT_FILENO) == -1)
    {
      g_warning ("Error when duplicating stdout file descriptor: %s",
          g_strerror (errno));
      return;
    }

  if (dup2 (out, STDERR_FILENO) == -1)
    {
      g_warning ("Error when duplicating stderr file descriptor: %s",
          g_strerror (errno));
      return;
    }
}

#ifdef ENABLE_DEBUG

static DebugFlags flags = 0;

GDebugKey keys[] = {
  { "presence",       DEBUG_PRESENCE },
  { "groups",         DEBUG_GROUPS },
  { "contacts",       DEBUG_CONTACTS },
  { "disco",          DEBUG_DISCO },
  { "properties",     DEBUG_PROPERTIES },
  { "roomlist",       DEBUG_ROOMLIST },
  { "media-channel",  DEBUG_MEDIA },
  { "muc",            DEBUG_MUC },
  { "muc-connection", DEBUG_MUC },
  { "net",            DEBUG_NET },
  { "connection",     DEBUG_CONNECTION },
  { "persist",        DEBUG_PERSIST },
  { "self",           DEBUG_SELF },
  { "all",            ~0 },
  { 0, },
};

void debug_set_flags_from_env ()
{
  guint nkeys;
  const gchar *flags_string;

  for (nkeys = 0; keys[nkeys].value; nkeys++);

  flags_string = g_getenv ("SALUT_DEBUG");

  if (flags_string) {
    tp_debug_set_flags_from_env("SALUT_DEBUG");
    debug_set_flags (g_parse_debug_string (flags_string, keys, nkeys));
  }
}

void debug_set_flags (DebugFlags new_flags)
{
  flags |= new_flags;
}

gboolean debug_flag_is_set (DebugFlags flag)
{
  return flag & flags;
}

void debug (DebugFlags flag,
                   const gchar *format,
                   ...)
{
  if (flag & flags)
    {
      va_list args;
      va_start (args, format);
      g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, format, args);
      va_end (args);
    }
}

typedef struct {
  GString *string;
  gchar *indent;
} PrintStanzaData;

static gboolean
attribute_to_string (const gchar *key,
                     const gchar *value,
                     const gchar *ns,
                     gpointer user_data) {
  PrintStanzaData *data = user_data;

  g_string_append_c (data->string, ' ');
  if (ns != NULL) {
    g_string_append (data->string, ns);
    g_string_append_c (data->string, ':');
  }
  g_string_append_printf (data->string, "%s='%s'", key, value);

  return TRUE;
}

static gboolean
node_to_string (GibberXmppNode *node,
                gpointer user_data) {
  PrintStanzaData *data = user_data;
  gchar *old_indent;
  const gchar *ns;

  g_string_append_printf (data->string, "%s<%s", data->indent, node->name);
  ns = gibber_xmpp_node_get_ns (node);
  if (ns != NULL)
    g_string_append_printf (data->string, " xmlns='%s'", ns);
  gibber_xmpp_node_each_attribute (node, attribute_to_string, data);
  g_string_append_printf (data->string, ">\n");

  old_indent = data->indent;
  data->indent = g_strconcat (data->indent, "  ", NULL);
  if (node->content != NULL)
    g_string_append_printf (data->string, "%s%s\n", data->indent, node->content);
  gibber_xmpp_node_each_child (node, node_to_string, data);
  g_free (data->indent);
  data->indent = old_indent;

  g_string_append_printf (data->string, "%s</%s>", data->indent, node->name);
  if (data->indent[0] != '\0')
    g_string_append_c (data->string, '\n');

  return TRUE;
}

void debug_stanza (DebugFlags flag,
                   GibberXmppStanza *stanza,
                   const gchar *format,
                   ...)
{
  if (flag & flags)
    {
      PrintStanzaData *data;
      va_list args;

      data = g_new0 (PrintStanzaData, 1);
      data->string = g_string_new ("");
      data->indent = "";

      va_start (args, format);
      g_string_append (data->string, g_strdup_vprintf (format, args));
      va_end (args);
      g_string_append_c (data->string, '\n');
      node_to_string (stanza->node, data);

      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s", data->string->str);

      g_string_free (data->string, TRUE);
      g_free (data);
    }
}

#endif /* ENABLE_DEBUG */

