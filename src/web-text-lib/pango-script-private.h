/* Pango
 * pango-script-private.h: Script tag handling, private definitions
 *
 * Copyright (C) 2002 Red Hat Software
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __NS_PANGO_SCRIPT_PRIVATE_H__
#define __NS_PANGO_SCRIPT_PRIVATE_H__

#define PAREN_STACK_DEPTH 128

typedef struct _ParenStackEntry ParenStackEntry;

struct _ParenStackEntry
{
  int pair_index;
  NsPangoScript script_code;
};

struct _PangoScriptIter
{
  const gchar *text_start;
  const gchar *text_end;

  const gchar *script_start;
  const gchar *script_end;
  NsPangoScript script_code;

  ParenStackEntry paren_stack[PAREN_STACK_DEPTH];
  int paren_sp;
};

NsPangoScriptIter *
_ns_pango_script_iter_init (NsPangoScriptIter *iter,
	                 const char      *text,
			 int              length);

void
_ns_pango_script_iter_fini (NsPangoScriptIter *iter);

#endif /* __NS_PANGO_SCRIPT_PRIVATE_H__ */
