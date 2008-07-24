
#ifndef __DEBUG_H__
#define __DEBUG_H_

#include "config.h"

#include <glib.h>

#include <gibber/gibber-xmpp-stanza.h>

G_BEGIN_DECLS

void debug_set_log_file_from_env (void);

#ifdef ENABLE_DEBUG

typedef enum
{
  DEBUG_PRESENCE       = 1 << 0,
  DEBUG_GROUPS         = 1 << 1,
  DEBUG_CONTACTS       = 1 << 2,
  DEBUG_DISCO          = 1 << 3,
  DEBUG_PROPERTIES     = 1 << 4,
  DEBUG_ROOMLIST       = 1 << 5,
  DEBUG_MEDIA          = 1 << 6,
  DEBUG_MUC            = 1 << 7,
  DEBUG_MUC_CONNECTION = 1 << 8,
  DEBUG_CONNECTION     = 1 << 9,
  DEBUG_IM             = 1 << 10,
  DEBUG_SI_BYTESTREAM_MGR = 1 << 11,
  DEBUG_NET            = 1 << 12,
  DEBUG_SELF           = 1 << 13,
  DEBUG_TUBES          = 1 << 14,
  DEBUG_XCM            = 1 << 15,
} DebugFlags;

void debug_set_flags_from_env ();
void debug_set_flags (DebugFlags flags);
gboolean debug_flag_is_set (DebugFlags flag);
void debug (DebugFlags flag, const gchar *format, ...)
    G_GNUC_PRINTF (2, 3);

#ifdef DEBUG_FLAG

#define DEBUG(format, ...) \
  debug(DEBUG_FLAG, "%s: " format, G_STRFUNC, ##__VA_ARGS__)

#define DEBUGGING debug_flag_is_set(DEBUG_FLAG)

#endif /* DEBUG_FLAG */

#else /* ENABLE_DEBUG */

#ifdef DEBUG_FLAG

#define DEBUG(format, ...)

#define DEBUGGING 0

#define NODE_DEBUG(n, s)

#endif /* DEBUG_FLAG */

#endif /* ENABLE_DEBUG */

G_END_DECLS

#endif
