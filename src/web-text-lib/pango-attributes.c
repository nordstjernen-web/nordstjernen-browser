/* Pango
 * pango-attributes.c: Attributed text
 *
 * Copyright (C) 2000-2002 Red Hat Software
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

#include "config.h"
#include <string.h>

#include "pango-attributes.h"
#include "pango-attributes-private.h"
#include "pango-impl-utils.h"


/* {{{ Generic attribute code */


/**
 * ns_pango_attribute_init:
 * @attr: a `NsPangoAttribute`
 * @klass: a `NsPangoAttrClass`
 *
 * Initializes @attr's klass to @klass, it's start_index to
 * %NS_PANGO_ATTR_INDEX_FROM_TEXT_BEGINNING and end_index to
 * %NS_PANGO_ATTR_INDEX_TO_TEXT_END such that the attribute applies
 * to the entire text by default.
 *
 * Since: 1.20
 */
void
ns_pango_attribute_init (NsPangoAttribute       *attr,
                      const NsPangoAttrClass *klass)
{
  g_return_if_fail (attr != NULL);
  g_return_if_fail (klass != NULL);

  attr->klass = klass;
  attr->start_index = NS_PANGO_ATTR_INDEX_FROM_TEXT_BEGINNING;
  attr->end_index   = NS_PANGO_ATTR_INDEX_TO_TEXT_END;
}

/**
 * ns_pango_attribute_copy:
 * @attr: a `NsPangoAttribute`
 *
 * Make a copy of an attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy].
 */
NsPangoAttribute *
ns_pango_attribute_copy (const NsPangoAttribute *attr)
{
  NsPangoAttribute *result;

  g_return_val_if_fail (attr != NULL, NULL);

  result = attr->klass->copy (attr);
  result->start_index = attr->start_index;
  result->end_index = attr->end_index;

  return result;
}

/**
 * ns_pango_attribute_destroy:
 * @attr: a `NsPangoAttribute`.
 *
 * Destroy a `NsPangoAttribute` and free all associated memory.
 */
void
ns_pango_attribute_destroy (NsPangoAttribute *attr)
{
  g_return_if_fail (attr != NULL);

  attr->klass->destroy (attr);
}

/**
 * ns_pango_attribute_equal:
 * @attr1: a `NsPangoAttribute`
 * @attr2: another `NsPangoAttribute`
 *
 * Compare two attributes for equality.
 *
 * This compares only the actual value of the two
 * attributes and not the ranges that the attributes
 * apply to.
 *
 * Return value: %TRUE if the two attributes have the same value
 */
gboolean
ns_pango_attribute_equal (const NsPangoAttribute *attr1,
                       const NsPangoAttribute *attr2)
{
  g_return_val_if_fail (attr1 != NULL, FALSE);
  g_return_val_if_fail (attr2 != NULL, FALSE);

  if (attr1->klass->type != attr2->klass->type)
    return FALSE;

  return attr1->klass->equal (attr1, attr2);
}

/* }}} */
/* {{{ Attribute types */
/* {{{ String attribute */
static NsPangoAttribute *ns_pango_attr_string_new (const NsPangoAttrClass *klass,
                                              const char           *str);

static NsPangoAttribute *
ns_pango_attr_string_copy (const NsPangoAttribute *attr)
{
  return ns_pango_attr_string_new (attr->klass, ((NsPangoAttrString *)attr)->value);
}

static void
ns_pango_attr_string_destroy (NsPangoAttribute *attr)
{
  NsPangoAttrString *sattr = (NsPangoAttrString *)attr;

  g_free (sattr->value);
  g_slice_free (NsPangoAttrString, sattr);
}

static gboolean
ns_pango_attr_string_equal (const NsPangoAttribute *attr1,
                         const NsPangoAttribute *attr2)
{
  return strcmp (((NsPangoAttrString *)attr1)->value, ((NsPangoAttrString *)attr2)->value) == 0;
}

static NsPangoAttribute *
ns_pango_attr_string_new (const NsPangoAttrClass *klass,
                       const char           *str)
{
  NsPangoAttrString *result = g_slice_new (NsPangoAttrString);
  ns_pango_attribute_init (&result->attr, klass);
  result->value = g_strdup (str);

  return (NsPangoAttribute *)result;
}
 /* }}} */
/* {{{ Language attribute */
static NsPangoAttribute *
ns_pango_attr_language_copy (const NsPangoAttribute *attr)
{
  return ns_pango_attr_language_new (((NsPangoAttrLanguage *)attr)->value);
}

static void
ns_pango_attr_language_destroy (NsPangoAttribute *attr)
{
  NsPangoAttrLanguage *lattr = (NsPangoAttrLanguage *)attr;

  g_slice_free (NsPangoAttrLanguage, lattr);
}

static gboolean
ns_pango_attr_language_equal (const NsPangoAttribute *attr1,
                           const NsPangoAttribute *attr2)
{
  return ((NsPangoAttrLanguage *)attr1)->value == ((NsPangoAttrLanguage *)attr2)->value;
}
/* }}}} */
/* {{{ Color attribute */
static NsPangoAttribute *ns_pango_attr_color_new (const NsPangoAttrClass *klass,
                                             guint16               red,
                                             guint16               green,
                                             guint16               blue);

static NsPangoAttribute *
ns_pango_attr_color_copy (const NsPangoAttribute *attr)
{
  const NsPangoAttrColor *color_attr = (NsPangoAttrColor *)attr;

  return ns_pango_attr_color_new (attr->klass,
                               color_attr->color.red,
                               color_attr->color.green,
                               color_attr->color.blue);
}

static void
ns_pango_attr_color_destroy (NsPangoAttribute *attr)
{
  NsPangoAttrColor *cattr = (NsPangoAttrColor *)attr;

  g_slice_free (NsPangoAttrColor, cattr);
}

static gboolean
ns_pango_attr_color_equal (const NsPangoAttribute *attr1,
                        const NsPangoAttribute *attr2)
{
  const NsPangoAttrColor *color_attr1 = (const NsPangoAttrColor *)attr1;
  const NsPangoAttrColor *color_attr2 = (const NsPangoAttrColor *)attr2;

  return (color_attr1->color.red == color_attr2->color.red &&
          color_attr1->color.blue == color_attr2->color.blue &&
          color_attr1->color.green == color_attr2->color.green);
}

static NsPangoAttribute *
ns_pango_attr_color_new (const NsPangoAttrClass *klass,
                      guint16               red,
                      guint16               green,
                      guint16               blue)
{
  NsPangoAttrColor *result = g_slice_new (NsPangoAttrColor);
  ns_pango_attribute_init (&result->attr, klass);
  result->color.red = red;
  result->color.green = green;
  result->color.blue = blue;

  return (NsPangoAttribute *)result;
}
/* }}}} */
/* {{{ Integer attribute */
static NsPangoAttribute *ns_pango_attr_int_new (const NsPangoAttrClass *klass,
                                           int                   value);

static NsPangoAttribute *
ns_pango_attr_int_copy (const NsPangoAttribute *attr)
{
  const NsPangoAttrInt *int_attr = (NsPangoAttrInt *)attr;

  return ns_pango_attr_int_new (attr->klass, int_attr->value);
}

static void
ns_pango_attr_int_destroy (NsPangoAttribute *attr)
{
  NsPangoAttrInt *iattr = (NsPangoAttrInt *)attr;

  g_slice_free (NsPangoAttrInt, iattr);
}

static gboolean
ns_pango_attr_int_equal (const NsPangoAttribute *attr1,
                      const NsPangoAttribute *attr2)
{
  const NsPangoAttrInt *int_attr1 = (const NsPangoAttrInt *)attr1;
  const NsPangoAttrInt *int_attr2 = (const NsPangoAttrInt *)attr2;

  return (int_attr1->value == int_attr2->value);
}

static NsPangoAttribute *
ns_pango_attr_int_new (const NsPangoAttrClass *klass,
                    int                   value)
{
  NsPangoAttrInt *result = g_slice_new (NsPangoAttrInt);
  ns_pango_attribute_init (&result->attr, klass);
  result->value = value;

  return (NsPangoAttribute *)result;
}
/* }}} */
/* {{{ Float attribute */

/* }}} */
/* {{{ Size attribute */
static NsPangoAttribute *ns_pango_attr_size_new_internal (int      size,
                                                     gboolean absolute);

static NsPangoAttribute *
ns_pango_attr_size_copy (const NsPangoAttribute *attr)
{
  const NsPangoAttrSize *size_attr = (NsPangoAttrSize *)attr;

  if (attr->klass->type == NS_PANGO_ATTR_ABSOLUTE_SIZE)
    return ns_pango_attr_size_new_absolute (size_attr->size);
  else
    return ns_pango_attr_size_new (size_attr->size);
}

static void
ns_pango_attr_size_destroy (NsPangoAttribute *attr)
{
  NsPangoAttrSize *sattr = (NsPangoAttrSize *)attr;

  g_slice_free (NsPangoAttrSize, sattr);
}

static gboolean
ns_pango_attr_size_equal (const NsPangoAttribute *attr1,
                       const NsPangoAttribute *attr2)
{
  const NsPangoAttrSize *size_attr1 = (const NsPangoAttrSize *)attr1;
  const NsPangoAttrSize *size_attr2 = (const NsPangoAttrSize *)attr2;

  return size_attr1->size == size_attr2->size;
}

static NsPangoAttribute *
ns_pango_attr_size_new_internal (int size,
                              gboolean absolute)
{
  NsPangoAttrSize *result;

  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_SIZE,
    ns_pango_attr_size_copy,
    ns_pango_attr_size_destroy,
    ns_pango_attr_size_equal
  };
  static const NsPangoAttrClass absolute_klass = {
    NS_PANGO_ATTR_ABSOLUTE_SIZE,
    ns_pango_attr_size_copy,
    ns_pango_attr_size_destroy,
    ns_pango_attr_size_equal
  };

  result = g_slice_new (NsPangoAttrSize);
  ns_pango_attribute_init (&result->attr, absolute ? &absolute_klass : &klass);
  result->size = size;
  result->absolute = absolute;

  return (NsPangoAttribute *)result;
}
/* }}} */
/* {{{ Font description attribute */
static NsPangoAttribute *
ns_pango_attr_font_desc_copy (const NsPangoAttribute *attr)
{
  const NsPangoAttrFontDesc *desc_attr = (const NsPangoAttrFontDesc *)attr;

  return ns_pango_attr_font_desc_new (desc_attr->desc);
}

static void
ns_pango_attr_font_desc_destroy (NsPangoAttribute *attr)
{
  NsPangoAttrFontDesc *desc_attr = (NsPangoAttrFontDesc *)attr;

  ns_pango_font_description_free (desc_attr->desc);
  g_slice_free (NsPangoAttrFontDesc, desc_attr);
}

static gboolean
ns_pango_attr_font_desc_equal (const NsPangoAttribute *attr1,
                            const NsPangoAttribute *attr2)
{
  const NsPangoAttrFontDesc *desc_attr1 = (const NsPangoAttrFontDesc *)attr1;
  const NsPangoAttrFontDesc *desc_attr2 = (const NsPangoAttrFontDesc *)attr2;

  return ns_pango_font_description_get_set_fields (desc_attr1->desc) ==
         ns_pango_font_description_get_set_fields (desc_attr2->desc) &&
         ns_pango_font_description_equal (desc_attr1->desc, desc_attr2->desc);
}
/* }}} */
/* {{{ Shape attribute */
static NsPangoAttribute *
ns_pango_attr_shape_copy (const NsPangoAttribute *attr)
{
  const NsPangoAttrShape *shape_attr = (NsPangoAttrShape *)attr;
  gpointer data;

  if (shape_attr->copy_func)
    data = shape_attr->copy_func (shape_attr->data);
  else
    data = shape_attr->data;

  return ns_pango_attr_shape_new_with_data (&shape_attr->ink_rect, &shape_attr->logical_rect,
                                         data, shape_attr->copy_func, shape_attr->destroy_func);
}

static void
ns_pango_attr_shape_destroy (NsPangoAttribute *attr)
{
  NsPangoAttrShape *shape_attr = (NsPangoAttrShape *)attr;

  if (shape_attr->destroy_func)
    shape_attr->destroy_func (shape_attr->data);

  g_slice_free (NsPangoAttrShape, shape_attr);
}

static gboolean
ns_pango_attr_shape_equal (const NsPangoAttribute *attr1,
                        const NsPangoAttribute *attr2)
{
  const NsPangoAttrShape *shape_attr1 = (const NsPangoAttrShape *)attr1;
  const NsPangoAttrShape *shape_attr2 = (const NsPangoAttrShape *)attr2;

  return (shape_attr1->logical_rect.x == shape_attr2->logical_rect.x &&
          shape_attr1->logical_rect.y == shape_attr2->logical_rect.y &&
          shape_attr1->logical_rect.width == shape_attr2->logical_rect.width &&
          shape_attr1->logical_rect.height == shape_attr2->logical_rect.height &&
          shape_attr1->ink_rect.x == shape_attr2->ink_rect.x &&
          shape_attr1->ink_rect.y == shape_attr2->ink_rect.y &&
          shape_attr1->ink_rect.width == shape_attr2->ink_rect.width &&
          shape_attr1->ink_rect.height == shape_attr2->ink_rect.height &&
          shape_attr1->data == shape_attr2->data);
}
/* }}} */
/* }}} */
/* {{{ Public API */

/**
 * ns_pango_attr_family_new:
 * @family: the family or comma-separated list of families
 *
 * Create a new font family attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 */
NsPangoAttribute *
ns_pango_attr_family_new (const char *family)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_FAMILY,
    ns_pango_attr_string_copy,
    ns_pango_attr_string_destroy,
    ns_pango_attr_string_equal
  };

  g_return_val_if_fail (family != NULL, NULL);

  return ns_pango_attr_string_new (&klass, family);
}

/**
 * ns_pango_attr_language_new:
 * @language: language tag
 *
 * Create a new language tag attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 */
NsPangoAttribute *
ns_pango_attr_language_new (NsPangoLanguage *language)
{
  NsPangoAttrLanguage *result;

  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_LANGUAGE,
    ns_pango_attr_language_copy,
    ns_pango_attr_language_destroy,
    ns_pango_attr_language_equal
  };

  result = g_slice_new (NsPangoAttrLanguage);
  ns_pango_attribute_init (&result->attr, &klass);
  result->value = language;

  return (NsPangoAttribute *)result;
}

/**
 * ns_pango_attr_foreground_new:
 * @red: the red value (ranging from 0 to 65535)
 * @green: the green value
 * @blue: the blue value
 *
 * Create a new foreground color attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 */
NsPangoAttribute *
ns_pango_attr_foreground_new (guint16 red,
                           guint16 green,
                           guint16 blue)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_FOREGROUND,
    ns_pango_attr_color_copy,
    ns_pango_attr_color_destroy,
    ns_pango_attr_color_equal
  };

  return ns_pango_attr_color_new (&klass, red, green, blue);
}

/**
 * ns_pango_attr_background_new:
 * @red: the red value (ranging from 0 to 65535)
 * @green: the green value
 * @blue: the blue value
 *
 * Create a new background color attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 */
NsPangoAttribute *
ns_pango_attr_background_new (guint16 red,
                           guint16 green,
                           guint16 blue)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_BACKGROUND,
    ns_pango_attr_color_copy,
    ns_pango_attr_color_destroy,
    ns_pango_attr_color_equal
  };

  return ns_pango_attr_color_new (&klass, red, green, blue);
}

/**
 * ns_pango_attr_size_new:
 * @size: the font size, in %NS_PANGO_SCALE-ths of a point
 *
 * Create a new font-size attribute in fractional points.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 */
NsPangoAttribute *
ns_pango_attr_size_new (int size)
{
  return ns_pango_attr_size_new_internal (size, FALSE);
}

/**
 * ns_pango_attr_size_new_absolute:
 * @size: the font size, in %NS_PANGO_SCALE-ths of a device unit
 *
 * Create a new font-size attribute in device units.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.8
 */
NsPangoAttribute *
ns_pango_attr_size_new_absolute (int size)
{
  return ns_pango_attr_size_new_internal (size, TRUE);
}

/**
 * ns_pango_attr_style_new:
 * @style: the slant style
 *
 * Create a new font slant style attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 */
NsPangoAttribute *
ns_pango_attr_style_new (NsPangoStyle style)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_STYLE,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, (int)style);
}

/**
 * ns_pango_attr_weight_new:
 * @weight: the weight
 *
 * Create a new font weight attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 */
NsPangoAttribute *
ns_pango_attr_weight_new (NsPangoWeight weight)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_WEIGHT,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, (int)weight);
}

/**
 * ns_pango_attr_variant_new:
 * @variant: the variant
 *
 * Create a new font variant attribute (normal or small caps).
 *
 * Return value: (transfer full): the newly allocated `NsPangoAttribute`,
 *   which should be freed with [method@Pango.Attribute.destroy].
 */
NsPangoAttribute *
ns_pango_attr_variant_new (NsPangoVariant variant)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_VARIANT,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, (int)variant);
}

/**
 * ns_pango_attr_stretch_new:
 * @stretch: the stretch
 *
 * Create a new font stretch attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 */
NsPangoAttribute *
ns_pango_attr_stretch_new (NsPangoStretch  stretch)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_STRETCH,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, (int)stretch);
}

/**
 * ns_pango_attr_font_desc_new:
 * @desc: the font description
 *
 * Create a new font description attribute.
 *
 * This attribute allows setting family, style, weight, variant,
 * stretch, and size simultaneously.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 */
NsPangoAttribute *
ns_pango_attr_font_desc_new (const NsPangoFontDescription *desc)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_FONT_DESC,
    ns_pango_attr_font_desc_copy,
    ns_pango_attr_font_desc_destroy,
    ns_pango_attr_font_desc_equal
  };

  NsPangoAttrFontDesc *result = g_slice_new (NsPangoAttrFontDesc);
  ns_pango_attribute_init (&result->attr, &klass);
  result->desc = ns_pango_font_description_copy (desc);

  return (NsPangoAttribute *)result;
}

/**
 * ns_pango_attr_underline_new:
 * @underline: the underline style
 *
 * Create a new underline-style attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 */
NsPangoAttribute *
ns_pango_attr_underline_new (NsPangoUnderline underline)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_UNDERLINE,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, (int)underline);
}

/**
 * ns_pango_attr_underline_color_new:
 * @red: the red value (ranging from 0 to 65535)
 * @green: the green value
 * @blue: the blue value
 *
 * Create a new underline color attribute.
 *
 * This attribute modifies the color of underlines.
 * If not set, underlines will use the foreground color.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.8
 */
NsPangoAttribute *
ns_pango_attr_underline_color_new (guint16 red,
                                guint16 green,
                                guint16 blue)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_UNDERLINE_COLOR,
    ns_pango_attr_color_copy,
    ns_pango_attr_color_destroy,
    ns_pango_attr_color_equal
  };

  return ns_pango_attr_color_new (&klass, red, green, blue);
}

/**
 * ns_pango_attr_strikethrough_new:
 * @strikethrough: %TRUE if the text should be struck-through
 *
 * Create a new strike-through attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 */
NsPangoAttribute *
ns_pango_attr_strikethrough_new (gboolean strikethrough)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_STRIKETHROUGH,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, (int)strikethrough);
}

/**
 * ns_pango_attr_strikethrough_color_new:
 * @red: the red value (ranging from 0 to 65535)
 * @green: the green value
 * @blue: the blue value
 *
 * Create a new strikethrough color attribute.
 *
 * This attribute modifies the color of strikethrough lines.
 * If not set, strikethrough lines will use the foreground color.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.8
 */
NsPangoAttribute *
ns_pango_attr_strikethrough_color_new (guint16 red,
                                    guint16 green,
                                    guint16 blue)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_STRIKETHROUGH_COLOR,
    ns_pango_attr_color_copy,
    ns_pango_attr_color_destroy,
    ns_pango_attr_color_equal
  };

  return ns_pango_attr_color_new (&klass, red, green, blue);
}

/**
 * ns_pango_attr_rise_new:
 * @rise: the amount that the text should be displaced vertically,
 *   in Pango units. Positive values displace the text upwards.
 *
 * Create a new baseline displacement attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 */
NsPangoAttribute *
ns_pango_attr_rise_new (int rise)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_RISE,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, (int)rise);
}

/**
 * ns_pango_attr_font_scale_new:
 * @scale: a `NsPangoFontScale` value, which indicates font size change relative
 *   to the size of the previous run.
 *
 *
 * Create a new font scale attribute.
 *
 * The effect of this attribute is to change the font size of a run,
 * relative to the size of preceding run.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.50
 */
NsPangoAttribute *
ns_pango_attr_font_scale_new (NsPangoFontScale scale)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_FONT_SCALE,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, (int)scale);
}

/**
 * ns_pango_attr_fallback_new:
 * @enable_fallback: %TRUE if we should fall back on other fonts
 *   for characters the active font is missing
 *
 * Create a new font fallback attribute.
 *
 * If fallback is disabled, characters will only be
 * used from the closest matching font on the system.
 * No fallback will be done to other fonts on the system
 * that might contain the characters in the text.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.4
 */
NsPangoAttribute *
ns_pango_attr_fallback_new (gboolean enable_fallback)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_FALLBACK,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal,
  };

  return ns_pango_attr_int_new (&klass, (int)enable_fallback);
}

/**
 * ns_pango_attr_letter_spacing_new:
 * @letter_spacing: amount of extra space to add between
 *   graphemes of the text, in Pango units
 *
 * Create a new letter-spacing attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.6
 */
NsPangoAttribute *
ns_pango_attr_letter_spacing_new (int letter_spacing)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_LETTER_SPACING,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, letter_spacing);
}

/**
 * ns_pango_attr_shape_new_with_data:
 * @ink_rect: ink rectangle to assign to each character
 * @logical_rect: logical rectangle to assign to each character
 * @data: user data pointer
 * @copy_func: (nullable): function to copy @data when the
 *   attribute is copied. If %NULL, @data is simply copied
 *   as a pointer
 * @destroy_func: (nullable): function to free @data when the
 *   attribute is freed
 *
 * Creates a new shape attribute.
 *
 * Like [func@Pango.AttrShape.new], but a user data pointer
 * is also provided; this pointer can be accessed when later
 * rendering the glyph.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.8
 */
NsPangoAttribute *
ns_pango_attr_shape_new_with_data (const NsPangoRectangle  *ink_rect,
                                const NsPangoRectangle  *logical_rect,
                                gpointer               data,
                                NsPangoAttrDataCopyFunc  copy_func,
                                GDestroyNotify         destroy_func)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_SHAPE,
    ns_pango_attr_shape_copy,
    ns_pango_attr_shape_destroy,
    ns_pango_attr_shape_equal
  };

  NsPangoAttrShape *result;

  g_return_val_if_fail (ink_rect != NULL, NULL);
  g_return_val_if_fail (logical_rect != NULL, NULL);

  result = g_slice_new (NsPangoAttrShape);
  ns_pango_attribute_init (&result->attr, &klass);
  result->ink_rect = *ink_rect;
  result->logical_rect = *logical_rect;
  result->data = data;
  result->copy_func = copy_func;
  result->destroy_func =  destroy_func;

  return (NsPangoAttribute *)result;
}

/**
 * ns_pango_attr_shape_new:
 * @ink_rect: ink rectangle to assign to each character
 * @logical_rect: logical rectangle to assign to each character
 *
 * Create a new shape attribute.
 *
 * A shape is used to impose a particular ink and logical
 * rectangle on the result of shaping a particular glyph.
 * This might be used, for instance, for embedding a picture
 * or a widget inside a `NsPangoLayout`.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 */
NsPangoAttribute *
ns_pango_attr_shape_new (const NsPangoRectangle *ink_rect,
                      const NsPangoRectangle *logical_rect)
{
  g_return_val_if_fail (ink_rect != NULL, NULL);
  g_return_val_if_fail (logical_rect != NULL, NULL);

  return ns_pango_attr_shape_new_with_data (ink_rect, logical_rect,
                                         NULL, NULL, NULL);
}

/**
 * ns_pango_attr_font_features_new:
 * @features: a string with OpenType font features, with the syntax of the [CSS
 * font-feature-settings property](https://www.w3.org/TR/css-fonts-4/#font-rend-desc)
 *
 * Create a new font features tag attribute.
 *
 * You can use this attribute to select OpenType font features like small-caps,
 * alternative glyphs, ligatures, etc. for fonts that support them.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.38
 */
NsPangoAttribute *
ns_pango_attr_font_features_new (const gchar *features)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_FONT_FEATURES,
    ns_pango_attr_string_copy,
    ns_pango_attr_string_destroy,
    ns_pango_attr_string_equal
  };

  g_return_val_if_fail (features != NULL, NULL);

  return ns_pango_attr_string_new (&klass, features);
}

/**
 * ns_pango_attr_foreground_alpha_new:
 * @alpha: the alpha value, between 1 and 65536
 *
 * Create a new foreground alpha attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.38
 */
NsPangoAttribute *
ns_pango_attr_foreground_alpha_new (guint16 alpha)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_FOREGROUND_ALPHA,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, (int)alpha);
}

/**
 * ns_pango_attr_background_alpha_new:
 * @alpha: the alpha value, between 1 and 65536
 *
 * Create a new background alpha attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.38
 */
NsPangoAttribute *
ns_pango_attr_background_alpha_new (guint16 alpha)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_BACKGROUND_ALPHA,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, (int)alpha);
}

/**
 * ns_pango_attr_allow_breaks_new:
 * @allow_breaks: %TRUE if we line breaks are allowed
 *
 * Create a new allow-breaks attribute.
 *
 * If breaks are disabled, the range will be kept in a
 * single run, as far as possible.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.44
 */
NsPangoAttribute *
ns_pango_attr_allow_breaks_new (gboolean allow_breaks)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_ALLOW_BREAKS,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal,
  };

  return ns_pango_attr_int_new (&klass, (int)allow_breaks);
}

/**
 * ns_pango_attr_insert_hyphens_new:
 * @insert_hyphens: %TRUE if hyphens should be inserted
 *
 * Create a new insert-hyphens attribute.
 *
 * Pango will insert hyphens when breaking lines in
 * the middle of a word. This attribute can be used
 * to suppress the hyphen.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.44
 */
NsPangoAttribute *
ns_pango_attr_insert_hyphens_new (gboolean insert_hyphens)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_INSERT_HYPHENS,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal,
  };

  return ns_pango_attr_int_new (&klass, (int)insert_hyphens);
}

/**
 * ns_pango_attr_show_new:
 * @flags: `NsPangoShowFlags` to apply
 *
 * Create a new attribute that influences how invisible
 * characters are rendered.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.44
 **/
NsPangoAttribute *
ns_pango_attr_show_new (NsPangoShowFlags flags)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_SHOW,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal,
  };

  return ns_pango_attr_int_new (&klass, (int)flags);
}

/**
 * ns_pango_attr_overline_new:
 * @overline: the overline style
 *
 * Create a new overline-style attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.46
 */
NsPangoAttribute *
ns_pango_attr_overline_new (NsPangoOverline overline)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_OVERLINE,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, (int)overline);
}

/**
 * ns_pango_attr_overline_color_new:
 * @red: the red value (ranging from 0 to 65535)
 * @green: the green value
 * @blue: the blue value
 *
 * Create a new overline color attribute.
 *
 * This attribute modifies the color of overlines.
 * If not set, overlines will use the foreground color.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.46
 */
NsPangoAttribute *
ns_pango_attr_overline_color_new (guint16 red,
                               guint16 green,
                               guint16 blue)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_OVERLINE_COLOR,
    ns_pango_attr_color_copy,
    ns_pango_attr_color_destroy,
    ns_pango_attr_color_equal
  };

  return ns_pango_attr_color_new (&klass, red, green, blue);
}

/**
 * ns_pango_attr_line_height_new_absolute:
 * @height: the line height, in %NS_PANGO_SCALE-ths of a point
 *
 * Override the height of logical line extents to be @height.
 *
 * This affects the values returned by
 * [method@Pango.LayoutLine.get_extents],
 * [method@Pango.LayoutLine.get_pixel_extents] and
 * [method@Pango.LayoutIter.get_line_extents].
 *
 * Since: 1.50
 */
NsPangoAttribute *
ns_pango_attr_line_height_new_absolute (int height)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_ABSOLUTE_LINE_HEIGHT,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, height);
}

/**
 * ns_pango_attr_text_transform_new:
 * @transform: `NsPangoTextTransform` to apply
 *
 * Create a new attribute that influences how characters
 * are transformed during shaping.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.50
 */
NsPangoAttribute *
ns_pango_attr_text_transform_new (NsPangoTextTransform transform)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_TEXT_TRANSFORM,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, transform);
}

/* }}} */
/* {{{ Binding helpers */

/**
 * ns_pango_attribute_as_int:
 * @attr: A `NsPangoAttribute` such as weight
 *
 * Returns the attribute cast to `NsPangoAttrInt`.
 *
 * This is mainly useful for language bindings.
 *
 * Returns: (nullable) (transfer none): The attribute as `NsPangoAttrInt`,
 *   or %NULL if it's not an integer attribute
 *
 * Since: 1.50
 */
NsPangoAttrInt *
ns_pango_attribute_as_int (NsPangoAttribute *attr)
{
  switch ((int)attr->klass->type)
    {
    case NS_PANGO_ATTR_STYLE:
    case NS_PANGO_ATTR_WEIGHT:
    case NS_PANGO_ATTR_VARIANT:
    case NS_PANGO_ATTR_STRETCH:
    case NS_PANGO_ATTR_WIDTH:
    case NS_PANGO_ATTR_UNDERLINE:
    case NS_PANGO_ATTR_STRIKETHROUGH:
    case NS_PANGO_ATTR_RISE:
    case NS_PANGO_ATTR_FALLBACK:
    case NS_PANGO_ATTR_LETTER_SPACING:
    case NS_PANGO_ATTR_WORD_SPACING:
    case NS_PANGO_ATTR_GRAVITY:
    case NS_PANGO_ATTR_GRAVITY_HINT:
    case NS_PANGO_ATTR_FOREGROUND_ALPHA:
    case NS_PANGO_ATTR_BACKGROUND_ALPHA:
    case NS_PANGO_ATTR_ALLOW_BREAKS:
    case NS_PANGO_ATTR_SHOW:
    case NS_PANGO_ATTR_INSERT_HYPHENS:
    case NS_PANGO_ATTR_OVERLINE:
    case NS_PANGO_ATTR_ABSOLUTE_LINE_HEIGHT:
    case NS_PANGO_ATTR_TEXT_TRANSFORM:
    case NS_PANGO_ATTR_WORD:
    case NS_PANGO_ATTR_SENTENCE:
    case NS_PANGO_ATTR_BASELINE_SHIFT:
    case NS_PANGO_ATTR_FONT_SCALE:
      return (NsPangoAttrInt *)attr;

    default:
      return NULL;
    }
}

/**
 * ns_pango_attribute_as_float:
 * @attr: A `NsPangoAttribute` such as scale
 *
 * Returns the attribute cast to `NsPangoAttrFloat`.
 *
 * This is mainly useful for language bindings.
 *
 * Returns: (nullable) (transfer none): The attribute as `NsPangoAttrFloat`,
 *   or %NULL if it's not a floating point attribute
 *
 * Since: 1.50
 */
NsPangoAttrFloat *
ns_pango_attribute_as_float (NsPangoAttribute *attr)
{
  switch ((int)attr->klass->type)
    {
    case NS_PANGO_ATTR_SCALE:
    case NS_PANGO_ATTR_LINE_HEIGHT:
      return (NsPangoAttrFloat *)attr;

    default:
      return NULL;
    }
}

/**
 * ns_pango_attribute_as_string:
 * @attr: A `NsPangoAttribute` such as family
 *
 * Returns the attribute cast to `NsPangoAttrString`.
 *
 * This is mainly useful for language bindings.
 *
 * Returns: (nullable) (transfer none): The attribute as `NsPangoAttrString`,
 *   or %NULL if it's not a string attribute
 *
 * Since: 1.50
 */
NsPangoAttrString *
ns_pango_attribute_as_string (NsPangoAttribute *attr)
{
  switch ((int)attr->klass->type)
    {
    case NS_PANGO_ATTR_FAMILY:
      return (NsPangoAttrString *)attr;

    default:
      return NULL;
    }
}

/**
 * ns_pango_attribute_as_size:
 * @attr: A `NsPangoAttribute` representing a size
 *
 * Returns the attribute cast to `NsPangoAttrSize`.
 *
 * This is mainly useful for language bindings.
 *
 * Returns: (nullable) (transfer none): The attribute as `NsPangoAttrSize`,
 *   or NULL if it's not a size attribute
 *
 * Since: 1.50
 */
NsPangoAttrSize *
ns_pango_attribute_as_size (NsPangoAttribute *attr)
{
  switch ((int)attr->klass->type)
    {
    case NS_PANGO_ATTR_SIZE:
    case NS_PANGO_ATTR_ABSOLUTE_SIZE:
      return (NsPangoAttrSize *)attr;

    default:
      return NULL;
    }
}

/**
 * ns_pango_attribute_as_color:
 * @attr: A `NsPangoAttribute` such as foreground
 *
 * Returns the attribute cast to `NsPangoAttrColor`.
 *
 * This is mainly useful for language bindings.
 *
 * Returns: (nullable) (transfer none): The attribute as `NsPangoAttrColor`,
 *   or %NULL if it's not a color attribute
 *
 * Since: 1.50
 */
NsPangoAttrColor *
ns_pango_attribute_as_color (NsPangoAttribute *attr)
{
  switch ((int)attr->klass->type)
    {
    case NS_PANGO_ATTR_FOREGROUND:
    case NS_PANGO_ATTR_BACKGROUND:
    case NS_PANGO_ATTR_UNDERLINE_COLOR:
    case NS_PANGO_ATTR_STRIKETHROUGH_COLOR:
    case NS_PANGO_ATTR_OVERLINE_COLOR:
      return (NsPangoAttrColor *)attr;

    default:
      return NULL;
    }
}

/**
 * ns_pango_attribute_as_font_desc:
 * @attr: A `NsPangoAttribute` representing a font description
 *
 * Returns the attribute cast to `NsPangoAttrFontDesc`.
 *
 * This is mainly useful for language bindings.
 *
 * Returns: (nullable) (transfer none): The attribute as `NsPangoAttrFontDesc`,
 *   or %NULL if it's not a font description attribute
 *
 * Since: 1.50
 */
NsPangoAttrFontDesc *
ns_pango_attribute_as_font_desc (NsPangoAttribute *attr)
{
  switch ((int)attr->klass->type)
    {
    case NS_PANGO_ATTR_FONT_DESC:
      return (NsPangoAttrFontDesc *)attr;

    default:
      return NULL;
    }
}

/**
 * ns_pango_attribute_as_font_features:
 * @attr: A `NsPangoAttribute` representing font features
 *
 * Returns the attribute cast to `NsPangoAttrFontFeatures`.
 *
 * This is mainly useful for language bindings.
 *
 * Returns: (nullable) (transfer none): The attribute as `NsPangoAttrFontFeatures`,
 *   or %NULL if it's not a font features attribute
 *
 * Since: 1.50
 */
NsPangoAttrFontFeatures *
ns_pango_attribute_as_font_features (NsPangoAttribute *attr)
{
  switch ((int)attr->klass->type)
    {
    case NS_PANGO_ATTR_FONT_FEATURES:
      return (NsPangoAttrFontFeatures *)attr;

    default:
      return NULL;
    }
}

/**
 * ns_pango_attribute_as_language:
 * @attr: A `NsPangoAttribute` representing a language
 *
 * Returns the attribute cast to `NsPangoAttrLanguage`.
 *
 * This is mainly useful for language bindings.
 *
 * Returns: (nullable) (transfer none): The attribute as `NsPangoAttrLanguage`,
 *   or %NULL if it's not a language attribute
 *
 * Since: 1.50
 */
NsPangoAttrLanguage *
ns_pango_attribute_as_language (NsPangoAttribute *attr)
{
  switch ((int)attr->klass->type)
    {
    case NS_PANGO_ATTR_LANGUAGE:
      return (NsPangoAttrLanguage *)attr;

    default:
      return NULL;
    }
}

/* }}} */
/* {{{ Attribute List */

void
_ns_pango_attr_list_init (NsPangoAttrList *list)
{
  list->ref_count = 1;
  list->attributes = NULL;
}

/**
 * ns_pango_attr_list_new:
 *
 * Create a new empty attribute list with a reference
 * count of one.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttrList`, which should be freed with
 *   [method@Pango.AttrList.unref]
 */
NsPangoAttrList *
ns_pango_attr_list_new (void)
{
  NsPangoAttrList *list = g_slice_new (NsPangoAttrList);

  _ns_pango_attr_list_init (list);

  return list;
}

/**
 * ns_pango_attr_list_ref:
 * @list: (nullable): a `NsPangoAttrList`
 *
 * Increase the reference count of the given attribute
 * list by one.
 *
 * Return value: The attribute list passed in
 *
 * Since: 1.10
 */
NsPangoAttrList *
ns_pango_attr_list_ref (NsPangoAttrList *list)
{
  if (list == NULL)
    return NULL;

  g_atomic_int_inc ((int *) &list->ref_count);

  return list;
}

void
_ns_pango_attr_list_destroy (NsPangoAttrList *list)
{
  guint i, p;

  if (!list->attributes)
    return;

  for (i = 0, p = list->attributes->len; i < p; i++)
    {
      NsPangoAttribute *attr = g_ptr_array_index (list->attributes, i);

      attr->klass->destroy (attr);
    }

  g_ptr_array_free (list->attributes, TRUE);
}

/**
 * ns_pango_attr_list_unref:
 * @list: (nullable): a `NsPangoAttrList`
 *
 * Decrease the reference count of the given attribute
 * list by one.
 *
 * If the result is zero, free the attribute list
 * and the attributes it contains.
 */
void
ns_pango_attr_list_unref (NsPangoAttrList *list)
{
  if (list == NULL)
    return;

  g_return_if_fail (list->ref_count > 0);

  if (g_atomic_int_dec_and_test ((int *) &list->ref_count))
    {
      _ns_pango_attr_list_destroy (list);
      g_slice_free (NsPangoAttrList, list);
    }
}

/**
 * ns_pango_attr_list_copy:
 * @list: (nullable): a `NsPangoAttrList`
 *
 * Copy @list and return an identical new list.
 *
 * Return value: (nullable): the newly allocated
 *   `NsPangoAttrList`, with a reference count of one,
 *   which should be freed with [method@Pango.AttrList.unref].
 *   Returns %NULL if @list was %NULL.
 */
NsPangoAttrList *
ns_pango_attr_list_copy (NsPangoAttrList *list)
{
  NsPangoAttrList *new;

  if (list == NULL)
    return NULL;

  new = ns_pango_attr_list_new ();
  if (!list->attributes || list->attributes->len == 0)
    return new;

  new->attributes = g_ptr_array_copy (list->attributes, (GCopyFunc)ns_pango_attribute_copy, NULL);

  return new;
}

static void
ns_pango_attr_list_insert_internal (NsPangoAttrList  *list,
                                 NsPangoAttribute *attr,
                                 gboolean        before)
{
  const guint start_index = attr->start_index;
  NsPangoAttribute *last_attr;

  if (G_UNLIKELY (!list->attributes))
    list->attributes = g_ptr_array_new ();

  if (list->attributes->len == 0)
    {
      g_ptr_array_add (list->attributes, attr);
      return;
    }

  g_assert (list->attributes->len > 0);

  last_attr = g_ptr_array_index (list->attributes, list->attributes->len - 1);

  if (last_attr->start_index < start_index ||
      (!before && last_attr->start_index == start_index))
    {
      g_ptr_array_add (list->attributes, attr);
    }
  else
    {
      guint i, p;

      for (i = 0, p = list->attributes->len; i < p; i++)
        {
          NsPangoAttribute *cur = g_ptr_array_index (list->attributes, i);

          if (cur->start_index > start_index ||
              (before && cur->start_index == start_index))
            {
              g_ptr_array_insert (list->attributes, i, attr);
              break;
            }
        }
    }
}

/**
 * ns_pango_attr_list_insert:
 * @list: a `NsPangoAttrList`
 * @attr: (transfer full): the attribute to insert
 *
 * Insert the given attribute into the `NsPangoAttrList`.
 *
 * It will be inserted after all other attributes with a
 * matching @start_index.
 */
void
ns_pango_attr_list_insert (NsPangoAttrList  *list,
                        NsPangoAttribute *attr)
{
  g_return_if_fail (list != NULL);
  g_return_if_fail (attr != NULL);

  ns_pango_attr_list_insert_internal (list, attr, FALSE);
}

/**
 * ns_pango_attr_list_insert_before:
 * @list: a `NsPangoAttrList`
 * @attr: (transfer full): the attribute to insert
 *
 * Insert the given attribute into the `NsPangoAttrList`.
 *
 * It will be inserted before all other attributes with a
 * matching @start_index.
 */
void
ns_pango_attr_list_insert_before (NsPangoAttrList  *list,
                               NsPangoAttribute *attr)
{
  g_return_if_fail (list != NULL);
  g_return_if_fail (attr != NULL);

  ns_pango_attr_list_insert_internal (list, attr, TRUE);
}

/**
 * ns_pango_attr_list_get_attributes:
 * @list: a `NsPangoAttrList`
 *
 * Gets a list of all attributes in @list.
 *
 * Return value: (element-type Pango.Attribute) (transfer full):
 *   a list of all attributes in @list. To free this value,
 *   call [method@Pango.Attribute.destroy] on each value and
 *   g_slist_free() on the list.
 *
 * Since: 1.44
 */
GSList *
ns_pango_attr_list_get_attributes (NsPangoAttrList *list)
{
  GSList *result = NULL;
  guint i, p;

  g_return_val_if_fail (list != NULL, NULL);

  if (!list->attributes || list->attributes->len == 0)
    return NULL;

  for (i = 0, p = list->attributes->len; i < p; i++)
    {
      NsPangoAttribute *attr = g_ptr_array_index (list->attributes, i);

      result = g_slist_prepend (result, ns_pango_attribute_copy (attr));
    }

  return g_slist_reverse (result);
}

/**
 * ns_pango_attr_list_equal:
 * @list: a `NsPangoAttrList`
 * @other_list: the other `NsPangoAttrList`
 *
 * Checks whether @list and @other_list contain the same
 * attributes and whether those attributes apply to the
 * same ranges.
 *
 * Beware that this will return wrong values if any list
 * contains duplicates.
 *
 * Return value: %TRUE if the lists are equal, %FALSE if
 *   they aren't
 *
 * Since: 1.46
 */
gboolean
ns_pango_attr_list_equal (NsPangoAttrList *list,
                       NsPangoAttrList *other_list)
{
  GPtrArray *attrs, *other_attrs;
  guint64 skip_bitmask = 0;
  guint i;

  if (list == other_list)
    return TRUE;

  if (list == NULL || other_list == NULL)
    return FALSE;

  if (list->attributes == NULL || other_list->attributes == NULL)
    return list->attributes == other_list->attributes;

  attrs = list->attributes;
  other_attrs = other_list->attributes;

  if (attrs->len != other_attrs->len)
    return FALSE;

  for (i = 0; i < attrs->len; i++)
    {
      NsPangoAttribute *attr = g_ptr_array_index (attrs, i);
      gboolean attr_equal = FALSE;
      guint other_attr_index;

      for (other_attr_index = 0; other_attr_index < other_attrs->len; other_attr_index++)
        {
          NsPangoAttribute *other_attr = g_ptr_array_index (other_attrs, other_attr_index);
          /* The shift has to happen in 64 bits. Shifting a plain 1 -- an int --
           * by 31 overflows it into a mask with every bit from 31 up set, and
           * by 32 or more is undefined outright and in practice aliases a much
           * lower index. Either way the bitmask marks attributes as already
           * matched that never were, and the attribute that needed one of them
           * finds no candidate left. Measured: two lists built identically
           * compared equal up to 32 attributes and unequal at 33 and above.
           *
           * Nothing noticed while this was only change detection, which errs
           * safe by re-doing work. The item cache put it on a path where the
           * answer is a cache key, so every paragraph carrying more than 32
           * attributes missed every time.
           */
          guint64 other_attr_bitmask = other_attr_index < 64
                                     ? G_GUINT64_CONSTANT (1) << other_attr_index
                                     : 0;

          if ((skip_bitmask & other_attr_bitmask) != 0)
            continue;

          if (attr->start_index == other_attr->start_index &&
              attr->end_index == other_attr->end_index &&
              ns_pango_attribute_equal (attr, other_attr))
            {
              skip_bitmask |= other_attr_bitmask;
              attr_equal = TRUE;
              break;
            }

        }

      if (!attr_equal)
        return FALSE;
    }

  return TRUE;
}

/**
 * ns_pango_attr_list_filter:
 * @list: a `NsPangoAttrList`
 * @func: (scope call) (closure data): callback function;
 *   returns %TRUE if an attribute should be filtered out
 * @data: Data to be passed to @func
 *
 * Given a `NsPangoAttrList` and callback function, removes
 * any elements of @list for which @func returns %TRUE and
 * inserts them into a new list.
 *
 * Return value: (transfer full) (nullable): the new
 *   `NsPangoAttrList` or %NULL if no attributes of the
 *   given types were found
 *
 * Since: 1.2
 */
NsPangoAttrList *
ns_pango_attr_list_filter (NsPangoAttrList       *list,
                        NsPangoAttrFilterFunc  func,
                        gpointer             data)

{
  NsPangoAttrList *new = NULL;
  guint i, p;

  g_return_val_if_fail (list != NULL, NULL);

  if (!list->attributes || list->attributes->len == 0)
    return NULL;

  for (i = 0, p = list->attributes->len; i < p; i++)
    {
      NsPangoAttribute *tmp_attr = g_ptr_array_index (list->attributes, i);

      if ((*func) (tmp_attr, data))
        {
          g_ptr_array_remove_index (list->attributes, i);
          i--; /* Need to look at this index again */
          p--;

          if (G_UNLIKELY (!new))
            {
              new = ns_pango_attr_list_new ();
              new->attributes = g_ptr_array_new ();
            }

          g_ptr_array_add (new->attributes, tmp_attr);
        }
    }

  return new;
}

/* {{{ NsPangoAttrList serialization */

/* }}} */
/* {{{ Attribute Iterator */

void
_ns_pango_attr_list_get_iterator (NsPangoAttrList     *list,
                               NsPangoAttrIterator *iterator)
{
  iterator->attribute_stack = NULL;
  iterator->attrs = list->attributes;
  iterator->n_attrs = iterator->attrs ? iterator->attrs->len : 0;

  iterator->attr_index = 0;
  iterator->start_index = 0;
  iterator->end_index = 0;

  if (!ns_pango_attr_iterator_next (iterator))
    iterator->end_index = G_MAXUINT;
}

/**
 * ns_pango_attr_list_get_iterator:
 * @list: a `NsPangoAttrList`
 *
 * Create a iterator initialized to the beginning of the list.
 *
 * @list must not be modified until this iterator is freed.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttrIterator`, which should be freed with
 *   [method@Pango.AttrIterator.destroy]
 */
NsPangoAttrIterator *
ns_pango_attr_list_get_iterator (NsPangoAttrList  *list)
{
  NsPangoAttrIterator *iterator;

  g_return_val_if_fail (list != NULL, NULL);

  iterator = g_slice_new (NsPangoAttrIterator);
  _ns_pango_attr_list_get_iterator (list, iterator);

  return iterator;
}

/**
 * ns_pango_attr_iterator_range:
 * @iterator: a NsPangoAttrIterator
 * @start: (out): location to store the start of the range
 * @end: (out): location to store the end of the range
 *
 * Get the range of the current segment.
 *
 * Note that the stored return values are signed, not unsigned
 * like the values in `NsPangoAttribute`. To deal with this API
 * oversight, stored return values that wouldn't fit into
 * a signed integer are clamped to %G_MAXINT.
 */
void
ns_pango_attr_iterator_range (NsPangoAttrIterator *iterator,
                           gint              *start,
                           gint              *end)
{
  g_return_if_fail (iterator != NULL);

  if (start)
    *start = MIN (iterator->start_index, G_MAXINT);
  if (end)
    *end = MIN (iterator->end_index, G_MAXINT);
}

/**
 * ns_pango_attr_iterator_next:
 * @iterator: a `NsPangoAttrIterator`
 *
 * Advance the iterator until the next change of style.
 *
 * Return value: %FALSE if the iterator is at the end
 *   of the list, otherwise %TRUE
 */
gboolean
ns_pango_attr_iterator_next (NsPangoAttrIterator *iterator)
{
  int i;

  g_return_val_if_fail (iterator != NULL, FALSE);

  if (iterator->attr_index >= iterator->n_attrs &&
      (!iterator->attribute_stack || iterator->attribute_stack->len == 0))
    return FALSE;

  iterator->start_index = iterator->end_index;
  iterator->end_index = G_MAXUINT;

  if (iterator->attribute_stack)
    {
      for (i = iterator->attribute_stack->len - 1; i>= 0; i--)
        {
          const NsPangoAttribute *attr = g_ptr_array_index (iterator->attribute_stack, i);

          if (attr->end_index == iterator->start_index)
            g_ptr_array_remove_index (iterator->attribute_stack, i); /* Can't use index_fast :( */
          else
            iterator->end_index = MIN (iterator->end_index, attr->end_index);
        }
    }

  while (1)
    {
      NsPangoAttribute *attr;

      if (iterator->attr_index >= iterator->n_attrs)
        break;

      attr = g_ptr_array_index (iterator->attrs, iterator->attr_index);

      if (attr->start_index != iterator->start_index)
        break;

      if (attr->end_index > iterator->start_index)
        {
          if (G_UNLIKELY (!iterator->attribute_stack))
            iterator->attribute_stack = g_ptr_array_new ();

          g_ptr_array_add (iterator->attribute_stack, attr);

          iterator->end_index = MIN (iterator->end_index, attr->end_index);
        }

      iterator->attr_index++; /* NEXT! */
    }

  if (iterator->attr_index < iterator->n_attrs)
      {
      NsPangoAttribute *attr = g_ptr_array_index (iterator->attrs, iterator->attr_index);

      iterator->end_index = MIN (iterator->end_index, attr->start_index);
    }

  return TRUE;
}

/**
 * ns_pango_attr_iterator_copy:
 * @iterator: a `NsPangoAttrIterator`
 *
 * Copy a `NsPangoAttrIterator`.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttrIterator`, which should be freed with
 *   [method@Pango.AttrIterator.destroy]
 */
NsPangoAttrIterator *
ns_pango_attr_iterator_copy (NsPangoAttrIterator *iterator)
{
  NsPangoAttrIterator *copy;

  g_return_val_if_fail (iterator != NULL, NULL);

  copy = g_slice_new (NsPangoAttrIterator);

  *copy = *iterator;

  if (iterator->attribute_stack)
    copy->attribute_stack = g_ptr_array_copy (iterator->attribute_stack, NULL, NULL);
  else
    copy->attribute_stack = NULL;

  return copy;
}

void
_ns_pango_attr_iterator_destroy (NsPangoAttrIterator *iterator)
{
  if (iterator->attribute_stack)
    g_ptr_array_free (iterator->attribute_stack, TRUE);
}

/**
 * ns_pango_attr_iterator_destroy:
 * @iterator: a `NsPangoAttrIterator`
 *
 * Destroy a `NsPangoAttrIterator` and free all associated memory.
 */
void
ns_pango_attr_iterator_destroy (NsPangoAttrIterator *iterator)
{
  g_return_if_fail (iterator != NULL);

  _ns_pango_attr_iterator_destroy (iterator);
  g_slice_free (NsPangoAttrIterator, iterator);
}

/**
 * ns_pango_attr_iterator_get:
 * @iterator: a `NsPangoAttrIterator`
 * @type: the type of attribute to find
 *
 * Find the current attribute of a particular type
 * at the iterator location.
 *
 * When multiple attributes of the same type overlap,
 * the attribute whose range starts closest to the
 * current location is used.
 *
 * Return value: (nullable) (transfer none): the current
 *   attribute of the given type, or %NULL if no attribute
 *   of that type applies to the current location.
 */
NsPangoAttribute *
ns_pango_attr_iterator_get (NsPangoAttrIterator *iterator,
                         NsPangoAttrType      type)
{
  int i;

  g_return_val_if_fail (iterator != NULL, NULL);

  if (!iterator->attribute_stack)
    return NULL;

  for (i = iterator->attribute_stack->len - 1; i>= 0; i--)
    {
      NsPangoAttribute *attr = g_ptr_array_index (iterator->attribute_stack, i);

      if (attr->klass->type == type)
        return attr;
    }

  return NULL;
}

/**
 * ns_pango_attr_iterator_get_font:
 * @iterator: a `NsPangoAttrIterator`
 * @desc: a `NsPangoFontDescription` to fill in with the current
 *   values. The family name in this structure will be set using
 *   [method@Pango.FontDescription.set_family_static] using
 *   values from an attribute in the `NsPangoAttrList` associated
 *   with the iterator, so if you plan to keep it around, you
 *   must call:
 *   `ns_pango_font_description_set_family (desc, ns_pango_font_description_get_family (desc))`.
 * @language: (out) (optional): location to store language tag
 *   for item, or %NULL if none is found.
 * @extra_attrs: (out) (optional) (element-type Pango.Attribute) (transfer full):
 *   location in which to store a list of non-font attributes
 *   at the the current position; only the highest priority
 *   value of each attribute will be added to this list. In
 *   order to free this value, you must call
 *   [method@Pango.Attribute.destroy] on each member.
 *
 * Get the font and other attributes at the current
 * iterator position.
 */
void
ns_pango_attr_iterator_get_font (NsPangoAttrIterator     *iterator,
                              NsPangoFontDescription  *desc,
                              NsPangoLanguage        **language,
                              GSList               **extra_attrs)
{
  NsPangoFontMask mask = 0;
  gboolean have_language = FALSE;
  gdouble scale = 0;
  gboolean have_scale = FALSE;
  int i;

  g_return_if_fail (iterator != NULL);
  g_return_if_fail (desc != NULL);

  if (language)
    *language = NULL;

  if (extra_attrs)
    *extra_attrs = NULL;

  if (!iterator->attribute_stack)
    return;

  for (i = iterator->attribute_stack->len - 1; i >= 0; i--)
    {
      const NsPangoAttribute *attr = g_ptr_array_index (iterator->attribute_stack, i);

      switch ((int) attr->klass->type)
        {
        case NS_PANGO_ATTR_FONT_DESC:
          {
            NsPangoFontMask new_mask = ns_pango_font_description_get_set_fields (((NsPangoAttrFontDesc *)attr)->desc) & ~mask;
            mask |= new_mask;
            ns_pango_font_description_unset_fields (desc, new_mask);
            ns_pango_font_description_merge_static (desc, ((NsPangoAttrFontDesc *)attr)->desc, FALSE);

            break;
          }
        case NS_PANGO_ATTR_FAMILY:
          if (!(mask & NS_PANGO_FONT_MASK_FAMILY))
            {
              mask |= NS_PANGO_FONT_MASK_FAMILY;
              ns_pango_font_description_set_family (desc, ((NsPangoAttrString *)attr)->value);
            }
          break;
        case NS_PANGO_ATTR_STYLE:
          if (!(mask & NS_PANGO_FONT_MASK_STYLE))
            {
              mask |= NS_PANGO_FONT_MASK_STYLE;
              ns_pango_font_description_set_style (desc, ((NsPangoAttrInt *)attr)->value);
            }
          break;
        case NS_PANGO_ATTR_VARIANT:
          if (!(mask & NS_PANGO_FONT_MASK_VARIANT))
            {
              mask |= NS_PANGO_FONT_MASK_VARIANT;
              ns_pango_font_description_set_variant (desc, ((NsPangoAttrInt *)attr)->value);
            }
          break;
        case NS_PANGO_ATTR_WEIGHT:
          if (!(mask & NS_PANGO_FONT_MASK_WEIGHT))
            {
              mask |= NS_PANGO_FONT_MASK_WEIGHT;
              ns_pango_font_description_set_weight (desc, ((NsPangoAttrInt *)attr)->value);
            }
          break;
        case NS_PANGO_ATTR_STRETCH:
          if (!(mask & NS_PANGO_FONT_MASK_STRETCH))
            {
              mask |= NS_PANGO_FONT_MASK_STRETCH;
              ns_pango_font_description_set_stretch (desc, ((NsPangoAttrInt *)attr)->value);
            }
          break;
        case NS_PANGO_ATTR_WIDTH:
          if (!(mask & NS_PANGO_FONT_MASK_WIDTH))
            {
              mask |= NS_PANGO_FONT_MASK_WIDTH;
              ns_pango_font_description_set_width (desc, ((NsPangoAttrInt *)attr)->value);
            }
          break;
        case NS_PANGO_ATTR_SIZE:
          if (!(mask & NS_PANGO_FONT_MASK_SIZE))
            {
              mask |= NS_PANGO_FONT_MASK_SIZE;
              ns_pango_font_description_set_size (desc, ((NsPangoAttrSize *)attr)->size);
            }
          break;
        case NS_PANGO_ATTR_ABSOLUTE_SIZE:
          if (!(mask & NS_PANGO_FONT_MASK_SIZE))
            {
              mask |= NS_PANGO_FONT_MASK_SIZE;
              ns_pango_font_description_set_absolute_size (desc, ((NsPangoAttrSize *)attr)->size);
            }
          break;
        case NS_PANGO_ATTR_SCALE:
          if (!have_scale)
            {
              have_scale = TRUE;
              scale = ((NsPangoAttrFloat *)attr)->value;
            }
          break;
        case NS_PANGO_ATTR_LANGUAGE:
          if (language)
            {
              if (!have_language)
                {
                  have_language = TRUE;
                  *language = ((NsPangoAttrLanguage *)attr)->value;
                }
            }
          break;
        default:
          if (extra_attrs)
            {
              gboolean found = FALSE;

              /* Hack: special-case FONT_FEATURES, BASELINE_SHIFT and FONT_SCALE.
               * We don't want these to accumulate, not override each other,
               * so we never merge them.
               * This needs to be handled more systematically.
               */
              if (attr->klass->type != NS_PANGO_ATTR_FONT_FEATURES &&
                  attr->klass->type != NS_PANGO_ATTR_BASELINE_SHIFT &&
                  attr->klass->type != NS_PANGO_ATTR_FONT_SCALE)
                {
                  GSList *tmp_list = *extra_attrs;
                  while (tmp_list)
                    {
                      NsPangoAttribute *old_attr = tmp_list->data;
                      if (attr->klass->type == old_attr->klass->type)
                        {
                          found = TRUE;
                          break;
                        }

                      tmp_list = tmp_list->next;
                    }
                }

              if (!found)
                *extra_attrs = g_slist_prepend (*extra_attrs, ns_pango_attribute_copy (attr));
            }
        }
    }

  if (have_scale)
    {
      /* We need to use a local variable to ensure that the compiler won't
       * implicitly cast it to integer while the result is kept in registers,
       * leading to a wrong approximation in i386 (with 387 FPU)
       */
      volatile double size = scale * ns_pango_font_description_get_size (desc);

      if (ns_pango_font_description_get_size_is_absolute (desc))
        ns_pango_font_description_set_absolute_size (desc, size);
      else
        ns_pango_font_description_set_size (desc, size);
    }
}

/**
 * ns_pango_attr_iterator_get_attrs:
 * @iterator: a `NsPangoAttrIterator`
 *
 * Gets a list of all attributes at the current position of the
 * iterator.
 *
 * Return value: (element-type Pango.Attribute) (transfer full):
 *   a list of all attributes for the current range. To free
 *   this value, call [method@Pango.Attribute.destroy] on each
 *   value and g_slist_free() on the list.
 *
 * Since: 1.2
 */
GSList *
ns_pango_attr_iterator_get_attrs (NsPangoAttrIterator *iterator)
{
  GSList *attrs = NULL;
  int i;

  if (!iterator->attribute_stack ||
      iterator->attribute_stack->len == 0)
    return NULL;

  for (i = iterator->attribute_stack->len - 1; i >= 0; i--)
    {
      NsPangoAttribute *attr = g_ptr_array_index (iterator->attribute_stack, i);
      GSList *tmp_list2;
      gboolean found = FALSE;

      if (attr->klass->type != NS_PANGO_ATTR_FONT_DESC &&
          attr->klass->type != NS_PANGO_ATTR_BASELINE_SHIFT &&
          attr->klass->type != NS_PANGO_ATTR_FONT_SCALE)
        for (tmp_list2 = attrs; tmp_list2; tmp_list2 = tmp_list2->next)
          {
            NsPangoAttribute *old_attr = tmp_list2->data;
            if (attr->klass->type == old_attr->klass->type)
              {
                found = TRUE;
                break;
              }
           }

      if (!found)
        attrs = g_slist_prepend (attrs, ns_pango_attribute_copy (attr));
    }

  return attrs;
}

gboolean
ns_pango_attr_iterator_advance (NsPangoAttrIterator *iterator,
                             int                index)
{
  int start_range, end_range;

  ns_pango_attr_iterator_range (iterator, &start_range, &end_range);

  while (index >= end_range)
    {
      if (!ns_pango_attr_iterator_next (iterator))
        return FALSE;
      ns_pango_attr_iterator_range (iterator, &start_range, &end_range);
    }

  if (start_range > index)
    g_warning ("ns_pango_attr_iterator_advance(): iterator had already "
               "moved beyond the index");

  return TRUE;
}
/* }}} */

/* vim:set foldmethod=marker expandtab: */
