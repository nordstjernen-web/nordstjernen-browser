/* Pango
 *
 * Copyright (C) 2021 Matthias Clasen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __NS_PANGO_ITEM_PRIVATE_H__
#define __NS_PANGO_ITEM_PRIVATE_H__

#include <ns-pango/pango-item.h>
#include <ns-pango/pango-break.h>

G_BEGIN_DECLS

/**
 * We have to do some extra work for adding the char_offset field
 * to NsPangoItem to preserve ABI in the face of pango's open-coded
 * structs.
 *
 * Internally, pango uses the NsPangoItemPrivate type, and we use
 * a bit in the NsPangoAnalysis flags to indicate whether we are
 * dealing with a NsPangoItemPrivate struct or not.
 */

#define NS_PANGO_ANALYSIS_FLAG_HAS_CHAR_OFFSET (1 << 7)

typedef struct _PangoAnalysisPrivate NsPangoAnalysisPrivate;

struct _PangoAnalysisPrivate
{
  gpointer reserved;
  NsPangoFont *size_font;
  NsPangoFont *font;

  guint8 level;
  guint8 gravity;
  guint8 flags;

  guint8 script;
  NsPangoLanguage *language;

  GSList *extra_attrs;
};

typedef struct _PangoItemPrivate NsPangoItemPrivate;

#if defined(__x86_64__) && !defined(__ILP32__)

struct _PangoItemPrivate
{
  int offset;
  int length;
  int num_chars;
  int char_offset;
  NsPangoAnalysis analysis;
};

#else

struct _PangoItemPrivate
{
  int offset;
  int length;
  int num_chars;
  NsPangoAnalysis analysis;
  int char_offset;
};

#endif

G_STATIC_ASSERT (offsetof (NsPangoItem, offset) == offsetof (NsPangoItemPrivate, offset));
G_STATIC_ASSERT (offsetof (NsPangoItem, length) == offsetof (NsPangoItemPrivate, length));
G_STATIC_ASSERT (offsetof (NsPangoItem, num_chars) == offsetof (NsPangoItemPrivate, num_chars));
G_STATIC_ASSERT (offsetof (NsPangoItem, analysis) == offsetof (NsPangoItemPrivate, analysis));

void               ns_pango_analysis_collect_features    (const NsPangoAnalysis        *analysis,
                                                       hb_feature_t               *features,
                                                       guint                       length,
                                                       guint                      *num_features);

void               ns_pango_analysis_set_size_font       (NsPangoAnalysis              *analysis,
                                                       NsPangoFont                  *font);
NsPangoFont *        ns_pango_analysis_get_size_font       (const NsPangoAnalysis        *analysis);

GList *            ns_pango_itemize_with_font            (NsPangoContext               *context,
                                                       NsPangoDirection              base_dir,
                                                       const char                 *text,
                                                       int                         start_index,
                                                       int                         length,
                                                       NsPangoAttrList              *attrs,
                                                       NsPangoAttrIterator          *cached_iter,
                                                       const NsPangoFontDescription *desc);

GList *            ns_pango_itemize_post_process_items   (NsPangoContext               *context,
                                                       const char                 *text,
                                                       NsPangoLogAttr               *log_attrs,
                                                       GList                      *items);

void               ns_pango_item_unsplit                 (NsPangoItem *orig,
                                                       int        split_index,
                                                       int        split_offset);


G_END_DECLS

#endif /* __NS_PANGO_ITEM_PRIVATE_H__ */
