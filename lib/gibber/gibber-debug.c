
#include <stdarg.h>

#include <glib.h>

#include "gibber-debug.h"

static DebugFlags flags = 0;
static gboolean initialized = FALSE;

static GDebugKey keys[] = {
  { "transport",         DEBUG_TRANSPORT         },
  { "net",               DEBUG_NET               },
  { "xmpp",              DEBUG_XMPP              },
  { "xmpp-reader",       DEBUG_XMPP_READER       },
  { "xmpp-writer",       DEBUG_XMPP_WRITER       },
  { "sasl",              DEBUG_SASL              },
  { "ssl",               DEBUG_SSL               },
  { "rmulticast",        DEBUG_RMULTICAST        },
  { "rmulticast-sender", DEBUG_RMULTICAST_SENDER },
  { "muc-connection",    DEBUG_MUC_CONNECTION    },
  { "all",               ~0                      },
  { 0, },
};

void gibber_debug_set_flags_from_env ()
{
  guint nkeys;
  const gchar *flags_string;

  for (nkeys = 0; keys[nkeys].value; nkeys++);

  flags_string = g_getenv ("GIBBER_DEBUG");

  if (flags_string)
    gibber_debug_set_flags (g_parse_debug_string (flags_string, keys, nkeys));

  initialized = TRUE;
}

void gibber_debug_set_flags (DebugFlags new_flags)
{
  flags |= new_flags;
  initialized = TRUE;
}

gboolean gibber_debug_flag_is_set (DebugFlags flag)
{
  return flag & flags;
}

void gibber_debug (DebugFlags flag,
                   const gchar *format,
                   ...)
{
  if (G_UNLIKELY(!initialized))
    gibber_debug_set_flags_from_env();
  if (flag & flags)
    {
      va_list args;
      va_start (args, format);
      g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, format, args);
      va_end (args);
    }
}

typedef struct
{
  GString *string;
  gchar *indent;
} PrintStanzaData;

static gboolean
attribute_to_string (const gchar *key,
                     const gchar *value,
                     const gchar *ns,
                     gpointer user_data)
{
  PrintStanzaData *data = user_data;

  g_string_append_c (data->string, ' ');
  if (ns != NULL)
    {
      g_string_append (data->string, ns);
      g_string_append_c (data->string, ':');
    }
  g_string_append_printf (data->string, "%s='%s'", key, value);

  return TRUE;
}

static gboolean
node_to_string (GibberXmppNode *node,
                gpointer user_data)
{
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
    g_string_append_printf (data->string, "%s%s\n", data->indent,
        node->content);
  gibber_xmpp_node_each_child (node, node_to_string, data);
  g_free (data->indent);
  data->indent = old_indent;

  g_string_append_printf (data->string, "%s</%s>", data->indent, node->name);
  if (data->indent[0] != '\0')
    g_string_append_c (data->string, '\n');

  return TRUE;
}

void
gibber_debug_stanza (DebugFlags flag,
                     GibberXmppStanza *stanza,
                     const gchar *format,
                     ...)
{
  if (flag & flags)
    {
      PrintStanzaData *data;
      va_list args;
      gchar *tmp;

      data = g_new0 (PrintStanzaData, 1);
      data->string = g_string_new ("");
      data->indent = "";

      va_start (args, format);
      tmp = g_strdup_vprintf (format, args);
      g_string_append (data->string, tmp);
      va_end (args);
      g_string_append_c (data->string, '\n');
      node_to_string (stanza->node, data);

      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s", data->string->str);

      g_free (tmp);
      g_string_free (data->string, TRUE);
      g_free (data);
    }
}
