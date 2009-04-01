/*
 * check-helpers.c - Source for some check helpers
 * Copyright (C) 2007 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __CHECK_HELPERS_H__
#define __CHECK_HELPERS_H__

#include <glib.h>
#include <check.h>

void
check_helpers_init (void);

void
expect_critical (gboolean expected);

gboolean
got_critical (void);

#define fail_unless_critical(expr, ...)                          \
G_STMT_START {                                                    \
  expect_critical (TRUE);                                         \
  expr;                                                           \
  _fail_unless (got_critical (), __FILE__, __LINE__,              \
      "Expected g_critical, got none", ## __VA_ARGS__, NULL);     \
  expect_critical (FALSE);                                        \
} G_STMT_END;

#endif /* #ifndef __CHECK_HELPERS_H__ */
