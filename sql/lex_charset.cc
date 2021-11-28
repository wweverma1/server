/* Copyright (c) 2021, 2022, MariaDB Corporation.

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


#include "my_global.h"
#include "my_sys.h"
#include "m_ctype.h"
#include "lex_string.h"
#include "lex_charset.h"
#include "mysqld_error.h"

LEX_CSTRING Lex_charset_collation_st::collation_name_for_show() const
{
  switch (m_type) {
  case TYPE_EMPTY:
  case TYPE_CHARACTER_SET:
    DBUG_ASSERT(0);
    break;
  case TYPE_COLLATE_EXACT:
    break;
  case TYPE_COLLATE_CONTEXTUALLY_TYPED:
    if (is_contextually_typed_collate_default())
      return Lex_cstring(STRING_WITH_LEN("DEFAULT"));
    if (is_contextually_typed_binary_style())
      return Lex_cstring(STRING_WITH_LEN("BINARY"));
    return collation_name_context_suffix();
  }
  return m_ci->coll_name;
}


void Lex_charset_collation_st::
      error_conflicting_collations_or_styles(const Lex_charset_collation_st &a,
                                             const Lex_charset_collation_st &b)
{
  const char *ca= a.is_contextually_typed_binary_style() ? "" : "COLLATE ";
  const char *cb= b.is_contextually_typed_binary_style() ? "" : "COLLATE ";
  my_error(ER_CONFLICTING_DECLARATIONS, MYF(0),
           ca, a.collation_name_for_show().str,
           cb, b.collation_name_for_show().str);
}


bool Lex_charset_collation_st::set_charset_collate_exact(CHARSET_INFO *cs,
                                                         CHARSET_INFO *cl)
{
  DBUG_ASSERT(cs != nullptr && cl != nullptr);
  if (!my_charset_same(cl, cs))
  {
    my_error(ER_COLLATION_CHARSET_MISMATCH, MYF(0),
             cl->coll_name.str, cs->cs_name.str);
    return true;
  }
  set_collate_exact(cl);
  return false;
}


/*
  Resolve an empty or a contextually typed collation according to the
  upper level default character set (and optionally a collation), e.g.:
    CREATE TABLE t1 (a CHAR(10)) CHARACTER SET latin1;
    CREATE TABLE t1 (a CHAR(10) BINARY) CHARACTER SET latin1;
    CREATE TABLE t1 (a CHAR(10) COLLATE DEFAULT)
      CHARACTER SET latin1 COLLATE latin1_bin;

  "this" is the COLLATE clause                  (e.g. of a column)
  "def" is the upper level CHARACTER SET clause (e.g. of a table)
*/
CHARSET_INFO *
Lex_charset_collation_st::resolved_to_character_set(CHARSET_INFO *def) const
{
  DBUG_ASSERT(def);

  switch (m_type) {
  case TYPE_EMPTY:
    return def;
  case TYPE_CHARACTER_SET:
    DBUG_ASSERT(m_ci);
    return m_ci;
  case TYPE_COLLATE_EXACT:
    DBUG_ASSERT(m_ci);
    return m_ci;
  case TYPE_COLLATE_CONTEXTUALLY_TYPED:
    break;
  }

  // Contextually typed
  DBUG_ASSERT(m_ci);

  Charset_loader_mysys loader;
  if (is_contextually_typed_binary_style())    // CHAR(10) BINARY
    return loader.find_bin_collation_or_error(def);

  if (is_contextually_typed_collate_default()) // CHAR(10) COLLATE DEFAULT
    return loader.find_default_collation(def);

  const LEX_CSTRING context_name= collation_name_context_suffix();
  if (!strncasecmp(context_name.str, STRING_WITH_LEN("uca1400_")))
    return loader.get_contextually_typed_collation_or_error(def,
                                                            context_name.str);

  /*
    Non-binary, non-default, non-uca1400 contextually typed collation.
    We don't have such yet - the parser cannot produce this.
  */
  DBUG_ASSERT(0);
  return NULL;
}


/*
  Merge the CHARACTER SET clause to:
  - an empty COLLATE clause
  - an explicitly typed collation name
  - a contextually typed collation

  "this" corresponds to `CHARACTER SET xxx [BINARY]`
  "cl" corresponds to the COLLATE clause
*/
bool
Lex_charset_collation_st::
  merge_charset_clause_and_collate_clause(const Lex_charset_collation_st &cl)
{
  if (cl.is_empty()) // No COLLATE clause
    return false;

  switch (m_type) {
  case TYPE_EMPTY:
    /*
      No CHARACTER SET clause
      CHAR(10) NOT NULL COLLATE latin1_bin
      CHAR(10) NOT NULL COLLATE DEFAULT
    */
    *this= cl;
    return false;
  case TYPE_CHARACTER_SET:
  case TYPE_COLLATE_EXACT:
    {
      Lex_explicit_charset_opt_collate ecs(m_ci, m_type == TYPE_COLLATE_EXACT);
      if (ecs.merge_collate_or_error(cl))
        return true;
      set_collate_exact(ecs.charset_and_collation());
      return false;
    }
  case TYPE_COLLATE_CONTEXTUALLY_TYPED:
    break;
  }

  if (is_contextually_typed_collation())
  {
    if (cl.is_contextually_typed_collation())
    {
      /*
        CONTEXT + CONTEXT:
        CHAR(10) BINARY .. COLLATE DEFAULT - not supported by the parser
        CHAR(10) BINARY .. COLLATE uca1400_as_ci
        CREATE DATABASE db1 COLLATE uca1400_as_ci COLLATE uca1400_as_ci;
      */
      if (m_ci != cl.m_ci)
      {
        error_conflicting_collations_or_styles(*this, cl);
        return true;
      }
      return false;
    }

    /*
      CONTEXT + EXPLICIT
      CHAR(10) COLLATE DEFAULT       .. COLLATE latin1_swedish_ci
      CHAR(10) BINARY                .. COLLATE latin1_bin
      CHAR(10) COLLATE uca1400_as_ci .. COLLATE latin1_bin
    */
    if (is_contextually_typed_collate_default() &&
        !cl.charset_collation()->default_flag())
    {
      error_conflicting_collations_or_styles(*this, cl);
      return true;
    }

    if (is_contextually_typed_binary_style() &&
        !cl.charset_collation()->binsort_flag())
    {
      error_conflicting_collations_or_styles(*this, cl);
      return true;
    }
    *this= cl;
    return false;
  }

  DBUG_ASSERT(0);
  return false;
}


bool
Lex_explicit_charset_opt_collate::
  merge_collate_or_error(const Lex_charset_collation_st &cl)
{
  DBUG_ASSERT(cl.type() != Lex_charset_collation_st::TYPE_CHARACTER_SET);

  switch (cl.type()) {
  case Lex_charset_collation_st::TYPE_EMPTY:
    return false;
  case Lex_charset_collation_st::TYPE_CHARACTER_SET:
    DBUG_ASSERT(0);
    return false;
  case Lex_charset_collation_st::TYPE_COLLATE_EXACT:
    /*
      EXPLICIT + EXPLICIT
      CHAR(10) CHARACTER SET latin1                    .. COLLATE latin1_bin
      CHAR(10) CHARACTER SET latin1 COLLATE latin1_bin .. COLLATE latin1_bin
      CHAR(10) COLLATE latin1_bin                      .. COLLATE latin1_bin
      CHAR(10) COLLATE latin1_bin                      .. COLLATE latin1_bin
      CHAR(10) CHARACTER SET latin1 BINARY             .. COLLATE latin1_bin
    */
    if (m_with_collate && m_ci != cl.charset_collation())
    {
      my_error(ER_CONFLICTING_DECLARATIONS, MYF(0),
               "COLLATE ", m_ci->coll_name.str,
               "COLLATE ", cl.charset_collation()->coll_name.str);
      return true;
    }
    if (!my_charset_same(m_ci, cl.charset_collation()))
    {
      my_error(ER_COLLATION_CHARSET_MISMATCH, MYF(0),
               cl.charset_collation()->coll_name.str, m_ci->cs_name.str);
      return true;
    }
    m_ci= cl.charset_collation();
    m_with_collate= true;
    return false;

  case Lex_charset_collation_st::TYPE_COLLATE_CONTEXTUALLY_TYPED:
    if (cl.is_contextually_typed_collate_default())
    {
      /*
        SET NAMES latin1 COLLATE DEFAULT;
        ALTER TABLE t1 CONVERT TO CHARACTER SET latin1 COLLATE DEFAULT;
      */
      CHARSET_INFO *tmp= Charset_loader_mysys().find_default_collation(m_ci);
      if (!tmp)
        return true;
      m_ci= tmp;
      m_with_collate= true;
      return false;
    }
    else
    {
      /*
        EXPLICIT + CONTEXT
        CHAR(10) COLLATE latin1_bin .. COLLATE DEFAULT not possible yet
        CHAR(10) COLLATE latin1_bin .. COLLATE uca1400_as_ci
      */

      const LEX_CSTRING context_cl_name= cl.collation_name_context_suffix();
      if (!strncasecmp(context_cl_name.str, STRING_WITH_LEN("uca1400_")))
      {
        CHARSET_INFO *tmp;
        Charset_loader_mysys loader;
        if (!(tmp= loader.get_contextually_typed_collation_or_error(m_ci,
                                                          context_cl_name.str)))
          return true;
        m_with_collate= true;
        m_ci= tmp;
        return false;
      }

      DBUG_ASSERT(0); // Not possible yet
      return false;
    }
  }
  DBUG_ASSERT(0);
  return false;
}


/*
  This method is used in the "attribute_list" rule to merge two indepentent
  COLLATE clauses (not belonging to a CHARACTER SET clause).
*/
bool
Lex_charset_collation_st::
  merge_collate_clause_and_collate_clause(const Lex_charset_collation_st &cl)
{
  /*
    "BINARY" and "COLLATE DEFAULT" are not possible
    in an independent COLLATE clause in a column attribute.
  */
  DBUG_ASSERT(!is_contextually_typed_collation());
  DBUG_ASSERT(!cl.is_contextually_typed_collation());

  if (cl.is_empty())
    return false;

  switch (m_type) {
  case TYPE_EMPTY:
    *this= cl;
    return false;
  case TYPE_CHARACTER_SET:
    DBUG_ASSERT(0);
    return false;
  case TYPE_COLLATE_EXACT:
  case TYPE_COLLATE_CONTEXTUALLY_TYPED:
    break;
  }

  /*
    Two independent explicit or context collations:
      CHAR(10) NOT NULL COLLATE latin1_bin DEFAULT 'a' COLLATE latin1_bin
    Note, we should perhaps eventually disallow double COLLATE clauses.
    But for now let's just disallow only conflicting ones.
  */

  if (type() == cl.type())
  {
    /*
      CONTEXT + CONTEXT
        CHAR(10) NOT NULL COLLATE uca1400_as_ci
               DEFAULT '' COLLATE uca1400_as_ci

      EXPLICIT + EXPLICIT
        CHAR(10) NOT NULL COLLATE utf8mb4_bin
               DEFAULT '' COLLATE utf8mb4_bin
    */
    if (charset_collation() != cl.charset_collation())
    {
      my_error(ER_CONFLICTING_DECLARATIONS, MYF(0),
               "COLLATE ", collation_name_for_show().str,
               "COLLATE ", cl.collation_name_for_show().str);
      return true;
    }
    return false;
  }

  /*
    CONTEXT + EXPLICIT
      CHAR(10) NOT NULL   COLLATE uca1400_as_ci
               DEFAULT '' COLLATE utf8mb4_uca140_as_ci;

    EXPLICIT + CONTEXT
      CHAR(10) NOT NULL   COLLATE utf8mb4_uca1400_as_ci
               DEFAULT '' COLLATE uca140_as_ci;
  */
  const LEX_CSTRING a= collation_name_context_suffix();
  const LEX_CSTRING b= cl.collation_name_context_suffix();

  if (!lex_string_eq(&a, &b))
  {
    my_error(ER_CONFLICTING_DECLARATIONS, MYF(0),
             "COLLATE ", collation_name_for_show().str,
             "COLLATE ", cl.collation_name_for_show().str);
    return true;
  }
  if (is_contextually_typed_collation())
    *this= cl;
  return false;
}


/*
  Mix an unordered combination of CHARACTER SET and COLLATE clauses
  (i.e. COLLATE can come before CHARACTER SET).
  Merge a CHARACTER SET clause.
  @param cs         - The "CHARACTER SET exact_charset_name".
  @param first_time - true if there no CHARACTER SET DEFAULT earlier.
                      false if there was a CHARACTER SET DEFAULT already.
*/
bool
Lex_charset_collation_st::merge_unordered_charset_exact(CHARSET_INFO *cs)
{
  DBUG_ASSERT(cs);

  switch (m_type) {
  case TYPE_EMPTY:
    // CHARACTER SET cs
    set_charset(cs);
    return false;

  case TYPE_CHARACTER_SET:
    // CHARACTER SET cs1 .. CHARACTER SET cs2
    if (charset_collation() == cs)
      return false;
    my_error(ER_CONFLICTING_DECLARATIONS, MYF(0),
             "CHARACTER SET ", charset_collation()->cs_name.str,
             "CHARACTER SET ", cs->cs_name.str);
    return true;

  case TYPE_COLLATE_EXACT:
  case TYPE_COLLATE_CONTEXTUALLY_TYPED:
    {
      // COLLATE cl1 .. CHARACTER SET cs
      Lex_explicit_charset_opt_collate tmp(cs, false);
      if (tmp.merge_collate_or_error(*this))
        return false;
      *this= Lex_charset_collation(tmp);
      return false;
    }
  }
  DBUG_ASSERT(0);
  return false;
}


/*
  Mix an unordered combination of CHARACTER SET and COLLATE clauses
  (i.e. COLLATE can come before CHARACTER SET).
  Merge a COLLATE clause.
*/
bool Lex_charset_collation_st::
       merge_unordered_collate_clause(const Lex_charset_collation_st &cl)
{
  /*
    cl cannot be a CHARACTER SET clause: in such cases the caller
    should call merge_unordered_charset() instead.
  */
  DBUG_ASSERT(cl.type() != TYPE_CHARACTER_SET);
  DBUG_ASSERT(cl.type() != TYPE_EMPTY);

  switch (m_type){
  case TYPE_EMPTY:
    /*
      Neither CHARACTER SET nor COLLATE was specified before.
      This is the leftmost COLLATE clause.
      TODO: fix this difference:
      - CHARACTER SET DEFAULT .. COLLATE cl - returns OK
      - COLLATE cl .. CHARACTER SET DEFAULT - returns error
    */
    *this= cl;
    return false;

  case TYPE_CHARACTER_SET:
    {
      /*
        We had a CHARACTER SET clause before.
        Now we're adding a COLLATE clause.
      */
      switch (cl.type()) {
      case TYPE_EMPTY:
        DBUG_ASSERT(0);
        return false;
      case TYPE_CHARACTER_SET:
        DBUG_ASSERT(0);
        return false;
      case TYPE_COLLATE_EXACT:
      case TYPE_COLLATE_CONTEXTUALLY_TYPED:
        {
          // CHARACTER SET cs .. COLLATE cl
          Lex_explicit_charset_opt_collate tmp(charset_collation(), false);
          if (tmp.merge_collate_or_error(cl))
            return true;
          *this= Lex_charset_collation(tmp);
          return false;
        }
      }
    }
    break;

  case TYPE_COLLATE_EXACT:
  case TYPE_COLLATE_CONTEXTUALLY_TYPED:
    // [CHARACTER SET cs] .. COLLATE cl1.. COLLATE cl2
    return merge_collate_clause_and_collate_clause(cl);

  }
  DBUG_ASSERT(0);
  return false;
}


/**
  Get an explicit or a contextually typed collation by name,
  send error to client on failure.

  @param name      Collation name
  @param utf8_flag The utf8 translation flag
  @return
  @retval          NULL on error
  @retval          Pointter to CHARSET_INFO with the given name on success
*/
Lex_charset_collation_st
Lex_charset_collation_st::get_by_name_or_error(const char *name, myf utf8_flag)
{
  Charset_loader_mysys loader;
  CHARSET_INFO *cs;

  if (!strncasecmp(name, STRING_WITH_LEN("uca1400_")) &&
      (cs= loader.get_contextually_typed_collation(name)))
    return Lex_charset_collation(cs, TYPE_COLLATE_CONTEXTUALLY_TYPED);

  if ((cs= loader.get_exact_collation(name, utf8_flag)))
    return Lex_charset_collation(cs, TYPE_COLLATE_EXACT);

  // Not found, neither explicit, nor contextually typed
  loader.raise_unknown_collation_error(name, &my_charset_utf8mb3_general_ci);
  return Lex_charset_collation(NULL, TYPE_EMPTY);
}


bool Lex_maybe_default_charset_collation_st::merge_unordered_charset_default()
{
  switch (m_type) {
  case TYPE_EMPTY:
    // CHARACTER SET DEFAULT
    // CHARACTER SET DEFAULT .. CHARACTER SET DEFAULT
    m_had_charset_default= true;
    return false;
  case TYPE_CHARACTER_SET:
    // CHARACTER SET cs .. CHARACTER SET DEFAULT
    my_error(ER_CONFLICTING_DECLARATIONS, MYF(0),
             "CHARACTER SET ", charset_collation()->cs_name.str,
             "CHARACTER SET ", "DEFAULT");
    return true;

  case TYPE_COLLATE_EXACT:
    // COLLATE latin1_bin .. CHARACTER SET DEFAULT
    my_error(ER_CONFLICTING_DECLARATIONS, MYF(0),
             "COLLATE ", collation_name_for_show().str,
             "CHARACTER SET ", "DEFAULT");
    return true;

  case TYPE_COLLATE_CONTEXTUALLY_TYPED:
    // COLLATE uca1400_ai_ci .. CHARACTER SET DEFAULT
    m_had_charset_default= true;
    return false;
  }
  DBUG_ASSERT(0);
  return false;
}


bool
Lex_maybe_default_charset_collation_st::
  merge_unordered_charset_exact(CHARSET_INFO *cs)
{
  switch (m_type) {
  case TYPE_EMPTY:
  case TYPE_CHARACTER_SET:
    if (m_had_charset_default)
    {
      // CHARACTER SET DEFAULT .. CHARACTER SET cs
      my_error(ER_CONFLICTING_DECLARATIONS, MYF(0),
               "CHARACTER SET ", "DEFAULT",
               "CHARACTER SET ", cs->cs_name.str);
      return true;
    }
    return Lex_charset_collation_st::merge_unordered_charset_exact(cs);

  case TYPE_COLLATE_EXACT:
     if (m_had_charset_default)
     {
        // CHARACTER SET DEFAULT ..  COLLATE latin1_bin
        my_error(ER_CONFLICTING_DECLARATIONS, MYF(0),
                 "CHARACTER SET ", "DEFAULT",
                 "COLLATE ", collation_name_for_show().str);
        return true;
     }
     return Lex_charset_collation_st::merge_unordered_charset_exact(cs);

  case TYPE_COLLATE_CONTEXTUALLY_TYPED:
    // COLLATE uca1400_as_ci .. CHARACTER SET cs
    return Lex_charset_collation_st::merge_unordered_charset_exact(cs);
  }
  DBUG_ASSERT(0);
  return false;
}


bool
Lex_maybe_default_charset_collation_st::
  merge_unordered_collate_clause(const Lex_charset_collation_st &cl)
{
  if (m_had_charset_default)
  {
    switch (cl.type()) {
    case TYPE_EMPTY:
    case TYPE_CHARACTER_SET:
      DBUG_ASSERT(0);
      break;

    case TYPE_COLLATE_EXACT:
      // CHARACTER SET DEFAULT ..  COLLATE latin1_bin
      my_error(ER_CONFLICTING_DECLARATIONS, MYF(0),
               "CHARACTER SET ", "DEFAULT",
               "COLLATE ", cl.collation_name_for_show().str);
      return true;      

    case TYPE_COLLATE_CONTEXTUALLY_TYPED:
      break;
    }
  }
  return Lex_charset_collation_st::merge_unordered_collate_clause(cl);
}


CHARSET_INFO *
Lex_maybe_default_charset_collation_st::
  resolved_to_character_set(CHARSET_INFO *upper_level_default,
                            CHARSET_INFO *current_level_default) const
{
  switch (type()) {
  case TYPE_EMPTY:
    if (m_had_charset_default)     // CHARACTER SET DEFAULT
      return Charset_loader_mysys().
               find_default_collation(upper_level_default);
    return current_level_default; // Empty, reuse CHARACTER SET and COLLATE

  case TYPE_CHARACTER_SET:
  case TYPE_COLLATE_EXACT:
    /*
      These combinations should have failed earlier:
        CHARACTER SET DEFAULT .. CHARACTER SET cs
        CHARACTER SET cs .. CHARACTER SET DEFAULT
        CHARACTER SET DEFAULT .. COLLATE cl
        COLLATE cl .. CHARACTER SET DEFAULT
    */
    DBUG_ASSERT(!m_had_charset_default);
    return charset_collation();

  case TYPE_COLLATE_CONTEXTUALLY_TYPED:
    return Lex_charset_collation_st::
             resolved_to_character_set(current_level_default);
  }
  DBUG_ASSERT(0);
  return NULL;
}
