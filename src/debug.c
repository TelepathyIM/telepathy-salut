#include "config.h"
#include "debug.h"

#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <telepathy-glib/telepathy-glib.h>

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
  { "im",             DEBUG_IM },
  { "muc",            DEBUG_MUC },
  { "muc-connection", DEBUG_MUC },
  { "net",            DEBUG_NET },
  { "connection",     DEBUG_CONNECTION },
  { "self",           DEBUG_SELF },
  { "tubes",          DEBUG_TUBES },
  { "xmpp-connection-manager",  DEBUG_XCM },
  { "si-bytestream-manager",    DEBUG_SI_BYTESTREAM_MGR },
  { "discovery",      DEBUG_DISCOVERY },
  { "ft",             DEBUG_FT },
  { "plugin",         DEBUG_PLUGIN },
  { 0, },
};

void debug_set_flags_from_env ()
{
  guint nkeys;
  const gchar *flags_string;

  for (nkeys = 0; keys[nkeys].value; nkeys++);

  flags_string = g_getenv ("SALUT_DEBUG");

  tp_debug_set_flags (flags_string);

  if (flags_string) {
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

static GHashTable *flag_to_keys = NULL;

static const gchar *
debug_flag_to_domain (DebugFlags flag)
{
  if (G_UNLIKELY (flag_to_keys == NULL))
    {
      guint i;

      flag_to_keys = g_hash_table_new_full (g_direct_hash, g_direct_equal,
          NULL, g_free);

      for (i = 0; keys[i].value; i++)
        {
          GDebugKey key = (GDebugKey) keys[i];
          gchar *val;

          val = g_strdup_printf ("%s/%s", G_LOG_DOMAIN, key.key);
          g_hash_table_insert (flag_to_keys,
              GUINT_TO_POINTER (key.value), val);
        }
    }

  return g_hash_table_lookup (flag_to_keys, GUINT_TO_POINTER (flag));
}

void
debug_free (void)
{
  if (flag_to_keys == NULL)
    return;

  g_hash_table_unref (flag_to_keys);
  flag_to_keys = NULL;
}

static void
log_to_debug_sender (DebugFlags flag,
    const gchar *message)
{
  TpDebugSender *dbg;
  GTimeVal now;

  dbg = tp_debug_sender_dup ();

  g_get_current_time (&now);

  tp_debug_sender_add_message (dbg, &now, debug_flag_to_domain (flag),
      G_LOG_LEVEL_DEBUG, message);

  g_object_unref (dbg);
}

void debug (DebugFlags flag,
                   const gchar *format,
                   ...)
{
  gchar *message;
  va_list args;

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);

  log_to_debug_sender (flag, message);

  if (flag & flags)
    g_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s", message);

  g_free (message);
}

#endif /* ENABLE_DEBUG */
