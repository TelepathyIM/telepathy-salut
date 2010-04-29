
#ifndef __DEBUG_H__
#define __DEBUG_H__

#include "config.h"

#include <glib.h>

#include <gibber/gibber-xmpp-stanza.h>

#ifdef ENABLE_DEBUG

typedef enum
{
  DEBUG_PRESENCE       = 1 << 0,
  DEBUG_GROUPS         = 1 << 1,
  DEBUG_CAPS           = 1 << 2,
  DEBUG_CONTACTS       = 1 << 3,
  DEBUG_DISCO          = 1 << 4,
  DEBUG_PROPERTIES     = 1 << 5,
  DEBUG_ROOMLIST       = 1 << 6,
  DEBUG_MEDIA          = 1 << 7,
  DEBUG_MUC            = 1 << 8,
  DEBUG_MUC_CONNECTION = 1 << 9,
  DEBUG_CONNECTION     = 1 << 10,
  DEBUG_IM             = 1 << 11,
  DEBUG_SI_BYTESTREAM_MGR     = 1 << 12,
  DEBUG_DIRECT_BYTESTREAM_MGR = 1 << 13,
  DEBUG_NET            = 1 << 14,
  DEBUG_SELF           = 1 << 15,
  DEBUG_TUBES          = 1 << 16,
  DEBUG_XCM            = 1 << 17,
  DEBUG_DISCOVERY      = 1 << 18,
  DEBUG_OLPC_ACTIVITY  = 1 << 19,
  DEBUG_FT             = 1 << 20,
} DebugFlags;

void debug_set_flags_from_env (void);
void debug_set_flags (DebugFlags flags);
gboolean debug_flag_is_set (DebugFlags flag);
void salut_log (GLogLevelFlags level, DebugFlags flag,
    const gchar *format, ...) G_GNUC_PRINTF (3, 4);
void debug_free (void);

#ifdef DEBUG_FLAG

#define ERROR(format, ...) \
  salut_log (G_LOG_LEVEL_ERROR, DEBUG_FLAG, "%s: " format, \
      G_STRFUNC, ##__VA_ARGS__)
#define CRITICAL(format, ...) \
  salut_log (G_LOG_LEVEL_CRITICAL, DEBUG_FLAG, "%s: " format, \
      G_STRFUNC, ##__VA_ARGS__)
#define WARNING(format, ...) \
  salut_log (G_LOG_LEVEL_WARNING, DEBUG_FLAG, "%s: " format, \
      G_STRFUNC, ##__VA_ARGS__)
#define MESSAGE(format, ...) \
  salut_log (G_LOG_LEVEL_MESSAGE, DEBUG_FLAG, "%s: " format, \
      G_STRFUNC, ##__VA_ARGS__)
#define INFO(format, ...) \
  salut_log (G_LOG_LEVEL_INFO, DEBUG_FLAG, "%s: " format, \
      G_STRFUNC, ##__VA_ARGS__)
#define DEBUG(format, ...) \
  salut_log (G_LOG_LEVEL_DEBUG, DEBUG_FLAG, "%s: " format, \
      G_STRFUNC, ##__VA_ARGS__)

#define DEBUGGING debug_flag_is_set(DEBUG_FLAG)

#endif /* DEBUG_FLAG */

#else /* ENABLE_DEBUG */

#ifdef DEBUG_FLAG

#define DEBUG(format, ...) \
  G_STMT_START { } G_STMT_END
#define CRITICAL(format, ...) \
  G_STMT_START { } G_STMT_END
#define WARNING(format, ...) \
  G_STMT_START { } G_STMT_END
#define MESSAGE(format, ...) \
  G_STMT_START { } G_STMT_END
#define INFO(format, ...) \
  G_STMT_START { } G_STMT_END
#define DEBUG(format, ...) \
  G_STMT_START { } G_STMT_END

#define DEBUGGING 0

#define NODE_DEBUG(n, s) \
  G_STMT_START { } G_STMT_END

#endif /* DEBUG_FLAG */

#define debug_free() G_STMT_START { } G_STMT_END

#endif /* ENABLE_DEBUG */

G_END_DECLS

#endif
