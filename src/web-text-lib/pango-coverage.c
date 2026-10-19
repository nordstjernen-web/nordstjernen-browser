/* Pango
 * pango-coverage.c: Coverage maps for fonts
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

#include "config.h"
#include <string.h>

#include "pango-coverage-private.h"

G_DEFINE_TYPE (NsPangoCoverage, ns_pango_coverage, G_TYPE_OBJECT)

static void
ns_pango_coverage_init (NsPangoCoverage *coverage)
{
}

static void
ns_pango_coverage_finalize (GObject *object)
{
  NsPangoCoverage *coverage = NS_PANGO_COVERAGE (object);

  if (coverage->chars)
    hb_set_destroy (coverage->chars);
  if (coverage->nonchars)
    hb_set_destroy (coverage->nonchars);

  G_OBJECT_CLASS (ns_pango_coverage_parent_class)->finalize (object);
}

static NsPangoCoverageLevel
ns_pango_coverage_real_get (NsPangoCoverage *coverage,
                         int            index)
{
  gunichar ch1, ch2;

  if (coverage->chars == NULL)
    return NS_PANGO_COVERAGE_NONE;

  if (hb_set_has (coverage->chars, (hb_codepoint_t)index))
    return NS_PANGO_COVERAGE_EXACT;

  if (g_unichar_decompose ((gunichar) index, &ch1, &ch2))
    {
      if ((ns_pango_coverage_get (coverage, ch1) == NS_PANGO_COVERAGE_EXACT) &&
          (ch2 == 0 || ns_pango_coverage_get (coverage, ch2) == NS_PANGO_COVERAGE_EXACT))
        {
          ns_pango_coverage_set (coverage, index, NS_PANGO_COVERAGE_EXACT);
          return NS_PANGO_COVERAGE_EXACT;
        }
    }

  ns_pango_coverage_set (coverage, index, NS_PANGO_COVERAGE_NONE);
  return NS_PANGO_COVERAGE_NONE;
}

static void
ns_pango_coverage_real_set (NsPangoCoverage      *coverage,
                         int                 index,
                         NsPangoCoverageLevel  level)
{
  if (level != NS_PANGO_COVERAGE_NONE)
    {
      if (coverage->chars == NULL)
        coverage->chars = hb_set_create ();

      hb_set_add (coverage->chars, (hb_codepoint_t) index);

      if (coverage->nonchars)
        hb_set_del (coverage->nonchars, (hb_codepoint_t) index);
    }
  else
    {
      if (coverage->nonchars == NULL)
        coverage->nonchars = hb_set_create ();

      hb_set_add (coverage->nonchars, (hb_codepoint_t) index);

      if (coverage->chars)
        hb_set_del (coverage->chars, (hb_codepoint_t) index);
    }

}

static NsPangoCoverage *
ns_pango_coverage_real_copy (NsPangoCoverage *coverage)
{
  NsPangoCoverage *copy;

  g_return_val_if_fail (coverage != NULL, NULL);

  copy = g_object_new (NS_TYPE_PANGO_COVERAGE, NULL);

  if (coverage->chars)
    copy->chars = hb_set_copy (coverage->chars);
  if (coverage->nonchars)
    copy->nonchars = hb_set_copy (coverage->nonchars);

  return copy;
}

static void
ns_pango_coverage_class_init (NsPangoCoverageClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = ns_pango_coverage_finalize;

  class->get = ns_pango_coverage_real_get;
  class->set = ns_pango_coverage_real_set;
  class->copy = ns_pango_coverage_real_copy;
}

/**
 * ns_pango_coverage_new:
 *
 * Create a new `NsPangoCoverage`
 *
 * Return value: the newly allocated `NsPangoCoverage`, initialized
 *   to %NS_PANGO_COVERAGE_NONE with a reference count of one, which
 *   should be freed with [method@Pango.Coverage.unref].
 */
NsPangoCoverage *
ns_pango_coverage_new (void)
{
  return g_object_new (NS_TYPE_PANGO_COVERAGE, NULL);
}

/**
 * ns_pango_coverage_copy:
 * @coverage: a `NsPangoCoverage`
 *
 * Copy an existing `NsPangoCoverage`.
 *
 * Return value: (transfer full): the newly allocated `NsPangoCoverage`,
 *   with a reference count of one, which should be freed with
 *   [method@Pango.Coverage.unref].
 */
NsPangoCoverage *
ns_pango_coverage_copy (NsPangoCoverage *coverage)
{
  return NS_PANGO_COVERAGE_GET_CLASS (coverage)->copy (coverage);
}

/**
 * ns_pango_coverage_ref:
 * @coverage: (not nullable): a `NsPangoCoverage`
 *
 * Increase the reference count on the `NsPangoCoverage` by one.
 *
 * Return value: (transfer full): @coverage
 *
 * Deprecated: 1.52: Use g_object_ref instead
 */
NsPangoCoverage *
ns_pango_coverage_ref (NsPangoCoverage *coverage)
{
  return g_object_ref (coverage);
}

/**
 * ns_pango_coverage_unref:
 * @coverage: (transfer full) (not nullable): a `NsPangoCoverage`
 *
 * Decrease the reference count on the `NsPangoCoverage` by one.
 *
 * If the result is zero, free the coverage and all associated memory.
 *
 * Deprecated: 1.52: Use g_object_unref instead
 */
void
ns_pango_coverage_unref (NsPangoCoverage *coverage)
{
  g_object_unref (coverage);
}

/**
 * ns_pango_coverage_get:
 * @coverage: a `NsPangoCoverage`
 * @index_: the index to check
 *
 * Determine whether a particular index is covered by @coverage.
 *
 * Return value: the coverage level of @coverage for character @index_.
 */
NsPangoCoverageLevel
ns_pango_coverage_get (NsPangoCoverage *coverage,
                    int            index)
{
  return NS_PANGO_COVERAGE_GET_CLASS (coverage)->get (coverage, index);
}

/**
 * ns_pango_coverage_set:
 * @coverage: a `NsPangoCoverage`
 * @index_: the index to modify
 * @level: the new level for @index_
 *
 * Modify a particular index within @coverage
 */
void
ns_pango_coverage_set (NsPangoCoverage     *coverage,
                    int                index,
                    NsPangoCoverageLevel level)
{
  NS_PANGO_COVERAGE_GET_CLASS (coverage)->set (coverage, index, level);
}

/**
 * ns_pango_coverage_max:
 * @coverage: a `NsPangoCoverage`
 * @other: another `NsPangoCoverage`
 *
 * Set the coverage for each index in @coverage to be the max (better)
 * value of the current coverage for the index and the coverage for
 * the corresponding index in @other.
 *
 * Deprecated: 1.44: This function does nothing
 */
void
ns_pango_coverage_max (NsPangoCoverage *coverage,
                    NsPangoCoverage *other)
{
}

/**
 * ns_pango_coverage_to_bytes:
 * @coverage: a `NsPangoCoverage`
 * @bytes: (out) (array length=n_bytes) (element-type guint8):
 *   location to store result (must be freed with g_free())
 * @n_bytes: (out): location to store size of result
 *
 * Convert a `NsPangoCoverage` structure into a flat binary format.
 *
 * Deprecated: 1.44: This returns %NULL
 */
void
ns_pango_coverage_to_bytes (NsPangoCoverage  *coverage,
                         guchar        **bytes,
                         int            *n_bytes)
{
  *bytes = NULL;
  *n_bytes = 0;
}

/**
 * ns_pango_coverage_from_bytes:
 * @bytes: (array length=n_bytes) (element-type guint8): binary data
 *   representing a `NsPangoCoverage`
 * @n_bytes: the size of @bytes in bytes
 *
 * Convert data generated from [method@Pango.Coverage.to_bytes]
 * back to a `NsPangoCoverage`.
 *
 * Return value: (transfer full) (nullable): a newly allocated `NsPangoCoverage`
 *
 * Deprecated: 1.44: This returns %NULL
 */
NsPangoCoverage *
ns_pango_coverage_from_bytes (guchar *bytes,
                           int     n_bytes)
{
  return NULL;
}
