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

#include <stdio.h>
#include <stdlib.h>

#include "check-helpers.h"

static gboolean expecting_critical = FALSE;
static gboolean received_critical  = FALSE;

void
check_helper_log_critical_func (const gchar *log_damain,
                                GLogLevelFlags log_level,
                                const gchar *message,
                                gpointer user_data)
{

  if (!expecting_critical)
    {
      fail("Unexpected critical message: %s\n", message);
    }

  g_assert (log_level & G_LOG_LEVEL_CRITICAL);

  received_critical = TRUE;
}

gboolean
got_critical (void)
{
  return received_critical;
}

void
expect_critical (gboolean expected)
{
  expecting_critical = expected;
  received_critical = FALSE;
}

void
check_helpers_init (void)
{
  g_log_set_handler (NULL, G_LOG_LEVEL_CRITICAL,
      check_helper_log_critical_func, NULL);
}
