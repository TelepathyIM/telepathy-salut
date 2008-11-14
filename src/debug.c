
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
  { "discovery",      DEBUG_DISCO },
  { "olpc-activity",      DEBUG_OLPC_ACTIVITY },
  { "ft",             DEBUG_FT },
  { "all",            ~0 },
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

#endif /* ENABLE_DEBUG */
