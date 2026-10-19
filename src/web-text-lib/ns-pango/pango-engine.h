/* Pango
 * pango-engine.h: Engines for script and language specific processing
 *
 * Copyright (C) 2000,2003 Red Hat Software
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

#ifndef __NS_PANGO_ENGINE_H__
#define __NS_PANGO_ENGINE_H__

#include <ns-pango/pango-types.h>
#include <ns-pango/pango-item.h>
#include <ns-pango/pango-font.h>
#include <ns-pango/pango-glyph.h>
#include <ns-pango/pango-script.h>

G_BEGIN_DECLS

/* All of this is deprecated and entirely useless for bindings.
 * Leave it out of the gir file.
 */
#ifndef __GI_SCANNER__

#ifndef NS_PANGO_DISABLE_DEPRECATED

/**
 * NS_PANGO_RENDER_TYPE_NONE:
 *
 * A string constant defining the render type
 * for engines that are not rendering-system specific.
 *
 * Deprecated: 1.38
 */
#define NS_PANGO_RENDER_TYPE_NONE "NsPangoRenderNone"

#define NS_TYPE_PANGO_ENGINE              (ns_pango_engine_get_type ())
#define NS_PANGO_ENGINE(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_ENGINE, NsPangoEngine))
#define NS_PANGO_IS_ENGINE(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_ENGINE))
#define NS_PANGO_ENGINE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), NS_TYPE_PANGO_ENGINE, NsPangoEngineClass))
#define NS_PANGO_IS_ENGINE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), NS_TYPE_PANGO_ENGINE))
#define NS_PANGO_ENGINE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), NS_TYPE_PANGO_ENGINE, NsPangoEngineClass))

typedef struct _PangoEngine NsPangoEngine;
typedef struct _PangoEngineClass NsPangoEngineClass;

/**
 * NsPangoEngine:
 *
 * `NsPangoEngine` is the base class for all types of language and
 * script specific engines. It has no functionality by itself.
 *
 * Deprecated: 1.38
 **/
struct _PangoEngine
{
  /*< private >*/
  GObject parent_instance;
};

/**
 * NsPangoEngineClass:
 *
 * Class structure for `NsPangoEngine`
 *
 * Deprecated: 1.38
 **/
struct _PangoEngineClass
{
  /*< private >*/
  GObjectClass parent_class;
};

NS_PANGO_DEPRECATED_IN_1_38
GType ns_pango_engine_get_type (void) G_GNUC_CONST;

/**
 * NS_PANGO_ENGINE_TYPE_LANG:
 *
 * A string constant defining the engine type for language engines.
 * These engines derive from `NsPangoEngineLang`.
 *
 * Deprecated: 1.38
 */
#define NS_PANGO_ENGINE_TYPE_LANG "NsPangoEngineLang"

#define NS_TYPE_PANGO_ENGINE_LANG              (ns_pango_engine_lang_get_type ())
#define NS_PANGO_ENGINE_LANG(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_ENGINE_LANG, NsPangoEngineLang))
#define NS_PANGO_IS_ENGINE_LANG(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_ENGINE_LANG))
#define NS_PANGO_ENGINE_LANG_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), NS_TYPE_PANGO_ENGINE_LANG, NsPangoEngineLangClass))
#define NS_PANGO_IS_ENGINE_LANG_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), NS_TYPE_PANGO_ENGINE_LANG))
#define NS_PANGO_ENGINE_LANG_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), NS_TYPE_PANGO_ENGINE_LANG, NsPangoEngineLangClass))

typedef struct _PangoEngineLangClass NsPangoEngineLangClass;

/**
 * NsPangoEngineLang:
 *
 * The `NsPangoEngineLang` class is implemented by engines that
 * customize the rendering-system independent part of the
 * Pango pipeline for a particular script or language. For
 * instance, a custom `NsPangoEngineLang` could be provided for
 * Thai to implement the dictionary-based word boundary
 * lookups needed for that language.
 *
 * Deprecated: 1.38
 **/
struct _PangoEngineLang
{
  /*< private >*/
  NsPangoEngine parent_instance;
};

/**
 * NsPangoEngineLangClass:
 * @script_break: (nullable): Provides a custom implementation of
 * ns_pango_break().  If %NULL, ns_pango_default_break() is used instead. If
 * not %NULL, for Pango versions before 1.16 (module interface version
 * before 1.6.0), this was called instead of ns_pango_default_break(),
 * but in newer versions, ns_pango_default_break() is always called and
 * this is called after that to allow tailoring the breaking results.
 *
 * Class structure for `NsPangoEngineLang`
 *
 * Deprecated: 1.38
 **/
struct _PangoEngineLangClass
{
  /*< private >*/
  NsPangoEngineClass parent_class;

  /*< public >*/
  void (*script_break) (NsPangoEngineLang *engine,
			const char    *text,
			int            len,
			NsPangoAnalysis *analysis,
			NsPangoLogAttr  *attrs,
			int            attrs_len);
};

NS_PANGO_DEPRECATED_IN_1_38
GType ns_pango_engine_lang_get_type (void) G_GNUC_CONST;

/**
 * NS_PANGO_ENGINE_TYPE_SHAPE:
 *
 * A string constant defining the engine type for shaping engines.
 * These engines derive from `NsPangoEngineShape`.
 *
 * Deprecated: 1.38
 */
#define NS_PANGO_ENGINE_TYPE_SHAPE "NsPangoEngineShape"

#define NS_TYPE_PANGO_ENGINE_SHAPE              (ns_pango_engine_shape_get_type ())
#define NS_PANGO_ENGINE_SHAPE(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_ENGINE_SHAPE, NsPangoEngineShape))
#define NS_PANGO_IS_ENGINE_SHAPE(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_ENGINE_SHAPE))
#define NS_PANGO_ENGINE_SHAPE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), NS_TYPE_PANGO_ENGINE_SHAPE, NsPangoEngineShapeClass))
#define NS_PANGO_IS_ENGINE_SHAPE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), NS_TYPE_PANGO_ENGINE_SHAPE))
#define NS_PANGO_ENGINE_SHAPE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), NS_TYPE_PANGO_ENGINE_SHAPE, NsPangoEngineShapeClass))

typedef struct _PangoEngineShapeClass NsPangoEngineShapeClass;

/**
 * NsPangoEngineShape:
 *
 * The `NsPangoEngineShape` class is implemented by engines that
 * customize the rendering-system dependent part of the
 * Pango pipeline for a particular script or language.
 * A `NsPangoEngineShape` implementation is then specific to both
 * a particular rendering system or group of rendering systems
 * and to a particular script. For instance, there is one
 * `NsPangoEngineShape` implementation to handle shaping Arabic
 * for Fontconfig-based backends.
 *
 * Deprecated: 1.38
 **/
struct _PangoEngineShape
{
  NsPangoEngine parent_instance;
};

/**
 * NsPangoEngineShapeClass:
 * @script_shape: Given a font, a piece of text, and a `NsPangoAnalysis`
 *   structure, converts characters to glyphs and positions the
 *   resulting glyphs. The results are stored in the `NsPangoGlyphString`
 *   that is passed in. (The implementation should resize it
 *   appropriately using ns_pango_glyph_string_set_size()). All fields
 *   of the @log_clusters and @glyphs array must be filled in, with
 *   the exception that Pango will automatically generate
 *   `glyphs->glyphs[i].attr.is_cluster_start`
 *   using the @log_clusters array. Each input character must occur in one
 *   of the output logical clusters;
 *   if no rendering is desired for a character, this may involve
 *   inserting glyphs with the `NsPangoGlyph` ID %NS_PANGO_GLYPH_EMPTY, which
 *   is guaranteed never to render. If the shaping fails for any reason,
 *   the shaper should return with an empty (zero-size) glyph string.
 *   If the shaper has not set the size on the glyph string yet, simply
 *   returning signals the failure too.
 * @covers: Returns the characters that this engine can cover
 *   with a given font for a given language. If not overridden, the default
 *   implementation simply returns the coverage information for the
 *   font itself unmodified.
 *
 * Class structure for `NsPangoEngineShape`
 *
 * Deprecated: 1.38
 **/
struct _PangoEngineShapeClass
{
  /*< private >*/
  NsPangoEngineClass parent_class;

  /*< public >*/
  void (*script_shape) (NsPangoEngineShape    *engine,
			NsPangoFont           *font,
			const char          *item_text,
			unsigned int         item_length,
			const NsPangoAnalysis *analysis,
			NsPangoGlyphString    *glyphs,
			const char          *paragraph_text,
			unsigned int         paragraph_length);
  NsPangoCoverageLevel (*covers)   (NsPangoEngineShape *engine,
				  NsPangoFont        *font,
				  NsPangoLanguage    *language,
				  gunichar          wc);
};

NS_PANGO_DEPRECATED_IN_1_38
GType ns_pango_engine_shape_get_type (void) G_GNUC_CONST;

typedef struct _PangoEngineInfo NsPangoEngineInfo;
typedef struct _PangoEngineScriptInfo NsPangoEngineScriptInfo;

/**
 * NsPangoEngineScriptInfo:
 * @script: a `NsPangoScript`. The value %NS_PANGO_SCRIPT_COMMON has
 * the special meaning here of "all scripts"
 * @langs: a semicolon separated list of languages that this
 * engine handles for this script. This may be empty,
 * in which case the engine is saying that it is a
 * fallback choice for all languages for this range,
 * but should not be used if another engine
 * indicates that it is specific for the language for
 * a given code point. An entry in this list of "*"
 * indicates that this engine is specific to all
 * languages for this range.
 *
 * The `NsPangoEngineScriptInfo` structure contains
 * information about how the shaper covers a particular script.
 *
 * Deprecated: 1.38
 */
struct _PangoEngineScriptInfo
{
  NsPangoScript script;
  const gchar *langs;
};

/**
 * NsPangoEngineInfo:
 * @id: a unique string ID for the engine.
 * @engine_type: a string identifying the engine type.
 * @render_type: a string identifying the render type.
 * @scripts: array of scripts this engine supports.
 * @n_scripts: number of items in @scripts.
 *
 * The `NsPangoEngineInfo` structure contains information about a particular
 * engine. It contains the following fields:
 *
 * Deprecated: 1.38
 */
struct _PangoEngineInfo
{
  const gchar *id;
  const gchar *engine_type;
  const gchar *render_type;
  NsPangoEngineScriptInfo *scripts;
  gint n_scripts;
};

/**
 * script_engine_list: (skip)
 * @engines: location to store a pointer to an array of engines.
 * @n_engines: location to store the number of elements in @engines.
 *
 * Do not use.
 *
 * Deprecated: 1.38
 **/
NS_PANGO_DEPRECATED_IN_1_38
void script_engine_list (NsPangoEngineInfo **engines,
			 int              *n_engines);

/**
 * script_engine_init: (skip)
 * @module: a `GTypeModule` structure used to associate any
 *  GObject types created in this module with the module.
 *
 * Do not use.
 *
 * Deprecated: 1.38
 **/
NS_PANGO_DEPRECATED_IN_1_38
void script_engine_init (GTypeModule *module);


/**
 * script_engine_exit: (skip)
 *
 * Do not use.
 *
 * Deprecated: 1.38
 **/
NS_PANGO_DEPRECATED_IN_1_38
void script_engine_exit (void);

/**
 * script_engine_create: (skip)
 * @id: the ID of an engine as reported by script_engine_list.
 *
 * Do not use.
 *
 * Deprecated: 1.38
 **/
NS_PANGO_DEPRECATED_IN_1_38
NsPangoEngine *script_engine_create (const char *id);

/* Utility macro used by NS_PANGO_ENGINE_LANG_DEFINE_TYPE and
 * NS_PANGO_ENGINE_LANG_DEFINE_TYPE
 */
#define NS_PANGO_ENGINE_DEFINE_TYPE(name, prefix, class_init, instance_init, parent_type) \
static GType prefix ## _type;						  \
static void								  \
prefix ## _register_type (GTypeModule *module)				  \
{									  \
  const GTypeInfo object_info =						  \
    {									  \
      sizeof (name ## Class),						  \
      (GBaseInitFunc) NULL,						  \
      (GBaseFinalizeFunc) NULL,						  \
      (GClassInitFunc) class_init,					  \
      (GClassFinalizeFunc) NULL,					  \
      NULL,          /* class_data */					  \
      sizeof (name),							  \
      0,             /* n_prelocs */					  \
      (GInstanceInitFunc) instance_init,				  \
      NULL           /* value_table */					  \
    };									  \
									  \
  prefix ## _type =  g_type_module_register_type (module, parent_type,	  \
						  # name,		  \
						  &object_info, 0);	  \
}

/**
 * NS_PANGO_ENGINE_LANG_DEFINE_TYPE:
 * @name: Name of the the type to register (for example:, ArabicEngineFc)
 * @prefix: Prefix for symbols that will be defined (for example:, arabic_engine_fc)
 * @class_init: (nullable): Class initialization function for the new type
 * @instance_init: (nullable): Instance initialization function for the new type
 *
 * Outputs the necessary code for GObject type registration for a
 * `NsPangoEngineLang` class defined in a module. Two static symbols
 * are defined.
 *
 * <programlisting>
 *  static GType *prefix*_type;
 *  static void *prefix*_register_type (GTypeModule module);
 *
 * The *prefix*_register_type()
 * function should be called in your script_engine_init() function for
 * each type that your module implements, and then your script_engine_create()
 * function can create instances of the object as follows:
 *
 * ```
 * NsPangoEngine *engine = g_object_new (prefix_type, NULL);
 * ```
 *
 * Deprecated: 1.38
 **/
#define NS_PANGO_ENGINE_LANG_DEFINE_TYPE(name, prefix, class_init, instance_init)	\
  NS_PANGO_ENGINE_DEFINE_TYPE (name, prefix,				\
			    class_init, instance_init,			\
			    NS_TYPE_PANGO_ENGINE_LANG)

/**
 * NS_PANGO_ENGINE_SHAPE_DEFINE_TYPE:
 * @name: Name of the the type to register (for example:, ArabicEngineFc)
 * @prefix: Prefix for symbols that will be defined (for example:, arabic_engine_fc)
 * @class_init: (nullable): Class initialization function for the new type
 * @instance_init: (nullable): Instance initialization function for the new type
 *
 * Outputs the necessary code for GObject type registration for a
 * `NsPangoEngineShape` class defined in a module. Two static symbols
 * are defined.
 *
 * <programlisting>
 *  static GType *prefix*_type;
 *  static void *prefix*_register_type (GTypeModule module);
 * </programlisting>
 *
 * The *prefix*_register_type()
 * function should be called in your script_engine_init() function for
 * each type that your module implements, and then your script_engine_create()
 * function can create instances of the object as follows:
 *
 * ```
 * NsPangoEngine *engine = g_object_new (prefix_type, NULL);
 * ```
 *
 * Deprecated: 1.38
 **/
#define NS_PANGO_ENGINE_SHAPE_DEFINE_TYPE(name, prefix, class_init, instance_init)	\
  NS_PANGO_ENGINE_DEFINE_TYPE (name, prefix,				\
			    class_init, instance_init,			\
			    NS_TYPE_PANGO_ENGINE_SHAPE)

/* Macro used for possibly builtin Pango modules. Not useful
 * for externally build modules. If we are compiling a module standalone,
 * then we name the entry points script_engine_list, etc. But if we
 * are compiling it for inclusion directly in Pango, then we need them to
 * to have distinct names for this module, so we prepend a prefix.
 *
 * The two intermediate macros are to deal with details of the C
 * preprocessor; token pasting tokens must be function arguments,
 * and macro substitution isn't used on function arguments that
 * are used for token pasting.
 */
#ifdef NS_PANGO_MODULE_PREFIX
#define NS_PANGO_MODULE_ENTRY(func) _PANGO_MODULE_ENTRY2(NS_PANGO_MODULE_PREFIX,func)
#define _PANGO_MODULE_ENTRY2(prefix,func) _PANGO_MODULE_ENTRY3(prefix,func)
#define _PANGO_MODULE_ENTRY3(prefix,func) prefix##_script_engine_##func
#else
#define NS_PANGO_MODULE_ENTRY(func) script_engine_##func
#endif

#endif /* NS_PANGO_DISABLE_DEPRECATED */

#endif /* __GI_SCANNER__ */

G_END_DECLS

#endif /* __NS_PANGO_ENGINE_H__ */
