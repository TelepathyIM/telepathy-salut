/*
 * gibber-util.c - Code for Gibber utility functions
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "gibber-util.h"

void
normalize_address (struct sockaddr_storage *addr)
{
  struct sockaddr_in *s4 = (struct sockaddr_in *) addr;
  struct sockaddr_in6 *s6 = (struct sockaddr_in6 *) addr;

  if (s6->sin6_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED (&(s6->sin6_addr)))
    {
      /* Normalize to ipv4 address */
      u_int32_t addr_big_endian;
      u_int16_t port;

      memcpy (&addr_big_endian, s6->sin6_addr.s6_addr + 12, 4);
      port = s6->sin6_port;

      s4->sin_family = AF_INET;
      s4->sin_addr.s_addr = addr_big_endian;
      s4->sin_port = port;
    }
}