/*****************************************************************************\
 *  $Id: ipmiconsole_.h,v 1.7 2010-02-08 22:02:30 chu11 Exp $
 *****************************************************************************
 *  Copyright (C) 2007-2012 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Albert Chu <chu11@llnl.gov>
 *  UCRL-CODE-221226
 *
 *  This file is part of Ipmiconsole, a set of IPMI 2.0 SOL libraries
 *  and utilities.  For details, see http://www.llnl.gov/linux/.
 *
 *  Ipmiconsole is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  Ipmiconsole is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Ipmiconsole.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

/* file is named ipmiconsole_.h to differentiate itself from the
 * library ipmiconsole.h.
 *
 * I am scared of the portability of the #include_next directive, so
 * that's why I'm doing it this way.
 */

/* file is "ipmiconsole_.h", so double underscore */
#ifndef _IPMICONSOLE__H
#define _IPMICONSOLE__H

#include <freeipmi/freeipmi.h>

#include "tool-cmdline-common.h"

enum ipmiconsole_argp_option_keys
  {
    DONT_STEAL_KEY = 160,
    DEACTIVATE_KEY = 161,
    SERIAL_KEEPALIVE_KEY = 162,
    LOCK_MEMORY_KEY = 163,
    ESCAPE_CHAR_KEY = 'e',
    DEBUG_KEY = 164,
    DEBUGFILE_KEY = 165,
    NORAW_KEY = 166,
  };

struct ipmiconsole_arguments
{
  struct common_cmd_args common;
  char escape_char;
  int dont_steal;
  int deactivate;
  int serial_keepalive;
  int lock_memory;
#ifndef NDEBUG
  int debugfile;
  int noraw;
#endif /* NDEBUG */
};

#endif /* _IPMICONSOLE__H */