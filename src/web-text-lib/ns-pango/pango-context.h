/* Pango
 * pango-context.h: Rendering contexts
 *
 * Copyright (C) 2000 Red Hat Software
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

#ifndef __NS_PANGO_CONTEXT_H__
#define __NS_PANGO_CONTEXT_H__

#include <ns-pango/pango-types.h>
#include <ns-pango/pango-font.h>
#include <ns-pango/pango-fontmap.h>
#include <ns-pango/pango-attributes.h>
#include <ns-pango/pango-direction.h>

G_BEGIN_DECLS

typedef struct _PangoContextClass NsPangoContextClass;

#define NS_TYPE_PANGO_CONTEXT              (ns_pango_context_get_type ())
#define NS_PANGO_CONTEXT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_CONTEXT, NsPangoContext))
#define NS_PANGO_CONTEXT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), NS_TYPE_PANGO_CONTEXT, NsPangoContextClass))
#define NS_PANGO_IS_CONTEXT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_CONTEXT))
#define NS_PANGO_IS_CONTEXT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), NS_TYPE_PANGO_CONTEXT))
#define NS_PANGO_CONTEXT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), NS_TYPE_PANGO_CONTEXT, NsPangoContextClass))


NS_PANGO_AVAILABLE_IN_ALL
GType                   ns_pango_context_get_type                  (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_ALL
NsPangoContext *          ns_pango_context_new                       (void);
NS_PANGO_AVAILABLE_IN_1_32
void                    ns_pango_context_changed                   (NsPangoContext                 *context);
NS_PANGO_AVAILABLE_IN_ALL
void                    ns_pango_context_set_font_map              (NsPangoContext                 *context,
                                                                 NsPangoFontMap                 *font_map);
NS_PANGO_AVAILABLE_IN_1_6
NsPangoFontMap *          ns_pango_context_get_font_map              (NsPangoContext                 *context);
NS_PANGO_AVAILABLE_IN_1_32
guint                   ns_pango_context_get_serial                (NsPangoContext                 *context);
NS_PANGO_AVAILABLE_IN_ALL
void                    ns_pango_context_list_families             (NsPangoContext                 *context,
                                                                 NsPangoFontFamily            ***families,
                                                                 int                          *n_families);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoFont *             ns_pango_context_load_font                 (NsPangoContext                 *context,
                                                                 const NsPangoFontDescription   *desc);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontset *          ns_pango_context_load_fontset              (NsPangoContext                 *context,
                                                                 const NsPangoFontDescription   *desc,
                                                                 NsPangoLanguage                *language);

NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontMetrics *      ns_pango_context_get_metrics               (NsPangoContext                 *context,
                                                                 const NsPangoFontDescription   *desc,
                                                                 NsPangoLanguage                *language);

NS_PANGO_AVAILABLE_IN_ALL
void                    ns_pango_context_set_font_description      (NsPangoContext                 *context,
                                                                 const NsPangoFontDescription   *desc);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontDescription *  ns_pango_context_get_font_description      (NsPangoContext                 *context);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoLanguage *         ns_pango_context_get_language              (NsPangoContext                 *context);
NS_PANGO_AVAILABLE_IN_ALL
void                    ns_pango_context_set_language              (NsPangoContext                 *context,
                                                                 NsPangoLanguage                *language);
NS_PANGO_AVAILABLE_IN_ALL
void                    ns_pango_context_set_base_dir              (NsPangoContext                 *context,
                                                                 NsPangoDirection                direction);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoDirection          ns_pango_context_get_base_dir              (NsPangoContext                 *context);
NS_PANGO_AVAILABLE_IN_1_16
void                    ns_pango_context_set_base_gravity          (NsPangoContext                 *context,
                                                                 NsPangoGravity                  gravity);
NS_PANGO_AVAILABLE_IN_1_16
NsPangoGravity            ns_pango_context_get_base_gravity          (NsPangoContext                 *context);
NS_PANGO_AVAILABLE_IN_1_16
NsPangoGravity            ns_pango_context_get_gravity               (NsPangoContext                 *context);
NS_PANGO_AVAILABLE_IN_1_16
void                    ns_pango_context_set_gravity_hint          (NsPangoContext                 *context,
                                                                 NsPangoGravityHint              hint);
NS_PANGO_AVAILABLE_IN_1_16
NsPangoGravityHint        ns_pango_context_get_gravity_hint          (NsPangoContext                 *context);

NS_PANGO_AVAILABLE_IN_1_6
void                    ns_pango_context_set_matrix                (NsPangoContext                 *context,
                                                                 const NsPangoMatrix            *matrix);
NS_PANGO_AVAILABLE_IN_1_6
const NsPangoMatrix *     ns_pango_context_get_matrix                (NsPangoContext                 *context);

NS_PANGO_AVAILABLE_IN_1_44
void                    ns_pango_context_set_round_glyph_positions (NsPangoContext                 *context,
                                                                 gboolean                      round_positions);
NS_PANGO_AVAILABLE_IN_1_44
gboolean                ns_pango_context_get_round_glyph_positions (NsPangoContext                 *context);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoContext, g_object_unref)

G_END_DECLS

#endif /* __NS_PANGO_CONTEXT_H__ */
