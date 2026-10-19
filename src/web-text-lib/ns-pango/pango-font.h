/* Pango
 * pango-font.h: Font handling
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

#ifndef __NS_PANGO_FONT_H__
#define __NS_PANGO_FONT_H__

#include <ns-pango/pango-coverage.h>
#include <ns-pango/pango-types.h>

#include <glib-object.h>
#include <hb.h>

G_BEGIN_DECLS

/**
 * NsPangoFontDescription:
 *
 * A `NsPangoFontDescription` describes a font in an implementation-independent
 * manner.
 *
 * `NsPangoFontDescription` structures are used both to list what fonts are
 * available on the system and also for specifying the characteristics of
 * a font to load.
 */
typedef struct _PangoFontDescription NsPangoFontDescription;

/**
 * NsPangoFontMetrics:
 *
 * A `NsPangoFontMetrics` structure holds the overall metric information
 * for a font.
 *
 * The information in a `NsPangoFontMetrics` structure may be restricted
 * to a script. The fields of this structure are private to implementations
 * of a font backend. See the documentation of the corresponding getters
 * for documentation of their meaning.
 *
 * For an overview of the most important metrics, see:
 *
 * <picture>
 *   <source srcset="fontmetrics-dark.png" media="(prefers-color-scheme: dark)">
 *   <img alt="Font metrics" src="fontmetrics-light.png">
 * </picture>

 */
typedef struct _PangoFontMetrics NsPangoFontMetrics;

/**
 * NsPangoStyle:
 * @NS_PANGO_STYLE_NORMAL: the font is upright.
 * @NS_PANGO_STYLE_OBLIQUE: the font is slanted, but in a roman style.
 * @NS_PANGO_STYLE_ITALIC: the font is slanted in an italic style.
 *
 * An enumeration specifying the various slant styles possible for a font.
 **/
typedef enum {
  NS_PANGO_STYLE_NORMAL,
  NS_PANGO_STYLE_OBLIQUE,
  NS_PANGO_STYLE_ITALIC
} NsPangoStyle;

/**
 * NsPangoVariant:
 * @NS_PANGO_VARIANT_NORMAL: A normal font.
 * @NS_PANGO_VARIANT_SMALL_CAPS: A font with the lower case characters
 *   replaced by smaller variants of the capital characters.
 * @NS_PANGO_VARIANT_ALL_SMALL_CAPS: A font with all characters
 *   replaced by smaller variants of the capital characters. Since: 1.50
 * @NS_PANGO_VARIANT_PETITE_CAPS: A font with the lower case characters
 *   replaced by smaller variants of the capital characters.
 *   Petite Caps can be even smaller than Small Caps. Since: 1.50
 * @NS_PANGO_VARIANT_ALL_PETITE_CAPS: A font with all characters
 *   replaced by smaller variants of the capital characters.
 *   Petite Caps can be even smaller than Small Caps. Since: 1.50
 * @NS_PANGO_VARIANT_UNICASE: A font with the upper case characters
 *   replaced by smaller variants of the capital letters. Since: 1.50
 * @NS_PANGO_VARIANT_TITLE_CAPS: A font with capital letters that
 *   are more suitable for all-uppercase titles. Since: 1.50
 *
 * An enumeration specifying capitalization variant of the font.
 */
typedef enum {
  NS_PANGO_VARIANT_NORMAL,
  NS_PANGO_VARIANT_SMALL_CAPS,
  NS_PANGO_VARIANT_ALL_SMALL_CAPS,
  NS_PANGO_VARIANT_PETITE_CAPS,
  NS_PANGO_VARIANT_ALL_PETITE_CAPS,
  NS_PANGO_VARIANT_UNICASE,
  NS_PANGO_VARIANT_TITLE_CAPS
} NsPangoVariant;

/**
 * NsPangoWeight:
 * @NS_PANGO_WEIGHT_THIN: the thin weight (= 100) Since: 1.24
 * @NS_PANGO_WEIGHT_ULTRALIGHT: the ultralight weight (= 200)
 * @NS_PANGO_WEIGHT_LIGHT: the light weight (= 300)
 * @NS_PANGO_WEIGHT_SEMILIGHT: the semilight weight (= 350) Since: 1.36.7
 * @NS_PANGO_WEIGHT_BOOK: the book weight (= 380) Since: 1.24)
 * @NS_PANGO_WEIGHT_NORMAL: the default weight (= 400)
 * @NS_PANGO_WEIGHT_MEDIUM: the medium weight (= 500) Since: 1.24
 * @NS_PANGO_WEIGHT_SEMIBOLD: the semibold weight (= 600)
 * @NS_PANGO_WEIGHT_BOLD: the bold weight (= 700)
 * @NS_PANGO_WEIGHT_ULTRABOLD: the ultrabold weight (= 800)
 * @NS_PANGO_WEIGHT_HEAVY: the heavy weight (= 900)
 * @NS_PANGO_WEIGHT_ULTRAHEAVY: the ultraheavy weight (= 1000) Since: 1.24
 *
 * An enumeration specifying the weight (boldness) of a font.
 *
 * Weight is specified as a numeric value ranging from 100 to 1000.
 * This enumeration simply provides some common, predefined values.
 */
typedef enum {
  NS_PANGO_WEIGHT_THIN = 100,
  NS_PANGO_WEIGHT_ULTRALIGHT = 200,
  NS_PANGO_WEIGHT_LIGHT = 300,
  NS_PANGO_WEIGHT_SEMILIGHT = 350,
  NS_PANGO_WEIGHT_BOOK = 380,
  NS_PANGO_WEIGHT_NORMAL = 400,
  NS_PANGO_WEIGHT_MEDIUM = 500,
  NS_PANGO_WEIGHT_SEMIBOLD = 600,
  NS_PANGO_WEIGHT_BOLD = 700,
  NS_PANGO_WEIGHT_ULTRABOLD = 800,
  NS_PANGO_WEIGHT_HEAVY = 900,
  NS_PANGO_WEIGHT_ULTRAHEAVY = 1000
} NsPangoWeight;

/**
 * NsPangoStretch:
 * @NS_PANGO_STRETCH_ULTRA_CONDENSED: ultra condensed width
 * @NS_PANGO_STRETCH_EXTRA_CONDENSED: extra condensed width
 * @NS_PANGO_STRETCH_CONDENSED: condensed width
 * @NS_PANGO_STRETCH_SEMI_CONDENSED: semi condensed width
 * @NS_PANGO_STRETCH_NORMAL: the normal width
 * @NS_PANGO_STRETCH_SEMI_EXPANDED: semi expanded width
 * @NS_PANGO_STRETCH_EXPANDED: expanded width
 * @NS_PANGO_STRETCH_EXTRA_EXPANDED: extra expanded width
 * @NS_PANGO_STRETCH_ULTRA_EXPANDED: ultra expanded width
 *
 * An enumeration specifying the width of the font relative to other designs
 * within a family.
 */
typedef enum {
  NS_PANGO_STRETCH_ULTRA_CONDENSED,
  NS_PANGO_STRETCH_EXTRA_CONDENSED,
  NS_PANGO_STRETCH_CONDENSED,
  NS_PANGO_STRETCH_SEMI_CONDENSED,
  NS_PANGO_STRETCH_NORMAL,
  NS_PANGO_STRETCH_SEMI_EXPANDED,
  NS_PANGO_STRETCH_EXPANDED,
  NS_PANGO_STRETCH_EXTRA_EXPANDED,
  NS_PANGO_STRETCH_ULTRA_EXPANDED
} NsPangoStretch;

/**
 * NsPangoWidth:
 * @NS_PANGO_WIDTH_ULTRA_CONDENSED: ultra condensed width
 * @NS_PANGO_WIDTH_EXTRA_CONDENSED: extra condensed width
 * @NS_PANGO_WIDTH_CONDENSED: condensed width
 * @NS_PANGO_WIDTH_SEMI_CONDENSED: semi condensed width
 * @NS_PANGO_WIDTH_NORMAL: the normal width
 * @NS_PANGO_WIDTH_SEMI_EXPANDED: semi expanded width
 * @NS_PANGO_WIDTH_EXPANDED: expanded width
 * @NS_PANGO_WIDTH_EXTRA_EXPANDED: extra expanded width
 * @NS_PANGO_WIDTH_ULTRA_EXPANDED: ultra expanded width
 *
 * An enumeration specifying the width of the font relative to other designs
 * within a family.
 *
 * The enumeration values match [enum@NsPangoStretch], but
 * the numeric values are expanded to allow intermediate
 * values.
 *
 * Since: 1.58
 */
typedef enum {
  NS_PANGO_WIDTH_ULTRA_CONDENSED = 500,
  NS_PANGO_WIDTH_EXTRA_CONDENSED = 625,
  NS_PANGO_WIDTH_CONDENSED = 750,
  NS_PANGO_WIDTH_SEMI_CONDENSED = 875,
  NS_PANGO_WIDTH_NORMAL = 1000,
  NS_PANGO_WIDTH_SEMI_EXPANDED = 1125,
  NS_PANGO_WIDTH_EXPANDED = 1250,
  NS_PANGO_WIDTH_EXTRA_EXPANDED = 1500,
  NS_PANGO_WIDTH_ULTRA_EXPANDED = 2000,
} NsPangoWidth;

/**
 * NsPangoFontMask:
 * @NS_PANGO_FONT_MASK_FAMILY: the font family is specified.
 * @NS_PANGO_FONT_MASK_STYLE: the font style is specified.
 * @NS_PANGO_FONT_MASK_VARIANT: the font variant is specified.
 * @NS_PANGO_FONT_MASK_WEIGHT: the font weight is specified.
 * @NS_PANGO_FONT_MASK_STRETCH: the font stretch/width is specified.
 * @NS_PANGO_FONT_MASK_SIZE: the font size is specified.
 *
 * The bits in a `NsPangoFontMask` correspond to the set fields in a
 * `NsPangoFontDescription`.
 */
/**
 * NS_PANGO_FONT_MASK_GRAVITY:
 *
 * The font gravity is specified.
 *
 * Since: 1.16
 */
/**
 * NS_PANGO_FONT_MASK_VARIATIONS:
 *
 * OpenType font variations are specified.
 *
 * Since: 1.42
 */
/**
 * NS_PANGO_FONT_MASK_FEATURES:
 *
 * OpenType font features are specified.
 *
 * Since: 1.56
 */
/**
 * NS_PANGO_FONT_MASK_COLOR:
 *
 * Font color is specified.
 *
 * Since: 1.57
 */
/**
 * NS_PANGO_FONT_MASK_WIDTH:
 *
 * Font width is specified.
 *
 * This is an alias for [flags@Pango.FontMask.STRETCH].
 *
 * 1.58
 */
typedef enum {
  NS_PANGO_FONT_MASK_FAMILY  = 1 << 0,
  NS_PANGO_FONT_MASK_STYLE   = 1 << 1,
  NS_PANGO_FONT_MASK_VARIANT = 1 << 2,
  NS_PANGO_FONT_MASK_WEIGHT  = 1 << 3,
  NS_PANGO_FONT_MASK_WIDTH   = 1 << 4,
  NS_PANGO_FONT_MASK_STRETCH = 1 << 4,
  NS_PANGO_FONT_MASK_SIZE    = 1 << 5,
  NS_PANGO_FONT_MASK_GRAVITY = 1 << 6,
  NS_PANGO_FONT_MASK_VARIATIONS = 1 << 7,
  NS_PANGO_FONT_MASK_FEATURES = 1 << 8,
  NS_PANGO_FONT_MASK_COLOR    = 1 << 9,
} NsPangoFontMask;

/* CSS scale factors (1.2 factor between each size) */
/**
 * NS_PANGO_SCALE_XX_SMALL:
 *
 * The scale factor for three shrinking steps (1 / (1.2 * 1.2 * 1.2)).
 */
/**
 * NS_PANGO_SCALE_X_SMALL:
 *
 * The scale factor for two shrinking steps (1 / (1.2 * 1.2)).
 */
/**
 * NS_PANGO_SCALE_SMALL:
 *
 * The scale factor for one shrinking step (1 / 1.2).
 */
/**
 * NS_PANGO_SCALE_MEDIUM:
 *
 * The scale factor for normal size (1.0).
 */
/**
 * NS_PANGO_SCALE_LARGE:
 *
 * The scale factor for one magnification step (1.2).
 */
/**
 * NS_PANGO_SCALE_X_LARGE:
 *
 * The scale factor for two magnification steps (1.2 * 1.2).
 */
/**
 * NS_PANGO_SCALE_XX_LARGE:
 *
 * The scale factor for three magnification steps (1.2 * 1.2 * 1.2).
 */
#define NS_PANGO_SCALE_XX_SMALL ((double)0.5787037037037)
#define NS_PANGO_SCALE_X_SMALL  ((double)0.6944444444444)
#define NS_PANGO_SCALE_SMALL    ((double)0.8333333333333)
#define NS_PANGO_SCALE_MEDIUM   ((double)1.0)
#define NS_PANGO_SCALE_LARGE    ((double)1.2)
#define NS_PANGO_SCALE_X_LARGE  ((double)1.44)
#define NS_PANGO_SCALE_XX_LARGE ((double)1.728)

/**
 * NsPangoFontColor:
 * @NS_PANGO_FONT_COLOR_FORBIDDEN: The font should not have color glyphs
 * @NS_PANGO_FONT_COLOR_REQUIRED: The font should have color glyphs
 * @NS_PANGO_FONT_COLOR_DONT_CARE: The font may or may not use color
 *
 * Specifies whether a font should or should not have color glyphs.
 *
 * Since: 1.57
 */
typedef enum {
  NS_PANGO_FONT_COLOR_FORBIDDEN,
  NS_PANGO_FONT_COLOR_REQUIRED,
  NS_PANGO_FONT_COLOR_DONT_CARE,
} NsPangoFontColor;

/*
 * NsPangoFontDescription
 */

#define NS_TYPE_PANGO_FONT_DESCRIPTION (ns_pango_font_description_get_type ())

NS_PANGO_AVAILABLE_IN_ALL
GType                 ns_pango_font_description_get_type    (void) G_GNUC_CONST;
NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontDescription *ns_pango_font_description_new         (void);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontDescription *ns_pango_font_description_copy        (const NsPangoFontDescription  *desc);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontDescription *ns_pango_font_description_copy_static (const NsPangoFontDescription  *desc);
NS_PANGO_AVAILABLE_IN_ALL
guint                 ns_pango_font_description_hash        (const NsPangoFontDescription  *desc) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_ALL
gboolean              ns_pango_font_description_equal       (const NsPangoFontDescription  *desc1,
                                                          const NsPangoFontDescription  *desc2) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_ALL
void                  ns_pango_font_description_free        (NsPangoFontDescription        *desc);
NS_PANGO_DEPRECATED_IN_1_56
void                  ns_pango_font_descriptions_free       (NsPangoFontDescription       **descs,
                                                          int                          n_descs);

NS_PANGO_AVAILABLE_IN_ALL
void                 ns_pango_font_description_set_family        (NsPangoFontDescription *desc,
                                                               const char           *family);
NS_PANGO_AVAILABLE_IN_ALL
void                 ns_pango_font_description_set_family_static (NsPangoFontDescription *desc,
                                                               const char           *family);
NS_PANGO_AVAILABLE_IN_ALL
const char          *ns_pango_font_description_get_family        (const NsPangoFontDescription *desc) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_ALL
void                 ns_pango_font_description_set_style         (NsPangoFontDescription *desc,
                                                               NsPangoStyle            style);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoStyle           ns_pango_font_description_get_style         (const NsPangoFontDescription *desc) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_ALL
void                 ns_pango_font_description_set_variant       (NsPangoFontDescription *desc,
                                                               NsPangoVariant          variant);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoVariant         ns_pango_font_description_get_variant       (const NsPangoFontDescription *desc) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_ALL
void                 ns_pango_font_description_set_weight        (NsPangoFontDescription *desc,
                                                               NsPangoWeight           weight);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoWeight          ns_pango_font_description_get_weight        (const NsPangoFontDescription *desc) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_ALL
void                 ns_pango_font_description_set_stretch       (NsPangoFontDescription *desc,
                                                               NsPangoStretch          stretch);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoStretch         ns_pango_font_description_get_stretch       (const NsPangoFontDescription *desc) G_GNUC_PURE;

NS_PANGO_AVAILABLE_IN_1_58
void                 ns_pango_font_description_set_width         (NsPangoFontDescription *desc,
                                                               NsPangoWidth            width);
NS_PANGO_AVAILABLE_IN_1_58
NsPangoWidth           ns_pango_font_description_get_width         (const NsPangoFontDescription *desc);

NS_PANGO_AVAILABLE_IN_ALL
void                 ns_pango_font_description_set_size          (NsPangoFontDescription *desc,
                                                               gint                  size);
NS_PANGO_AVAILABLE_IN_ALL
gint                 ns_pango_font_description_get_size          (const NsPangoFontDescription *desc) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_1_8
void                 ns_pango_font_description_set_absolute_size (NsPangoFontDescription *desc,
                                                               double                size);
NS_PANGO_AVAILABLE_IN_1_8
gboolean             ns_pango_font_description_get_size_is_absolute (const NsPangoFontDescription *desc) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_1_16
void                 ns_pango_font_description_set_gravity       (NsPangoFontDescription *desc,
                                                               NsPangoGravity          gravity);
NS_PANGO_AVAILABLE_IN_1_16
NsPangoGravity         ns_pango_font_description_get_gravity       (const NsPangoFontDescription *desc) G_GNUC_PURE;

NS_PANGO_AVAILABLE_IN_1_42
void                 ns_pango_font_description_set_variations_static (NsPangoFontDescription       *desc,
                                                                   const char                 *variations);
NS_PANGO_AVAILABLE_IN_1_42
void                 ns_pango_font_description_set_variations    (NsPangoFontDescription       *desc,
                                                               const char                 *variations);
NS_PANGO_AVAILABLE_IN_1_42
const char          *ns_pango_font_description_get_variations    (const NsPangoFontDescription *desc) G_GNUC_PURE;

NS_PANGO_AVAILABLE_IN_1_56
void                 ns_pango_font_description_set_features_static (NsPangoFontDescription       *desc,
                                                                 const char                 *features);
NS_PANGO_AVAILABLE_IN_1_56
void                 ns_pango_font_description_set_features        (NsPangoFontDescription       *desc,
                                                                 const char                 *features);
NS_PANGO_AVAILABLE_IN_1_42
const char          *ns_pango_font_description_get_features        (const NsPangoFontDescription *desc) G_GNUC_PURE;

NS_PANGO_AVAILABLE_IN_1_57
void                 ns_pango_font_description_set_color           (NsPangoFontDescription       *desc,
                                                                 NsPangoFontColor              color);
NS_PANGO_AVAILABLE_IN_1_57
NsPangoFontColor       ns_pango_font_description_get_color           (const NsPangoFontDescription *desc);

NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontMask ns_pango_font_description_get_set_fields (const NsPangoFontDescription *desc) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_ALL
void          ns_pango_font_description_unset_fields   (NsPangoFontDescription       *desc,
                                                     NsPangoFontMask               to_unset);

NS_PANGO_AVAILABLE_IN_ALL
void ns_pango_font_description_merge        (NsPangoFontDescription       *desc,
                                          const NsPangoFontDescription *desc_to_merge,
                                          gboolean                    replace_existing);
NS_PANGO_AVAILABLE_IN_ALL
void ns_pango_font_description_merge_static (NsPangoFontDescription       *desc,
                                          const NsPangoFontDescription *desc_to_merge,
                                          gboolean                    replace_existing);

NS_PANGO_AVAILABLE_IN_ALL
gboolean ns_pango_font_description_better_match (const NsPangoFontDescription *desc,
                                              const NsPangoFontDescription *old_match,
                                              const NsPangoFontDescription *new_match) G_GNUC_PURE;

NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontDescription *ns_pango_font_description_from_string (const char                  *str);
NS_PANGO_AVAILABLE_IN_ALL
char *                ns_pango_font_description_to_string   (const NsPangoFontDescription  *desc);
NS_PANGO_AVAILABLE_IN_ALL
char *                ns_pango_font_description_to_filename (const NsPangoFontDescription  *desc);

/*
 * NsPangoFontMetrics
 */

#define NS_TYPE_PANGO_FONT_METRICS  (ns_pango_font_metrics_get_type ())

struct _PangoFontMetrics
{
  /* <private> */
  guint ref_count;

  int ascent;
  int descent;
  int height;
  int approximate_char_width;
  int approximate_digit_width;
  int underline_position;
  int underline_thickness;
  int strikethrough_position;
  int strikethrough_thickness;
};

NS_PANGO_AVAILABLE_IN_ALL
GType             ns_pango_font_metrics_get_type                    (void) G_GNUC_CONST;
NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontMetrics *ns_pango_font_metrics_ref                         (NsPangoFontMetrics *metrics);
NS_PANGO_AVAILABLE_IN_ALL
void              ns_pango_font_metrics_unref                       (NsPangoFontMetrics *metrics);
NS_PANGO_AVAILABLE_IN_ALL
int               ns_pango_font_metrics_get_ascent                  (NsPangoFontMetrics *metrics) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_ALL
int               ns_pango_font_metrics_get_descent                 (NsPangoFontMetrics *metrics) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_1_44
int               ns_pango_font_metrics_get_height                  (NsPangoFontMetrics *metrics) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_ALL
int               ns_pango_font_metrics_get_approximate_char_width  (NsPangoFontMetrics *metrics) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_ALL
int               ns_pango_font_metrics_get_approximate_digit_width (NsPangoFontMetrics *metrics) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_1_6
int               ns_pango_font_metrics_get_underline_position      (NsPangoFontMetrics *metrics) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_1_6
int               ns_pango_font_metrics_get_underline_thickness     (NsPangoFontMetrics *metrics) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_1_6
int               ns_pango_font_metrics_get_strikethrough_position  (NsPangoFontMetrics *metrics) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_1_6
int               ns_pango_font_metrics_get_strikethrough_thickness (NsPangoFontMetrics *metrics) G_GNUC_PURE;


/*
 * NsPangoFontFamily
 */

#define NS_TYPE_PANGO_FONT_FAMILY              (ns_pango_font_family_get_type ())
#define NS_PANGO_FONT_FAMILY(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_FONT_FAMILY, NsPangoFontFamily))
#define NS_PANGO_IS_FONT_FAMILY(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_FONT_FAMILY))
#define NS_PANGO_FONT_FAMILY_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), NS_TYPE_PANGO_FONT_FAMILY, NsPangoFontFamilyClass))
#define NS_PANGO_IS_FONT_FAMILY_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), NS_TYPE_PANGO_FONT_FAMILY))
#define NS_PANGO_FONT_FAMILY_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), NS_TYPE_PANGO_FONT_FAMILY, NsPangoFontFamilyClass))

typedef struct _PangoFontFace        NsPangoFontFace;
typedef struct _PangoFontFamily      NsPangoFontFamily;
typedef struct _PangoFontFamilyClass NsPangoFontFamilyClass;

#ifndef NS_PANGO_DISABLE_DEPRECATED

/**
 * NsPangoFontFamily:
 *
 * A `NsPangoFontFamily` is used to represent a family of related
 * font faces.
 *
 * The font faces in a family share a common design, but differ in
 * slant, weight, width or other aspects.
 */
struct _PangoFontFamily
{
  GObject parent_instance;
};

struct _PangoFontFamilyClass
{
  GObjectClass parent_class;

  /*< public >*/

  void  (*list_faces)      (NsPangoFontFamily  *family,
                            NsPangoFontFace  ***faces,
                            int              *n_faces);
  const char * (*get_name) (NsPangoFontFamily  *family);
  gboolean (*is_monospace) (NsPangoFontFamily *family);
  gboolean (*is_variable)  (NsPangoFontFamily *family);

  NsPangoFontFace * (*get_face) (NsPangoFontFamily *family,
                               const char      *name);


  /*< private >*/

  /* Padding for future expansion */
  void (*_ns_pango_reserved2) (void);
};

#endif /* NS_PANGO_DISABLE_DEPRECATED */

NS_PANGO_AVAILABLE_IN_ALL
GType      ns_pango_font_family_get_type       (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_ALL
void                 ns_pango_font_family_list_faces (NsPangoFontFamily  *family,
                                                   NsPangoFontFace  ***faces,
                                                   int              *n_faces);
NS_PANGO_AVAILABLE_IN_ALL
const char *ns_pango_font_family_get_name   (NsPangoFontFamily  *family) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_1_4
gboolean   ns_pango_font_family_is_monospace         (NsPangoFontFamily  *family) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_1_44
gboolean   ns_pango_font_family_is_variable          (NsPangoFontFamily  *family) G_GNUC_PURE;

NS_PANGO_AVAILABLE_IN_1_46
NsPangoFontFace *ns_pango_font_family_get_face (NsPangoFontFamily *family,
                                           const char      *name);


/*
 * NsPangoFontFace
 */

#define NS_TYPE_PANGO_FONT_FACE              (ns_pango_font_face_get_type ())
#define NS_PANGO_FONT_FACE(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_FONT_FACE, NsPangoFontFace))
#define NS_PANGO_IS_FONT_FACE(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_FONT_FACE))
#define NS_PANGO_FONT_FACE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), NS_TYPE_PANGO_FONT_FACE, NsPangoFontFaceClass))
#define NS_PANGO_IS_FONT_FACE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), NS_TYPE_PANGO_FONT_FACE))
#define NS_PANGO_FONT_FACE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), NS_TYPE_PANGO_FONT_FACE, NsPangoFontFaceClass))

typedef struct _PangoFontFaceClass   NsPangoFontFaceClass;

#ifndef NS_PANGO_DISABLE_DEPRECATED

/**
 * NsPangoFontFace:
 *
 * A `NsPangoFontFace` is used to represent a group of fonts with
 * the same family, slant, weight, and width, but varying sizes.
 */
struct _PangoFontFace
{
  GObject parent_instance;
};

struct _PangoFontFaceClass
{
  GObjectClass parent_class;

  /*< public >*/

  const char           * (*get_face_name)  (NsPangoFontFace *face);
  NsPangoFontDescription * (*describe)       (NsPangoFontFace *face);
  void                   (*list_sizes)     (NsPangoFontFace  *face,
                                            int           **sizes,
                                            int            *n_sizes);
  gboolean               (*is_synthesized) (NsPangoFontFace *face);
  NsPangoFontFamily *      (*get_family)     (NsPangoFontFace *face);

  /*< private >*/

  /* Padding for future expansion */
  void (*_ns_pango_reserved3) (void);
  void (*_ns_pango_reserved4) (void);
};

#endif /* NS_PANGO_DISABLE_DEPRECATED */

NS_PANGO_AVAILABLE_IN_ALL
GType      ns_pango_font_face_get_type       (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontDescription *ns_pango_font_face_describe       (NsPangoFontFace  *face);
NS_PANGO_AVAILABLE_IN_ALL
const char           *ns_pango_font_face_get_face_name  (NsPangoFontFace  *face) G_GNUC_PURE;
NS_PANGO_AVAILABLE_IN_1_4
void                  ns_pango_font_face_list_sizes     (NsPangoFontFace  *face,
                                                      int           **sizes,
                                                      int            *n_sizes);
NS_PANGO_AVAILABLE_IN_1_18
gboolean              ns_pango_font_face_is_synthesized (NsPangoFontFace  *face) G_GNUC_PURE;

NS_PANGO_AVAILABLE_IN_1_46
NsPangoFontFamily *     ns_pango_font_face_get_family     (NsPangoFontFace  *face);


/*
 * NsPangoFont
 */

#define NS_TYPE_PANGO_FONT              (ns_pango_font_get_type ())
#define NS_PANGO_FONT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), NS_TYPE_PANGO_FONT, NsPangoFont))
#define NS_PANGO_IS_FONT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), NS_TYPE_PANGO_FONT))
#define NS_PANGO_FONT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), NS_TYPE_PANGO_FONT, NsPangoFontClass))
#define NS_PANGO_IS_FONT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), NS_TYPE_PANGO_FONT))
#define NS_PANGO_FONT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), NS_TYPE_PANGO_FONT, NsPangoFontClass))


#ifndef NS_PANGO_DISABLE_DEPRECATED

/**
 * NsPangoFont:
 *
 * A `NsPangoFont` is used to represent a font in a
 * rendering-system-independent manner.
 */
struct _PangoFont
{
  GObject parent_instance;
};

typedef struct _PangoFontClass       NsPangoFontClass;
struct _PangoFontClass
{
  GObjectClass parent_class;

  /*< public >*/

  NsPangoFontDescription *(*describe)           (NsPangoFont      *font);
  NsPangoCoverage *       (*get_coverage)       (NsPangoFont      *font,
                                               NsPangoLanguage  *language);
  void                  (*get_glyph_extents)  (NsPangoFont      *font,
                                               NsPangoGlyph      glyph,
                                               NsPangoRectangle *ink_rect,
                                               NsPangoRectangle *logical_rect);
  NsPangoFontMetrics *    (*get_metrics)        (NsPangoFont      *font,
                                               NsPangoLanguage  *language);
  NsPangoFontMap *        (*get_font_map)       (NsPangoFont      *font);
  NsPangoFontDescription *(*describe_absolute)  (NsPangoFont      *font);
  void                  (*get_features)       (NsPangoFont      *font,
                                               hb_feature_t   *features,
                                               guint           len,
                                               guint          *num_features);
  hb_font_t *           (*create_hb_font)     (NsPangoFont      *font);
};

#endif /* NS_PANGO_DISABLE_DEPRECATED */

NS_PANGO_AVAILABLE_IN_ALL
GType                 ns_pango_font_get_type          (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontDescription *ns_pango_font_describe          (NsPangoFont        *font);
NS_PANGO_AVAILABLE_IN_1_14
NsPangoFontDescription *ns_pango_font_describe_with_absolute_size (NsPangoFont        *font);
NS_PANGO_AVAILABLE_IN_ALL
NsPangoCoverage *       ns_pango_font_get_coverage      (NsPangoFont        *font,
                                                    NsPangoLanguage    *language);
#ifndef __GI_SCANNER__
NS_PANGO_DEPRECATED_IN_1_44
NsPangoEngineShape *    ns_pango_font_find_shaper       (NsPangoFont        *font,
                                                    NsPangoLanguage    *language,
                                                    guint32           ch);
#endif
NS_PANGO_AVAILABLE_IN_ALL
NsPangoFontMetrics *    ns_pango_font_get_metrics       (NsPangoFont        *font,
                                                    NsPangoLanguage    *language);
NS_PANGO_AVAILABLE_IN_ALL
void                  ns_pango_font_get_glyph_extents (NsPangoFont        *font,
                                                    NsPangoGlyph        glyph,
                                                    NsPangoRectangle   *ink_rect,
                                                    NsPangoRectangle   *logical_rect);
NS_PANGO_AVAILABLE_IN_1_10
NsPangoFontMap         *ns_pango_font_get_font_map      (NsPangoFont        *font);

NS_PANGO_AVAILABLE_IN_1_46
NsPangoFontFace *       ns_pango_font_get_face          (NsPangoFont        *font);

NS_PANGO_AVAILABLE_IN_1_44
gboolean              ns_pango_font_has_char          (NsPangoFont        *font,
                                                    gunichar          wc);
NS_PANGO_AVAILABLE_IN_1_44
void                  ns_pango_font_get_features      (NsPangoFont        *font,
                                                    hb_feature_t     *features,
                                                    guint             len,
                                                    guint            *num_features);
NS_PANGO_AVAILABLE_IN_1_44
hb_font_t *           ns_pango_font_get_hb_font       (NsPangoFont        *font);

NS_PANGO_AVAILABLE_IN_1_50
NsPangoLanguage **      ns_pango_font_get_languages     (NsPangoFont        *font);

/**
 * NS_PANGO_GLYPH_EMPTY:
 *
 * A `NsPangoGlyph` value that indicates a zero-width empty glpyh.
 *
 * This is useful for example in shaper modules, to use as the glyph for
 * various zero-width Unicode characters (those passing [func@is_zero_width]).
 */

/**
 * NS_PANGO_GLYPH_INVALID_INPUT:
 *
 * A `NsPangoGlyph` value for invalid input.
 *
 * `NsPangoLayout` produces one such glyph per invalid input UTF-8 byte and such
 * a glyph is rendered as a crossed box.
 *
 * Note that this value is defined such that it has the %NS_PANGO_GLYPH_UNKNOWN_FLAG
 * set.
 *
 * Since: 1.20
 */
/**
 * NS_PANGO_GLYPH_UNKNOWN_FLAG:
 *
 * Flag used in `NsPangoGlyph` to turn a `gunichar` value of a valid Unicode
 * character into an unknown-character glyph for that `gunichar`.
 *
 * Such unknown-character glyphs may be rendered as a 'hex box'.
 */
/**
 * NS_PANGO_GET_UNKNOWN_GLYPH:
 * @wc: a Unicode character
 *
 * The way this unknown glyphs are rendered is backend specific. For example,
 * a box with the hexadecimal Unicode code-point of the character written in it
 * is what is done in the most common backends.
 *
 * Returns: a `NsPangoGlyph` value that means no glyph was found for @wc.
 */
#define NS_PANGO_GLYPH_EMPTY           ((NsPangoGlyph)0x0FFFFFFF)
#define NS_PANGO_GLYPH_INVALID_INPUT   ((NsPangoGlyph)0xFFFFFFFF)
#define NS_PANGO_GLYPH_UNKNOWN_FLAG    ((NsPangoGlyph)0x10000000)
#define NS_PANGO_GET_UNKNOWN_GLYPH(wc) ((NsPangoGlyph)(wc)|NS_PANGO_GLYPH_UNKNOWN_FLAG)

#ifndef __GI_SCANNER__
#ifndef NS_PANGO_DISABLE_DEPRECATED
#define NS_PANGO_UNKNOWN_GLYPH_WIDTH  10
#define NS_PANGO_UNKNOWN_GLYPH_HEIGHT 14
#endif
#endif

G_DEFINE_AUTOPTR_CLEANUP_FUNC(NsPangoFontFamily, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(NsPangoFontFace, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(NsPangoFont, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(NsPangoFontDescription, ns_pango_font_description_free)

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NsPangoFontMetrics, ns_pango_font_metrics_unref)

G_END_DECLS

#endif /* __NS_PANGO_FONT_H__ */
