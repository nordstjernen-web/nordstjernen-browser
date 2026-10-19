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

G_LOCK_DEFINE_STATIC (attr_type);
static GHashTable *name_map = NULL; /* MT-safe */

/**
 * ns_pango_attr_type_register:
 * @name: an identifier for the type
 *
 * Allocate a new attribute type ID.
 *
 * The attribute type name can be accessed later
 * by using [func@Pango.AttrType.get_name].
 *
 * Return value: the new type ID.
 */
NsPangoAttrType
ns_pango_attr_type_register (const gchar *name)
{
  static guint current_type = 0x1000000; /* MT-safe */
  guint type;

  G_LOCK (attr_type);

  type = current_type++;

  if (name)
    {
      if (G_UNLIKELY (!name_map))
        name_map = g_hash_table_new (NULL, NULL);

      g_hash_table_insert (name_map, GUINT_TO_POINTER (type), (gpointer) g_intern_string (name));
    }

  G_UNLOCK (attr_type);

  return type;
}

/**
 * ns_pango_attr_type_get_name:
 * @type: an attribute type ID to fetch the name for
 *
 * Fetches the attribute type name.
 *
 * The attribute type name is the string passed in
 * when registering the type using
 * [func@Pango.AttrType.register].
 *
 * The returned value is an interned string (see
 * g_intern_string() for what that means) that should
 * not be modified or freed.
 *
 * Return value: (nullable): the type ID name (which
 *   may be %NULL), or %NULL if @type is a built-in Pango
 *   attribute type or invalid.
 *
 * Since: 1.22
 */
const char *
ns_pango_attr_type_get_name (NsPangoAttrType type)
{
  const char *result = NULL;

  G_LOCK (attr_type);

  if (name_map)
    result = g_hash_table_lookup (name_map, GUINT_TO_POINTER ((guint) type));

  G_UNLOCK (attr_type);

  return result;
}

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

G_DEFINE_BOXED_TYPE (NsPangoAttribute, ns_pango_attribute,
                     ns_pango_attribute_copy,
                     ns_pango_attribute_destroy);

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
static NsPangoAttribute *ns_pango_attr_float_new (const NsPangoAttrClass *klass,
                                             double                value);

static NsPangoAttribute *
ns_pango_attr_float_copy (const NsPangoAttribute *attr)
{
  const NsPangoAttrFloat *float_attr = (NsPangoAttrFloat *)attr;

  return ns_pango_attr_float_new (attr->klass, float_attr->value);
}

static void
ns_pango_attr_float_destroy (NsPangoAttribute *attr)
{
  NsPangoAttrFloat *fattr = (NsPangoAttrFloat *)attr;

  g_slice_free (NsPangoAttrFloat, fattr);
}

static gboolean
ns_pango_attr_float_equal (const NsPangoAttribute *attr1,
                        const NsPangoAttribute *attr2)
{
  const NsPangoAttrFloat *float_attr1 = (const NsPangoAttrFloat *)attr1;
  const NsPangoAttrFloat *float_attr2 = (const NsPangoAttrFloat *)attr2;

  return (float_attr1->value == float_attr2->value);
}

static NsPangoAttribute *
ns_pango_attr_float_new  (const NsPangoAttrClass *klass,
                       double                value)
{
  NsPangoAttrFloat *result = g_slice_new (NsPangoAttrFloat);
  ns_pango_attribute_init (&result->attr, klass);
  result->value = value;

  return (NsPangoAttribute *)result;
}
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

/* Helper for the deserialization code below */
static NsPangoAttribute *
ns_pango_attr_font_desc_from_string_new (const char *string)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_FONT_DESC,
    ns_pango_attr_font_desc_copy,
    ns_pango_attr_font_desc_destroy,
    ns_pango_attr_font_desc_equal
  };

  NsPangoAttrFontDesc *result = g_slice_new (NsPangoAttrFontDesc);
  ns_pango_attribute_init (&result->attr, &klass);
  result->desc = ns_pango_font_description_from_string (string);

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
 * ns_pango_attr_baseline_shift_new:
 * @shift: either a `NsPangoBaselineShift` enumeration value or an absolute value (> 1024)
 *   in Pango units, relative to the baseline of the previous run.
 *   Positive values displace the text upwards.
 *
 * Create a new baseline displacement attribute.
 *
 * The effect of this attribute is to shift the baseline of a run,
 * relative to the run of preceding run.
 *
 * <picture>
 *   <source srcset="baseline-shift-dark.png" media="(prefers-color-scheme: dark)">
 *   <img alt="Baseline Shift" src="baseline-shift-light.png">
 * </picture>

 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.50
 */
NsPangoAttribute *
ns_pango_attr_baseline_shift_new (int rise)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_BASELINE_SHIFT,
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
 * ns_pango_attr_scale_new:
 * @scale_factor: factor to scale the font
 *
 * Create a new font size scale attribute.
 *
 * The base font for the affected text will have
 * its size multiplied by @scale_factor.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 */
NsPangoAttribute*
ns_pango_attr_scale_new (double scale_factor)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_SCALE,
    ns_pango_attr_float_copy,
    ns_pango_attr_float_destroy,
    ns_pango_attr_float_equal
  };

  return ns_pango_attr_float_new (&klass, scale_factor);
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
 * ns_pango_attr_word_spacing_new:
 * @word_spacing: amount of extra space to add at each word
 *   separator, in Pango units. May be negative.
 *
 * Create a new word-spacing attribute.
 *
 * The space is added to the advance of each word separator
 * character in the range, which is what CSS `word-spacing`
 * asks for. The word separators are the ones CSS Text names:
 * space, no-break space, Ethiopic wordspace, the two Aegean
 * word separators, and the Ugaritic and Old Persian word
 * dividers.
 *
 * Unlike letter spacing, this does not change how the text is
 * shaped, and no space is added anywhere but at a separator.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.58
 */
NsPangoAttribute *
ns_pango_attr_word_spacing_new (int word_spacing)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_WORD_SPACING,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, word_spacing);
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
 * ns_pango_attr_gravity_new:
 * @gravity: the gravity value; should not be %NS_PANGO_GRAVITY_AUTO
 *
 * Create a new gravity attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.16
 */
NsPangoAttribute *
ns_pango_attr_gravity_new (NsPangoGravity gravity)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_GRAVITY,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  g_return_val_if_fail (gravity != NS_PANGO_GRAVITY_AUTO, NULL);

  return ns_pango_attr_int_new (&klass, (int)gravity);
}

/**
 * ns_pango_attr_gravity_hint_new:
 * @hint: the gravity hint value
 *
 * Create a new gravity hint attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.16
 */
NsPangoAttribute *
ns_pango_attr_gravity_hint_new (NsPangoGravityHint hint)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_GRAVITY_HINT,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, (int)hint);
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
 * ns_pango_attr_word_new:
 *
 * Marks the range of the attribute as a single word.
 *
 * Note that this may require adjustments to word and
 * sentence classification around the range.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.50
 */
NsPangoAttribute *
ns_pango_attr_word_new (void)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_WORD,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal,
  };

  return ns_pango_attr_int_new (&klass, 1);
}

/**
 * ns_pango_attr_sentence_new:
 *
 * Marks the range of the attribute as a single sentence.
 *
 * Note that this may require adjustments to word and
 * sentence classification around the range.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.50
 */
NsPangoAttribute *
ns_pango_attr_sentence_new (void)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_SENTENCE,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal,
  };

  return ns_pango_attr_int_new (&klass, 1);
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
 * ns_pango_attr_line_height_new:
 * @factor: the scaling factor to apply to the logical height
 *
 * Modify the height of logical line extents by a factor.
 *
 * This affects the values returned by
 * [method@Pango.LayoutLine.get_extents],
 * [method@Pango.LayoutLine.get_pixel_extents] and
 * [method@Pango.LayoutIter.get_line_extents].
 *
 *
 * Since: 1.50
 */
NsPangoAttribute *
ns_pango_attr_line_height_new (double factor)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_LINE_HEIGHT,
    ns_pango_attr_float_copy,
    ns_pango_attr_float_destroy,
    ns_pango_attr_float_equal
  };

  return ns_pango_attr_float_new (&klass, factor);
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

/**
 * ns_pango_attr_width_new:
 * @width: the width
 *
 * Create a new font width attribute.
 *
 * Return value: (transfer full): the newly allocated
 *   `NsPangoAttribute`, which should be freed with
 *   [method@Pango.Attribute.destroy]
 *
 * Since: 1.58
 */
NsPangoAttribute *
ns_pango_attr_width_new (NsPangoWidth width)
{
  static const NsPangoAttrClass klass = {
    NS_PANGO_ATTR_WIDTH,
    ns_pango_attr_int_copy,
    ns_pango_attr_int_destroy,
    ns_pango_attr_int_equal
  };

  return ns_pango_attr_int_new (&klass, (int) width);
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

/**
 * ns_pango_attribute_as_shape:
 * @attr: A `NsPangoAttribute` representing a shape
 *
 * Returns the attribute cast to `NsPangoAttrShape`.
 *
 * This is mainly useful for language bindings.
 *
 * Returns: (nullable) (transfer none): The attribute as `NsPangoAttrShape`,
 *   or %NULL if it's not a shape attribute
 *
 * Since: 1.50
 */
NsPangoAttrShape *
ns_pango_attribute_as_shape (NsPangoAttribute *attr)
{
  switch ((int)attr->klass->type)
    {
    case NS_PANGO_ATTR_SHAPE:
      return (NsPangoAttrShape *)attr;

    default:
      return NULL;
    }
}

/* }}} */
/* {{{ Attribute List */

G_DEFINE_BOXED_TYPE (NsPangoAttrList, ns_pango_attr_list,
                     ns_pango_attr_list_copy,
                     ns_pango_attr_list_unref);

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
 * ns_pango_attr_list_change:
 * @list: a `NsPangoAttrList`
 * @attr: (transfer full): the attribute to insert
 *
 * Insert the given attribute into the `NsPangoAttrList`.
 *
 * It will replace any attributes of the same type
 * on that segment and be merged with any adjoining
 * attributes that are identical.
 *
 * This function is slower than [method@Pango.AttrList.insert]
 * for creating an attribute list in order (potentially
 * much slower for large lists). However,
 * [method@Pango.AttrList.insert] is not suitable for
 * continually changing a set of attributes since it
 * never removes or combines existing attributes.
 */
void
ns_pango_attr_list_change (NsPangoAttrList  *list,
                        NsPangoAttribute *attr)
{
  guint i, p;
  guint start_index = attr->start_index;
  guint end_index = attr->end_index;
  gboolean inserted;

  g_return_if_fail (list != NULL);

  if (start_index == end_index) /* empty, nothing to do */
    {
      ns_pango_attribute_destroy (attr);
      return;
    }

  if (!list->attributes || list->attributes->len == 0)
    {
      ns_pango_attr_list_insert (list, attr);
      return;
    }

  inserted = FALSE;
  for (i = 0, p = list->attributes->len; i < p; i++)
    {
      NsPangoAttribute *tmp_attr = g_ptr_array_index (list->attributes, i);

      if (tmp_attr->start_index > start_index)
        {
          g_ptr_array_insert (list->attributes, i, attr);
          inserted = TRUE;
          break;
        }

      if (tmp_attr->klass->type != attr->klass->type)
        continue;

      if (tmp_attr->end_index < start_index)
        continue; /* This attr does not overlap with the new one */

      g_assert (tmp_attr->start_index <= start_index);
      g_assert (tmp_attr->end_index >= start_index);

      if (ns_pango_attribute_equal (tmp_attr, attr))
        {
          /* We can merge the new attribute with this attribute
           */
          if (tmp_attr->end_index >= end_index)
            {
              /* We are totally overlapping the previous attribute.
               * No action is needed.
               */
              ns_pango_attribute_destroy (attr);
              return;
            }

          tmp_attr->end_index = end_index;
          ns_pango_attribute_destroy (attr);

          attr = tmp_attr;
          inserted = TRUE;
          break;
        }
      else
        {
          /* Split, truncate, or remove the old attribute
           */
          if (tmp_attr->end_index > end_index)
            {
              NsPangoAttribute *end_attr = ns_pango_attribute_copy (tmp_attr);

              end_attr->start_index = end_index;
              ns_pango_attr_list_insert (list, end_attr);
            }

          if (tmp_attr->start_index == start_index)
            {
              ns_pango_attribute_destroy (tmp_attr);
              g_ptr_array_remove_index (list->attributes, i);
              break;
            }
          else
            {
              tmp_attr->end_index = start_index;
            }
        }
    }

  if (!inserted)
    /* we didn't insert attr yet */
    ns_pango_attr_list_insert (list, attr);

  /* We now have the range inserted into the list one way or the
   * other. Fix up the remainder
   */
  /* Attention: No i = 0 here. */
  for (i = i + 1, p = list->attributes->len; i < p; i++)
    {
      NsPangoAttribute *tmp_attr = g_ptr_array_index (list->attributes, i);

      if (tmp_attr->start_index > end_index)
        break;

      if (tmp_attr->klass->type != attr->klass->type)
        continue;

      if (tmp_attr == attr)
        continue;

      if (tmp_attr->end_index <= attr->end_index ||
          ns_pango_attribute_equal (tmp_attr, attr))
        {
          /* We can merge the new attribute with this attribute. */
          attr->end_index = MAX (end_index, tmp_attr->end_index);
          ns_pango_attribute_destroy (tmp_attr);
          g_ptr_array_remove_index (list->attributes, i);
          i--;
          p--;
          continue;
        }
      else
        {
          /* Trim the start of this attribute that it begins at the end
           * of the new attribute. This may involve moving it in the list
           * to maintain the required non-decreasing order of start indices.
           */
          int k, m;

          tmp_attr->start_index = attr->end_index;

          for (k = i + 1, m = list->attributes->len; k < m; k++)
            {
              NsPangoAttribute *tmp_attr2 = g_ptr_array_index (list->attributes, k);

              if (tmp_attr2->start_index >= tmp_attr->start_index)
                break;

              g_ptr_array_index (list->attributes, k - 1) = tmp_attr2;
              g_ptr_array_index (list->attributes, k) = tmp_attr;
            }
        }
    }
}

/**
 * ns_pango_attr_list_update:
 * @list: a `NsPangoAttrList`
 * @pos: the position of the change
 * @remove: the number of removed bytes
 * @add: the number of added bytes
 *
 * Update indices of attributes in @list for a change in the
 * text they refer to.
 *
 * The change that this function applies is removing @remove
 * bytes at position @pos and inserting @add bytes instead.
 *
 * Attributes that fall entirely in the (@pos, @pos + @remove)
 * range are removed.
 *
 * Attributes that start or end inside the (@pos, @pos + @remove)
 * range are shortened to reflect the removal.
 *
 * Attributes start and end positions are updated if they are
 * behind @pos + @remove.
 *
 * Since: 1.44
 */
void
ns_pango_attr_list_update (NsPangoAttrList *list,
                        int             pos,
                        int             remove,
                        int             add)
{
  guint i, p;

  g_return_if_fail (pos >= 0);
  g_return_if_fail (remove >= 0);
  g_return_if_fail (add >= 0);

  if (list->attributes)
    for (i = 0, p = list->attributes->len; i < p; i++)
      {
        NsPangoAttribute *attr = g_ptr_array_index (list->attributes, i);

        if (attr->start_index >= pos &&
          attr->end_index < pos + remove)
          {
            ns_pango_attribute_destroy (attr);
            g_ptr_array_remove_index (list->attributes, i);
            i--; /* Look at this index again */
            p--;
            continue;
          }

        if (attr->start_index != NS_PANGO_ATTR_INDEX_FROM_TEXT_BEGINNING)
          {
            if (attr->start_index >= pos &&
                attr->start_index < pos + remove)
              {
                attr->start_index = pos + add;
              }
            else if (attr->start_index >= pos + remove)
              {
                attr->start_index += add - remove;
              }
          }

        if (attr->end_index != NS_PANGO_ATTR_INDEX_TO_TEXT_END)
          {
            if (attr->end_index >= pos &&
                attr->end_index < pos + remove)
              {
                attr->end_index = pos;
              }
            else if (attr->end_index >= pos + remove)
              {
                if (add > remove &&
                    G_MAXUINT - attr->end_index < add - remove)
                  attr->end_index = G_MAXUINT;
                else
                  attr->end_index += add - remove;
              }
          }
      }
}

/**
 * ns_pango_attr_list_splice:
 * @list: a `NsPangoAttrList`
 * @other: another `NsPangoAttrList`
 * @pos: the position in @list at which to insert @other
 * @len: the length of the spliced segment. (Note that this
 *   must be specified since the attributes in @other may only
 *   be present at some subsection of this range)
 *
 * This function opens up a hole in @list, fills it
 * in with attributes from the left, and then merges
 * @other on top of the hole.
 *
 * This operation is equivalent to stretching every attribute
 * that applies at position @pos in @list by an amount @len,
 * and then calling [method@Pango.AttrList.change] with a copy
 * of each attribute in @other in sequence (offset in position
 * by @pos, and limited in length to @len).
 *
 * This operation proves useful for, for instance, inserting
 * a pre-edit string in the middle of an edit buffer.
 *
 * For backwards compatibility, the function behaves differently
 * when @len is 0. In this case, the attributes from @other are
 * not imited to @len, and are just overlayed on top of @list.
 *
 * This mode is useful for merging two lists of attributes together.
 */
void
ns_pango_attr_list_splice (NsPangoAttrList *list,
                        NsPangoAttrList *other,
                        gint           pos,
                        gint           len)
{
  guint i, p;
  guint upos, ulen;
  guint end;

  g_return_if_fail (list != NULL);
  g_return_if_fail (other != NULL);
  g_return_if_fail (pos >= 0);
  g_return_if_fail (len >= 0);

  upos = (guint)pos;
  ulen = (guint)len;

/* This definition only works when a and b are unsigned; overflow
 * isn't defined in the C standard for signed integers
 */
#define CLAMP_ADD(a,b) (((a) + (b) < (a)) ? G_MAXUINT : (a) + (b))

  end = CLAMP_ADD (upos, ulen);

  if (list->attributes)
    for (i = 0, p = list->attributes->len; i < p; i++)
      {
        NsPangoAttribute *attr = g_ptr_array_index (list->attributes, i);;

        if (attr->start_index <= upos)
          {
            if (attr->end_index > upos)
              attr->end_index = CLAMP_ADD (attr->end_index, ulen);
          }
        else
          {
            /* This could result in a zero length attribute if it
             * gets squashed up against G_MAXUINT, but deleting such
             * an element could (in theory) suprise the caller, so
             * we don't delete it.
             */
            attr->start_index = CLAMP_ADD (attr->start_index, ulen);
            attr->end_index = CLAMP_ADD (attr->end_index, ulen);
         }
      }

  if (!other->attributes || other->attributes->len == 0)
    return;

  for (i = 0, p = other->attributes->len; i < p; i++)
    {
      NsPangoAttribute *attr = ns_pango_attribute_copy (g_ptr_array_index (other->attributes, i));
      if (ulen > 0)
        {
          attr->start_index = MIN (CLAMP_ADD (attr->start_index, upos), end);
          attr->end_index = MIN (CLAMP_ADD (attr->end_index, upos), end);
        }
      else
        {
          attr->start_index = CLAMP_ADD (attr->start_index, upos);
          attr->end_index = CLAMP_ADD (attr->end_index, upos);
        }

      /* Same as above, the attribute could be squashed to zero-length; here
       * ns_pango_attr_list_change() will take care of deleting it.
       */
      ns_pango_attr_list_change (list, attr);
    }
#undef CLAMP_ADD
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

gboolean
_ns_pango_attr_list_has_attributes (const NsPangoAttrList *list)
{
  return list && list->attributes != NULL && list->attributes->len > 0;
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

/* We serialize attribute lists to strings. The format
 * is a comma-separated list of the attributes in the order
 * in which they are in the list, with each attribute having
 * this format:
 *
 * START END NICK VALUE
 *
 * Values that can contain a comma, such as font descriptions
 * are quoted with "".
 */

static const char *
get_attr_type_nick (NsPangoAttrType attr_type)
{
  GEnumClass *enum_class;
  GEnumValue *enum_value;

  enum_class = g_type_class_ref (ns_pango_attr_type_get_type ());
  enum_value = g_enum_get_value (enum_class, attr_type);
  g_type_class_unref (enum_class);

  return enum_value->value_nick;
}

static GType
get_attr_value_type (NsPangoAttrType type)
{
  switch ((int)type)
    {
    case NS_PANGO_ATTR_STYLE: return NS_TYPE_PANGO_STYLE;
    case NS_PANGO_ATTR_WEIGHT: return NS_TYPE_PANGO_WEIGHT;
    case NS_PANGO_ATTR_VARIANT: return NS_TYPE_PANGO_VARIANT;
    case NS_PANGO_ATTR_STRETCH: return NS_TYPE_PANGO_STRETCH;
    case NS_PANGO_ATTR_WIDTH: return NS_TYPE_PANGO_WIDTH;
    case NS_PANGO_ATTR_GRAVITY: return NS_TYPE_PANGO_GRAVITY;
    case NS_PANGO_ATTR_GRAVITY_HINT: return NS_TYPE_PANGO_GRAVITY_HINT;
    case NS_PANGO_ATTR_UNDERLINE: return NS_TYPE_PANGO_UNDERLINE;
    case NS_PANGO_ATTR_OVERLINE: return NS_TYPE_PANGO_OVERLINE;
    case NS_PANGO_ATTR_BASELINE_SHIFT: return NS_TYPE_PANGO_BASELINE_SHIFT;
    case NS_PANGO_ATTR_FONT_SCALE: return NS_TYPE_PANGO_FONT_SCALE;
    case NS_PANGO_ATTR_TEXT_TRANSFORM: return NS_TYPE_PANGO_TEXT_TRANSFORM;
    default: return G_TYPE_INVALID;
    }
}

static void
append_enum_value (GString *str,
                   GType    type,
                   int      value)
{
  GEnumClass *enum_class;
  GEnumValue *enum_value;

  enum_class = g_type_class_ref (type);
  enum_value = g_enum_get_value (enum_class, value);
  g_type_class_unref (enum_class);

  if (enum_value)
    g_string_append_printf (str, " %s", enum_value->value_nick);
  else
    g_string_append_printf (str, " %d", value);
}

static void
attr_print (GString        *str,
            NsPangoAttribute *attr)
{
  NsPangoAttrString *string;
  NsPangoAttrLanguage *lang;
  NsPangoAttrInt *integer;
  NsPangoAttrFloat *flt;
  NsPangoAttrFontDesc *font;
  NsPangoAttrColor *color;
  NsPangoAttrShape *shape;
  NsPangoAttrSize *size;
  NsPangoAttrFontFeatures *features;

  g_string_append_printf (str, "%u %u ", attr->start_index, attr->end_index);

  g_string_append (str, get_attr_type_nick (attr->klass->type));

  if (attr->klass->type == NS_PANGO_ATTR_WEIGHT ||
      attr->klass->type == NS_PANGO_ATTR_STYLE ||
      attr->klass->type == NS_PANGO_ATTR_STRETCH ||
      attr->klass->type == NS_PANGO_ATTR_WIDTH ||
      attr->klass->type == NS_PANGO_ATTR_VARIANT ||
      attr->klass->type == NS_PANGO_ATTR_GRAVITY ||
      attr->klass->type == NS_PANGO_ATTR_GRAVITY_HINT ||
      attr->klass->type == NS_PANGO_ATTR_UNDERLINE ||
      attr->klass->type == NS_PANGO_ATTR_OVERLINE ||
      attr->klass->type == NS_PANGO_ATTR_BASELINE_SHIFT ||
      attr->klass->type == NS_PANGO_ATTR_FONT_SCALE ||
      attr->klass->type == NS_PANGO_ATTR_TEXT_TRANSFORM)
    append_enum_value (str, get_attr_value_type (attr->klass->type), ((NsPangoAttrInt *)attr)->value);
  else if (attr->klass->type == NS_PANGO_ATTR_STRIKETHROUGH ||
           attr->klass->type == NS_PANGO_ATTR_ALLOW_BREAKS ||
           attr->klass->type == NS_PANGO_ATTR_INSERT_HYPHENS ||
           attr->klass->type == NS_PANGO_ATTR_FALLBACK)
    g_string_append (str, ((NsPangoAttrInt *)attr)->value ? " true" : " false");
  else if ((string = ns_pango_attribute_as_string (attr)) != NULL)
    {
      char *s = g_strescape (string->value, NULL);
      g_string_append_printf (str, " \"%s\"", s);
      g_free (s);
    }
  else if ((lang = ns_pango_attribute_as_language (attr)) != NULL)
    g_string_append_printf (str, " %s", ns_pango_language_to_string (lang->value));
  else if ((integer = ns_pango_attribute_as_int (attr)) != NULL)
    g_string_append_printf (str, " %d", integer->value);
  else if ((flt = ns_pango_attribute_as_float (attr)) != NULL)
    {
      char buf[20];
      g_ascii_formatd (buf, 20, "%f", flt->value);
      g_string_append_printf (str, " %s", buf);
    }
  else if ((font = ns_pango_attribute_as_font_desc (attr)) != NULL)
    {
      char *s = ns_pango_font_description_to_string (font->desc);
      char *s2 = g_strescape (s, NULL);
      g_string_append_printf (str, " \"%s\"", s2);
      g_free (s2);
      g_free (s);
    }
  else if ((color = ns_pango_attribute_as_color (attr)) != NULL)
    {
      char *s = ns_pango_color_to_string (&color->color);
      g_string_append_printf (str, " %s", s);
      g_free (s);
    }
  else if ((shape = ns_pango_attribute_as_shape (attr)) != NULL)
    g_string_append (str, "shape"); /* FIXME */
  else if ((size = ns_pango_attribute_as_size (attr)) != NULL)
    g_string_append_printf (str, " %d", size->size);
  else if ((features = ns_pango_attribute_as_font_features (attr)) != NULL)
    g_string_append_printf (str, " \"%s\"", features->features);
  else
    g_assert_not_reached ();
}

/**
 * ns_pango_attr_list_to_string:
 * @list: a `NsPangoAttrList`
 *
 * Serializes a `NsPangoAttrList` to a string.
 *
 * In the resulting string, serialized attributes are separated by newlines or commas.
 * Individual attributes are serialized to a string of the form
 *
 *     [START END] TYPE VALUE
 *
 * Where START and END are the indices (with -1 being accepted in place
 * of MAXUINT), TYPE is the nickname of the attribute value type, e.g.
 * _weight_ or _stretch_, and the value is serialized according to its type:
 *
 * Optionally, START and END can be omitted to indicate unlimited extent.
 *
 * - enum values as nick or numeric value
 * - boolean values as _true_ or _false_
 * - integers and floats as numbers
 * - strings as string, optionally quoted
 * - font features as quoted string
 * - NsPangoLanguage as string
 * - NsPangoFontDescription as serialized by [method@Pango.FontDescription.to_string], quoted
 * - NsPangoColor as serialized by [method@Pango.Color.to_string]
 *
 * Examples:
 *
 *     0 10 foreground red, 5 15 weight bold, 0 200 font-desc "Sans 10"
 *
 *     0 -1 weight 700
 *     0 100 family Times
 *
 *     weight bold
 *
 * To parse the returned value, use [func@Pango.AttrList.from_string].
 *
 * Note that shape attributes can not be serialized.
 *
 * Returns: (transfer full): a newly allocated string
 * Since: 1.50
 */
char *
ns_pango_attr_list_to_string (NsPangoAttrList *list)
{
  GString *s;

  s = g_string_new ("");

  if (list->attributes)
    for (int i = 0; i < list->attributes->len; i++)
      {
        NsPangoAttribute *attr = g_ptr_array_index (list->attributes, i);

        if (i > 0)
          g_string_append (s, "\n");
        attr_print (s, attr);
      }

  return g_string_free (s, FALSE);
}

static NsPangoAttrType
get_attr_type_by_nick (const char *nick,
                       int         len)
{
  GEnumClass *enum_class;

  enum_class = g_type_class_ref (ns_pango_attr_type_get_type ());
  for (GEnumValue *ev = enum_class->values; ev->value_name; ev++)
    {
      if (ev->value_nick && strncmp (ev->value_nick, nick, len) == 0)
        {
          g_type_class_unref (enum_class);
          return (NsPangoAttrType) ev->value;
        }
    }

  g_type_class_unref (enum_class);
  return NS_PANGO_ATTR_INVALID;
}

static int
get_attr_value (NsPangoAttrType  type,
                const char    *str,
                int            len)
{
  GEnumClass *enum_class;
  char *endp;
  int value;

  enum_class = g_type_class_ref (get_attr_value_type (type));
  for (GEnumValue *ev = enum_class->values; ev->value_name; ev++)
    {
      if (ev->value_nick && strncmp (ev->value_nick, str, len) == 0)
        {
          g_type_class_unref (enum_class);
          return ev->value;
        }
    }
  g_type_class_unref (enum_class);

  value = g_ascii_strtoll (str, &endp, 10);
  if (endp - str == len)
    return value;

  return -1;
}

static gboolean
is_valid_end_char (char c)
{
  return c == ',' || c == '\n' || c == '\0';
}

/**
 * ns_pango_attr_list_from_string:
 * @text: a string
 *
 * Deserializes a `NsPangoAttrList` from a string.
 *
 * This is the counterpart to [method@Pango.AttrList.to_string].
 * See that functions for details about the format.
 *
 * Returns: (transfer full) (nullable): a new `NsPangoAttrList`
 * Since: 1.50
 */
NsPangoAttrList *
ns_pango_attr_list_from_string (const char *text)
{
  NsPangoAttrList *list;
  const char *p;

  g_return_val_if_fail (text != NULL, NULL);

  list = ns_pango_attr_list_new ();

  if (*text == '\0')
    return list;

  list->attributes = g_ptr_array_new ();

  p = text + strspn (text, " \t\n");
  while (*p)
    {
      const char *endp;
      char *endtok;
      gint64 start_index;
      gint64 end_index;
      char *str;
      NsPangoAttrType attr_type;
      NsPangoAttribute *attr;
      NsPangoLanguage *lang;
      gint64 integer;
      NsPangoColor color;
      double num;
      int len;

      if g_ascii_isdigit (p[0])
        {
          start_index = g_ascii_strtoll (p, &endtok, 10);
          if (*endtok != ' ')
            goto fail;
          endp = endtok;

          p = endp + strspn (endp, " ");
          if (!*p)
            goto fail;
          endp = endtok;

          end_index = g_ascii_strtoll (p, &endtok, 10);
          if (*endtok != ' ')
            goto fail;
          endp = endtok;

          p = endp + strspn (endp, " ");
        }
      else
        {
          /* START and END are omitted */
          start_index = NS_PANGO_ATTR_INDEX_FROM_TEXT_BEGINNING;
          end_index = NS_PANGO_ATTR_INDEX_TO_TEXT_END;
        }

      endp = (char *)p + strcspn (p, " ");
      attr_type = get_attr_type_by_nick (p, endp - p);

      p = endp + strspn (endp, " ");
      if (*p == '\0')
        goto fail;

#define INT_ATTR(name) \
          integer = g_ascii_strtoll (p, &endtok, 10); \
          if (!is_valid_end_char (*endtok)) goto fail; \
          attr = ns_pango_attr_##name##_new ((int)integer); \
          endp = endtok;

#define INT_ATTR_ABS(name) \
          integer = g_ascii_strtoll (p, &endtok, 10); \
          if (!is_valid_end_char (*endtok)) goto fail; \
          attr = ns_pango_attr_##name##_new_absolute ((int)integer); \
          endp = endtok;

#define MARK_ATTR(name) \
          integer = g_ascii_strtoll (p, &endtok, 10); \
          if (!is_valid_end_char (*endtok)) goto fail; \
          attr = ns_pango_attr_##name##_new (); \
          endp = endtok;

#define BOOLEAN_ATTR(name,type) \
          if (strncmp (p, "true", strlen ("true")) == 0) \
            { \
              integer = 1; \
              endp = (char *)(p + strlen ("true")); \
            } \
          else if (strncmp (p, "false", strlen ("false")) == 0) \
            { \
              integer = 0; \
              endp = (char *)(p + strlen ("false")); \
            } \
          else \
            { \
              integer = g_ascii_strtoll (p, &endtok, 10); \
              endp = endtok; \
            } \
          if (!is_valid_end_char (*endp)) goto fail; \
          attr = ns_pango_attr_##name##_new ((type)integer);

#define ENUM_ATTR(name, type, min, max) \
          endp = (char *)p + strcspn (p, ",\n"); \
          len = endp - p; \
          while (len > 0 && p[len - 1] == ' ') \
            len--; \
          integer = get_attr_value (attr_type, p, len); \
          attr = ns_pango_attr_##name##_new ((type) CLAMP (integer, min, max));

#define FLAGS_ATTR(name,type) \
          integer = g_ascii_strtoll (p, &endtok, 10); \
          if (!is_valid_end_char (*endtok)) goto fail; \
          attr = ns_pango_attr_##name##_new ((type)integer); \
          endp = endtok;

#define FLOAT_ATTR(name) \
          num = g_ascii_strtod (p, &endtok); \
          if (!is_valid_end_char (*endtok)) goto fail; \
          attr = ns_pango_attr_##name##_new ((float)num); \
          endp = endtok;

#define COLOR_ATTR(name) \
          endp = (char *)p + strcspn (p, ",\n"); \
          if (!is_valid_end_char (*endp)) goto fail; \
          str = g_strndup (p, endp - p); \
          if (!ns_pango_color_parse (&color, str)) \
            { \
              g_free (str); \
              goto fail; \
            } \
          attr = ns_pango_attr_##name##_new (color.red, color.green, color.blue); \
          g_free (str);

#define STRING_ATTR(name) \
          if (*p != '"') goto fail; \
          p++; \
          endp = strchr (p, '"'); \
          if (!endp) goto fail; \
          /* check the next character like other attributes */ \
          if (!is_valid_end_char (*(endp + 1))) goto fail; \
          str = g_strndup (p, endp - p); \
          attr = ns_pango_attr_##name##_new (str); \
          g_free (str); \
          endp++;

      switch (attr_type)
        {
        case NS_PANGO_ATTR_INVALID:
          ns_pango_attr_list_unref (list);
          return NULL;

        case NS_PANGO_ATTR_LANGUAGE:
          endp = (char *)p + strcspn (p, ",\n");
          if (!is_valid_end_char (*endp)) goto fail;
          str = g_strndup (p, endp - p);
          lang = ns_pango_language_from_string (str);
          attr = ns_pango_attr_language_new (lang);
          g_free (str);
          break;

        case NS_PANGO_ATTR_FAMILY:
          endp = (char *)p + strcspn (p, ",\n");
          if (!is_valid_end_char (*endp)) goto fail;
          if (p[0] == '"')
            {
              char *str2;

              len = endp - p;
              while (len > 0 && p[len - 1] == ' ')
                len--;

              /* A single quote passes the closing-quote test below against
               * itself, and len - 2 is then -1: as a gsize, an unbounded copy.
               */
              if (len < 2 || p[len - 1] != '"') goto fail;

              str2 = g_strndup (p + 1, len - 2);
              str = g_strcompress (str2);
              g_free (str2);
            }
          else
            str = g_strndup (p, endp - p);
          attr = ns_pango_attr_family_new (str);
          g_free (str);
          break;

        case NS_PANGO_ATTR_STYLE:
          ENUM_ATTR(style, NsPangoStyle, NS_PANGO_STYLE_NORMAL, NS_PANGO_STYLE_ITALIC);
          break;

        case NS_PANGO_ATTR_WEIGHT:
          ENUM_ATTR(weight, NsPangoWeight, NS_PANGO_WEIGHT_THIN, NS_PANGO_WEIGHT_ULTRAHEAVY);
          break;

        case NS_PANGO_ATTR_VARIANT:
          ENUM_ATTR(variant, NsPangoVariant, NS_PANGO_VARIANT_NORMAL, NS_PANGO_VARIANT_TITLE_CAPS);
          break;

        case NS_PANGO_ATTR_STRETCH:
          ENUM_ATTR(stretch, NsPangoStretch, NS_PANGO_STRETCH_ULTRA_CONDENSED, NS_PANGO_STRETCH_ULTRA_EXPANDED);
          break;

        case NS_PANGO_ATTR_WIDTH:
          ENUM_ATTR(width, NsPangoWidth, NS_PANGO_WIDTH_ULTRA_CONDENSED, NS_PANGO_WIDTH_ULTRA_EXPANDED);
          break;

        case NS_PANGO_ATTR_SIZE:
          INT_ATTR(size);
          break;

        case NS_PANGO_ATTR_FONT_DESC:
          STRING_ATTR(font_desc_from_string);
          break;

        case NS_PANGO_ATTR_FOREGROUND:
          COLOR_ATTR(foreground);
          break;

        case NS_PANGO_ATTR_BACKGROUND:
          COLOR_ATTR(background);
          break;

        case NS_PANGO_ATTR_UNDERLINE:
          ENUM_ATTR(underline, NsPangoUnderline, NS_PANGO_UNDERLINE_NONE, NS_PANGO_UNDERLINE_ERROR_LINE);
          break;

        case NS_PANGO_ATTR_STRIKETHROUGH:
          BOOLEAN_ATTR(strikethrough, gboolean);
          break;

        case NS_PANGO_ATTR_RISE:
          INT_ATTR(rise);
          break;

        case NS_PANGO_ATTR_SHAPE:
          endp = (char *)p + strcspn (p, ",\n");
          continue; /* FIXME */

        case NS_PANGO_ATTR_SCALE:
          FLOAT_ATTR(scale);
          break;

        case NS_PANGO_ATTR_FALLBACK:
          BOOLEAN_ATTR(fallback, gboolean);
          break;

        case NS_PANGO_ATTR_LETTER_SPACING:
          INT_ATTR(letter_spacing);
          break;

        case NS_PANGO_ATTR_WORD_SPACING:
          INT_ATTR(word_spacing);
          break;

        case NS_PANGO_ATTR_UNDERLINE_COLOR:
          COLOR_ATTR(underline_color);
          break;

        case NS_PANGO_ATTR_STRIKETHROUGH_COLOR:
          COLOR_ATTR(strikethrough_color);
          break;

        case NS_PANGO_ATTR_ABSOLUTE_SIZE:
          INT_ATTR_ABS(size);
          break;

        case NS_PANGO_ATTR_GRAVITY:
          ENUM_ATTR(gravity, NsPangoGravity, NS_PANGO_GRAVITY_SOUTH, NS_PANGO_GRAVITY_WEST);
          break;

        case NS_PANGO_ATTR_GRAVITY_HINT:
          ENUM_ATTR(gravity_hint, NsPangoGravityHint, NS_PANGO_GRAVITY_HINT_NATURAL, NS_PANGO_GRAVITY_HINT_LINE);
          break;

        case NS_PANGO_ATTR_FONT_FEATURES:
          STRING_ATTR(font_features);
          break;

        case NS_PANGO_ATTR_FOREGROUND_ALPHA:
          INT_ATTR(foreground_alpha);
          break;

        case NS_PANGO_ATTR_BACKGROUND_ALPHA:
          INT_ATTR(background_alpha);
          break;

        case NS_PANGO_ATTR_ALLOW_BREAKS:
          BOOLEAN_ATTR(allow_breaks, gboolean);
          break;

        case NS_PANGO_ATTR_SHOW:
          FLAGS_ATTR(show, NsPangoShowFlags);
          break;

        case NS_PANGO_ATTR_INSERT_HYPHENS:
          BOOLEAN_ATTR(insert_hyphens, gboolean);
          break;

        case NS_PANGO_ATTR_OVERLINE:
          ENUM_ATTR(overline, NsPangoOverline, NS_PANGO_OVERLINE_NONE, NS_PANGO_OVERLINE_SINGLE);
          break;

        case NS_PANGO_ATTR_OVERLINE_COLOR:
          COLOR_ATTR(overline_color);
          break;

        case NS_PANGO_ATTR_LINE_HEIGHT:
          FLOAT_ATTR(line_height);
          break;

        case NS_PANGO_ATTR_ABSOLUTE_LINE_HEIGHT:
          INT_ATTR_ABS(line_height);
          break;

        case NS_PANGO_ATTR_TEXT_TRANSFORM:
          ENUM_ATTR(text_transform, NsPangoTextTransform, NS_PANGO_TEXT_TRANSFORM_NONE, NS_PANGO_TEXT_TRANSFORM_CAPITALIZE);
          break;

        case NS_PANGO_ATTR_WORD:
          MARK_ATTR(word);
          break;

        case NS_PANGO_ATTR_SENTENCE:
          MARK_ATTR(sentence);
          break;

        case NS_PANGO_ATTR_BASELINE_SHIFT:
          ENUM_ATTR(baseline_shift, NsPangoBaselineShift, 0, G_MAXINT);
          break;

        case NS_PANGO_ATTR_FONT_SCALE:
          ENUM_ATTR(font_scale, NsPangoFontScale, NS_PANGO_FONT_SCALE_NONE, NS_PANGO_FONT_SCALE_SMALL_CAPS);
          break;

        default:
          g_assert_not_reached ();
        }

      attr->start_index = (guint)start_index;
      attr->end_index = (guint)end_index;
      g_ptr_array_add (list->attributes, attr);

      p = endp;
      if (*p)
        {
          if (*p == ',')
            p++;
          p += strspn (p, " \n");
        }
    }

  goto success;

fail:
  ns_pango_attr_list_unref (list);
  list = NULL;

success:
  return list;
}

/* }}} */
/* {{{ Attribute Iterator */

G_DEFINE_BOXED_TYPE (NsPangoAttrIterator,
                     ns_pango_attr_iterator,
                     ns_pango_attr_iterator_copy,
                     ns_pango_attr_iterator_destroy)

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
