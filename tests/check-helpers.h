#ifndef __CHECK_HELPERS_H__
#define __CHECK_HELPERS_H__

#include <glib.h>
#include <check.h>

void
check_helpers_init(void);

void
expect_critical(gboolean expected);

gboolean
got_critical(void);

#define fail_unless_critical(expr, ...)                           \
G_STMT_START {                                                    \
  expect_critical (TRUE);                                         \
  expr;                                                           \
  _fail_unless (got_critical (), __FILE__, __LINE__,              \
      "Expected g_critical, got none", ## __VA_ARGS__, NULL);     \
  expect_critical (FALSE);                                        \
} G_STMT_END;

#endif /* #ifndef __CHECK_HELPERS_H__ */
