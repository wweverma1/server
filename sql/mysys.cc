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

/*
  Convenience wrappers for mysys functionality
*/


#include "mariadb.h"
#include "sql_priv.h"
#include "sql_class.h"


void Charset_loader_mysys::raise_unknown_collation_error(const char *name,
                                                         CHARSET_INFO *name_cs)
                                                         const
{
  ErrConvString err(name, name_cs);
  my_error(ER_UNKNOWN_COLLATION, MYF(0), err.ptr());
  if (error[0])
    push_warning_printf(current_thd,
                        Sql_condition::WARN_LEVEL_WARN,
                        ER_UNKNOWN_COLLATION, "%s", error);
}

/**
  Get a CHARSET_INFO by a character set name.

  @param name      Collation name
  @param cs_flags  e.g. MY_CS_PRIMARY, MY_CS_BINARY
  @param my_flags  mysys flags (MY_WME, MY_UTF8_IS_UTF8MB3)
  @return
  @retval          NULL on error (e.g. not found)
  @retval          A CHARSET_INFO pointter on success
*/
CHARSET_INFO *
Charset_loader_mysys::get_charset(const char *name, uint cs_flags, myf my_flags)
{
  error[0]= '\0'; // Need to clear in case of the second call
  return my_charset_get_by_name(this, name, cs_flags, my_flags);
}


/**
  Get a CHARSET_INFO by an exact collation by name.

  @param name      Collation name
  @param utf8_flag The utf8 translation flag
  @return
  @retval          NULL on error (e.g. not found)
  @retval          A CHARSET_INFO pointter on success
*/
CHARSET_INFO *
Charset_loader_mysys::get_exact_collation(const char *name, myf utf8_flag)
{
  error[0]= '\0'; // Need to clear in case of the second call
  return my_collation_get_by_name(this, name, utf8_flag);
}


/**
  Get a CHARSET_INFO by by a contextually typed collation name.

  @param name      Collation name
  @param utf8_flag The utf8 translation flag
  @return
  @retval          NULL on error (e.g. not found)
  @retval          A CHARSET_INFO pointer on success
*/
CHARSET_INFO *
Charset_loader_mysys::get_contextually_typed_collation(CHARSET_INFO *cs,
                                                       const char *name)
{
  char tmp[MY_CS_COLLATION_NAME_SIZE];
  my_snprintf(tmp, sizeof(tmp), "%s_%s", cs->cs_name.str, name);
  return get_exact_collation(tmp, MYF(0));
}


CHARSET_INFO *
Charset_loader_mysys::get_contextually_typed_collation(const char *name)
{
  return get_contextually_typed_collation(&my_charset_utf8mb4_general_ci,
                                          name);
}


/*
  Find a CHARSET_INFO by a character set and a
  contextually typed collation name.
  @param cs              - the character set
  @param context_cl_name - the context name, e.g. "uca1400_cs_ci"
  @returns               - a NULL pointer in case of failure, or
                           a CHARSET_INFO pointer on success.
*/
CHARSET_INFO *
Charset_loader_mysys::get_contextually_typed_collation_or_error(
                                                           CHARSET_INFO *cs,
                                                           const char *name)
{
  CHARSET_INFO *cl= get_contextually_typed_collation(cs, name);
  if (!cl)
  {
    my_error(ER_COLLATION_CHARSET_MISMATCH, MYF(0), name, cs->cs_name.str);
    return NULL;
  }
  return cl;
}


/*
  Find a collation with binary comparison rules
*/
CHARSET_INFO *
Charset_loader_mysys::find_bin_collation_or_error(CHARSET_INFO *cs)
{
  /*
    We don't need to handle old_mode=UTF8_IS_UTF8MB3 here,
    This method assumes that "cs" points to a real character set name.
    It can be either "utf8mb3" or "utf8mb4". It cannot be "utf8".
    No thd->get_utf8_flag() flag passed to get_charset_by_csname().
  */
  DBUG_ASSERT(cs->cs_name.length !=4 || memcmp(cs->cs_name.str, "utf8", 4));
  /*
    CREATE TABLE t1 (a CHAR(10) BINARY)
      CHARACTER SET utf8mb4 COLLATE utf8mb4_bin;
    Nothing to do, we have the binary collation already.
  */
  if (cs->state & MY_CS_BINSORT)
    return cs;

  // CREATE TABLE t1 (a CHAR(10) BINARY) CHARACTER SET utf8mb4;/
  error[0]= '\0'; // Need in case of the second execution
  if (!(cs= get_charset(cs->cs_name.str, MY_CS_BINSORT, MYF(0))))
  {
    char tmp[65];
    strxnmov(tmp, sizeof(tmp)-1, cs->cs_name.str, "_bin", NULL);
    raise_unknown_collation_error(tmp, system_charset_info);
  }
  return cs;
}


/*
  Find the default collation in the given character set
*/
CHARSET_INFO *Charset_loader_mysys::find_default_collation(CHARSET_INFO *cs)
{
  // See comments in find_bin_collation_or_error()
  DBUG_ASSERT(cs->cs_name.length !=4 || memcmp(cs->cs_name.str, "utf8", 4));
  /*
    CREATE TABLE t1 (a CHAR(10) COLLATE DEFAULT) CHARACTER SET utf8mb4;
    Nothing to do, we have the default collation already.
  */
  if (cs->state & MY_CS_PRIMARY)
    return cs;
  /*
    CREATE TABLE t1 (a CHAR(10) COLLATE DEFAULT)
      CHARACTER SET utf8mb4 COLLATE utf8mb4_bin;

    Don't need to handle old_mode=UTF8_IS_UTF8MB3 here.
    See comments in find_bin_collation_or_error.
  */
  cs= get_charset(cs->cs_name.str, MY_CS_PRIMARY, MYF(MY_WME));
  /*
    The above should never fail, as we have default collations for
    all character sets.
  */
  DBUG_ASSERT(cs);
  return cs;
}
