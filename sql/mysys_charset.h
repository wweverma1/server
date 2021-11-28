#ifndef MYSYS_CHARSET
#define MYSYS_CHARSET

/* Copyright (c) 2021, MariaDB Corporation.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */


#include "my_sys.h"


class Charset_loader_mysys: public MY_CHARSET_LOADER
{
public:
  Charset_loader_mysys()
  {
    my_charset_loader_init_mysys(this);
  }
  void raise_unknown_collation_error(const char *name,
                                     CHARSET_INFO *name_cs) const;
  CHARSET_INFO *get_charset(const char *cs_name, uint cs_flags, myf my_flags);
  CHARSET_INFO *get_exact_collation(const char *name, myf utf8_flag);
  CHARSET_INFO *get_contextually_typed_collation(CHARSET_INFO *cs,
                                                 const char *name);
  CHARSET_INFO *get_contextually_typed_collation(const char *name);
  CHARSET_INFO *get_contextually_typed_collation_or_error(CHARSET_INFO *cs,
                                                          const char *name);
  CHARSET_INFO *find_default_collation(CHARSET_INFO *cs);
  CHARSET_INFO *find_bin_collation_or_error(CHARSET_INFO *cs);
};

#endif // MYSYS_CHARSET

