
#include <stdarg.h>

#include <glib.h>

#include "gibber-debug.h"

static DebugFlags flags = 0;
static gboolean initialized = FALSE;

static GDebugKey keys[] = {
  { "transport",      DEBUG_TRANSPORT },
  { "net",            DEBUG_NET },
  { "xmpp",           DEBUG_XMPP },
  { "xmpp-reader",    DEBUG_XMPP_READER },
  { "xmpp-writer",    DEBUG_XMPP_WRITER },
  { "all",            ~0 },
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
