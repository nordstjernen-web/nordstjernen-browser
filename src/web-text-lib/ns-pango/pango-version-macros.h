/* Pango - Internationalized text layout and rendering library
 * Copyright (C) 1999 Red Hat Software
 * Copyright (C) 2012 Ryan Lortie, Matthias Clasen and Emmanuele Bassi
 * Copyright (C) 2016 Chun-wei Fan
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __NS_PANGO_VERSION_H__
#define __NS_PANGO_VERSION_H__

#include <ns-pango/pango-features.h>

#include <glib.h>

#ifndef _PANGO_EXTERN
#define _PANGO_EXTERN extern
#endif

#define NS_PANGO_AVAILABLE_IN_ALL                   _PANGO_EXTERN

/* XXX: Every new stable minor release bump should add a macro here */

/**
 * NS_PANGO_VERSION_1_2:
 *
 * A macro that evaluates to the 1.2 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_2        (G_ENCODE_VERSION (1, 2))

/**
 * NS_PANGO_VERSION_1_4:
 *
 * A macro that evaluates to the 1.4 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_4        (G_ENCODE_VERSION (1, 4))

/**
 * NS_PANGO_VERSION_1_6:
 *
 * A macro that evaluates to the 1.6 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_6        (G_ENCODE_VERSION (1, 6))

/**
 * NS_PANGO_VERSION_1_8:
 *
 * A macro that evaluates to the 1.8 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_8        (G_ENCODE_VERSION (1, 8))

/**
 * NS_PANGO_VERSION_1_10:
 *
 * A macro that evaluates to the 1.10 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_10       (G_ENCODE_VERSION (1, 10))

/**
 * NS_PANGO_VERSION_1_12:
 *
 * A macro that evaluates to the 1.12 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_12       (G_ENCODE_VERSION (1, 12))

/**
 * NS_PANGO_VERSION_1_14:
 *
 * A macro that evaluates to the 1.14 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_14       (G_ENCODE_VERSION (1, 14))

/**
 * NS_PANGO_VERSION_1_16:
 *
 * A macro that evaluates to the 1.16 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_16       (G_ENCODE_VERSION (1, 16))

/**
 * NS_PANGO_VERSION_1_18:
 *
 * A macro that evaluates to the 1.18 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_18       (G_ENCODE_VERSION (1, 18))

/**
 * NS_PANGO_VERSION_1_20:
 *
 * A macro that evaluates to the 1.20 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_20       (G_ENCODE_VERSION (1, 20))

/**
 * NS_PANGO_VERSION_1_22:
 *
 * A macro that evaluates to the 1.22 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_22       (G_ENCODE_VERSION (1, 22))

/**
 * NS_PANGO_VERSION_1_24:
 *
 * A macro that evaluates to the 1.24 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_24       (G_ENCODE_VERSION (1, 24))

/**
 * NS_PANGO_VERSION_1_26:
 *
 * A macro that evaluates to the 1.26 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_26       (G_ENCODE_VERSION (1, 26))

/**
 * NS_PANGO_VERSION_1_28:
 *
 * A macro that evaluates to the 1.28 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_28       (G_ENCODE_VERSION (1, 28))

/**
 * NS_PANGO_VERSION_1_30:
 *
 * A macro that evaluates to the 1.30 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_30       (G_ENCODE_VERSION (1, 30))

/**
 * NS_PANGO_VERSION_1_32:
 *
 * A macro that evaluates to the 1.32 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_32       (G_ENCODE_VERSION (1, 32))

/**
 * NS_PANGO_VERSION_1_34:
 *
 * A macro that evaluates to the 1.34 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_34       (G_ENCODE_VERSION (1, 34))

/**
 * NS_PANGO_VERSION_1_36:
 *
 * A macro that evaluates to the 1.36 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_36       (G_ENCODE_VERSION (1, 36))

/**
 * NS_PANGO_VERSION_1_38:
 *
 * A macro that evaluates to the 1.38 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_38       (G_ENCODE_VERSION (1, 38))

/**
 * NS_PANGO_VERSION_1_40:
 *
 * A macro that evaluates to the 1.40 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_40       (G_ENCODE_VERSION (1, 40))

/**
 * NS_PANGO_VERSION_1_42:
 *
 * A macro that evaluates to the 1.42 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.42
 */
#define NS_PANGO_VERSION_1_42       (G_ENCODE_VERSION (1, 42))

/**
 * NS_PANGO_VERSION_1_44:
 *
 * A macro that evaluates to the 1.44 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.44
 */
#define NS_PANGO_VERSION_1_44       (G_ENCODE_VERSION (1, 44))

/**
 * NS_PANGO_VERSION_1_46:
 *
 * A macro that evaluates to the 1.46 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.46
 */
#define NS_PANGO_VERSION_1_46       (G_ENCODE_VERSION (1, 46))

/**
 * NS_PANGO_VERSION_1_48:
 *
 * A macro that evaluates to the 1.48 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.48
 */
#define NS_PANGO_VERSION_1_48       (G_ENCODE_VERSION (1, 48))

/**
 * NS_PANGO_VERSION_1_50:
 *
 * A macro that evaluates to the 1.50 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.50
 */
#define NS_PANGO_VERSION_1_50       (G_ENCODE_VERSION (1, 50))

/**
 * NS_PANGO_VERSION_1_52:
 *
 * A macro that evaluates to the 1.52 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.52
 */
#define NS_PANGO_VERSION_1_52       (G_ENCODE_VERSION (1, 52))

/**
 * NS_PANGO_VERSION_1_54:
 *
 * A macro that evaluates to the 1.54 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.54
 */
#define NS_PANGO_VERSION_1_54       (G_ENCODE_VERSION (1, 54))

/**
 * NS_PANGO_VERSION_1_55:
 *
 * A macro that evaluates to the 1.55 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.55
 */
#define NS_PANGO_VERSION_1_55       (G_ENCODE_VERSION (1, 55))

/**
 * NS_PANGO_VERSION_1_56:
 *
 * A macro that evaluates to the 1.56 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.56
 */
#define NS_PANGO_VERSION_1_56       (G_ENCODE_VERSION (1, 56))

/**
 * NS_PANGO_VERSION_1_57:
 *
 * A macro that evaluates to the 1.57 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.57
 */
#define NS_PANGO_VERSION_1_57       (G_ENCODE_VERSION (1, 57))

/**
 * NS_PANGO_VERSION_1_58:
 *
 * A macro that evaluates to the 1.58 version of Pango, in a format
 * that can be used by the C pre-processor.
 *
 * Since: 1.58
 */
#define NS_PANGO_VERSION_1_58       (G_ENCODE_VERSION (1, 58))

/* evaluates to the current stable version; for development cycles,
 * this means the next stable target
 */
#if (NS_PANGO_VERSION_MINOR % 2)
#define NS_PANGO_VERSION_CUR_STABLE         (G_ENCODE_VERSION (NS_PANGO_VERSION_MAJOR, NS_PANGO_VERSION_MINOR + 1))
#else
#define NS_PANGO_VERSION_CUR_STABLE         (G_ENCODE_VERSION (NS_PANGO_VERSION_MAJOR, NS_PANGO_VERSION_MINOR))
#endif

/* evaluates to the previous stable version */
#if (NS_PANGO_VERSION_MINOR % 2)
#define NS_PANGO_VERSION_PREV_STABLE        (G_ENCODE_VERSION (NS_PANGO_VERSION_MAJOR, NS_PANGO_VERSION_MINOR - 1))
#else
#define NS_PANGO_VERSION_PREV_STABLE        (G_ENCODE_VERSION (NS_PANGO_VERSION_MAJOR, NS_PANGO_VERSION_MINOR - 2))
#endif

/**
 * NS_PANGO_VERSION_MIN_REQUIRED:
 *
 * A macro that should be defined by the user prior to including
 * the pango.h header.
 * The definition should be one of the predefined Pango version
 * macros: %NS_PANGO_VERSION_1_2, %NS_PANGO_VERSION_1_4,...
 *
 * This macro defines the earliest version of Pango that the package is
 * required to be able to compile against.
 *
 * If the compiler is configured to warn about the use of deprecated
 * functions, then using functions that were deprecated in version
 * %NS_PANGO_VERSION_MIN_REQUIRED or earlier will cause warnings (but
 * using functions deprecated in later releases will not).
 *
 * Since: 1.42
 */
/* If the package sets NS_PANGO_VERSION_MIN_REQUIRED to some future
 * NS_PANGO_VERSION_X_Y value that we don't know about, it will compare as
 * 0 in preprocessor tests.
 */
#ifndef NS_PANGO_VERSION_MIN_REQUIRED
# define NS_PANGO_VERSION_MIN_REQUIRED      (NS_PANGO_VERSION_CUR_STABLE)
#elif NS_PANGO_VERSION_MIN_REQUIRED == 0
# undef  NS_PANGO_VERSION_MIN_REQUIRED
# define NS_PANGO_VERSION_MIN_REQUIRED      (NS_PANGO_VERSION_CUR_STABLE + 2)
#endif

/**
 * NS_PANGO_VERSION_MAX_ALLOWED:
 *
 * A macro that should be defined by the user prior to including
 * the glib.h header.
 * The definition should be one of the predefined Pango version
 * macros: %NS_PANGO_VERSION_1_2, %NS_PANGO_VERSION_1_4,...
 *
 * This macro defines the latest version of the Pango API that the
 * package is allowed to make use of.
 *
 * If the compiler is configured to warn about the use of deprecated
 * functions, then using functions added after version
 * %NS_PANGO_VERSION_MAX_ALLOWED will cause warnings.
 *
 * Unless you are using NS_PANGO_VERSION_CHECK() or the like to compile
 * different code depending on the Pango version, then this should be
 * set to the same value as %NS_PANGO_VERSION_MIN_REQUIRED.
 *
 * Since: 1.42
 */
#if !defined (NS_PANGO_VERSION_MAX_ALLOWED) || (NS_PANGO_VERSION_MAX_ALLOWED == 0)
# undef NS_PANGO_VERSION_MAX_ALLOWED
# define NS_PANGO_VERSION_MAX_ALLOWED      (NS_PANGO_VERSION_CUR_STABLE)
#endif

/* sanity checks */
#if NS_PANGO_VERSION_MIN_REQUIRED > NS_PANGO_VERSION_CUR_STABLE
#error "NS_PANGO_VERSION_MIN_REQUIRED must be <= NS_PANGO_VERSION_CUR_STABLE"
#endif
#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_MIN_REQUIRED
#error "NS_PANGO_VERSION_MAX_ALLOWED must be >= NS_PANGO_VERSION_MIN_REQUIRED"
#endif
#if NS_PANGO_VERSION_MIN_REQUIRED < NS_PANGO_VERSION_1_2
#error "NS_PANGO_VERSION_MIN_REQUIRED must be >= NS_PANGO_VERSION_1_2"
#endif

/* These macros are used to mark deprecated functions in Pango headers,
 * and thus have to be exposed in installed headers.
 */
#ifdef NS_PANGO_DISABLE_DEPRECATION_WARNINGS
# define NS_PANGO_DEPRECATED                       _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_FOR(f)                _PANGO_EXTERN
# define NS_PANGO_UNAVAILABLE(maj,min)             _PANGO_EXTERN
#else
# define NS_PANGO_DEPRECATED                       G_DEPRECATED _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_FOR(f)                G_DEPRECATED_FOR(f) _PANGO_EXTERN
# define NS_PANGO_UNAVAILABLE(maj,min)             G_UNAVAILABLE(maj,min) _PANGO_EXTERN
#endif

/* XXX: Every new stable minor release should add a set of macros here */
#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_2
# define NS_PANGO_DEPRECATED_IN_1_2                NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_2_FOR(f)         NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_2                _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_2_FOR(f)         _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_2
# define NS_PANGO_AVAILABLE_IN_1_2                 NS_PANGO_UNAVAILABLE(1, 2)
#else
# define NS_PANGO_AVAILABLE_IN_1_2                 _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_4
# define NS_PANGO_DEPRECATED_IN_1_4                NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_4_FOR(f)         NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_4                _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_4_FOR(f)         _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_4
# define NS_PANGO_AVAILABLE_IN_1_4                 NS_PANGO_UNAVAILABLE(1, 4)
#else
# define NS_PANGO_AVAILABLE_IN_1_4                 _PANGO_EXTERN
#endif


#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_6
# define NS_PANGO_DEPRECATED_IN_1_6                NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_6_FOR(f)         NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_6                _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_6_FOR(f)         _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_6
# define NS_PANGO_AVAILABLE_IN_1_6                 NS_PANGO_UNAVAILABLE(1, 6)
#else
# define NS_PANGO_AVAILABLE_IN_1_6                 _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_8
# define NS_PANGO_DEPRECATED_IN_1_8                NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_8_FOR(f)         NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_8                _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_8_FOR(f)         _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_8
# define NS_PANGO_AVAILABLE_IN_1_8                 NS_PANGO_UNAVAILABLE(1, 8)
#else
# define NS_PANGO_AVAILABLE_IN_1_8                 _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_10
# define NS_PANGO_DEPRECATED_IN_1_10               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_10_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_10               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_10_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_10
# define NS_PANGO_AVAILABLE_IN_1_10                NS_PANGO_UNAVAILABLE(1, 10)
#else
# define NS_PANGO_AVAILABLE_IN_1_10                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_12
# define NS_PANGO_AVAILABLE_IN_1_12                NS_PANGO_UNAVAILABLE(1, 12)
#else
# define NS_PANGO_AVAILABLE_IN_1_12                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_12
# define NS_PANGO_DEPRECATED_IN_1_12               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_12_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_12               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_12_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_14
# define NS_PANGO_AVAILABLE_IN_1_14                NS_PANGO_UNAVAILABLE(1, 14)
#else
# define NS_PANGO_AVAILABLE_IN_1_14                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_14
# define NS_PANGO_DEPRECATED_IN_1_14               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_14_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_14               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_14_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_16
# define NS_PANGO_AVAILABLE_IN_1_16                NS_PANGO_UNAVAILABLE(1, 16)
#else
# define NS_PANGO_AVAILABLE_IN_1_16                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_16
# define NS_PANGO_DEPRECATED_IN_1_16               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_16_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_16               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_16_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_18
# define NS_PANGO_DEPRECATED_IN_1_18               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_18_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_18               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_18_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_18
# define NS_PANGO_AVAILABLE_IN_1_18                NS_PANGO_UNAVAILABLE(1, 18)
#else
# define NS_PANGO_AVAILABLE_IN_1_18                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_20
# define NS_PANGO_DEPRECATED_IN_1_20               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_20_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_20               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_20_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_20
# define NS_PANGO_AVAILABLE_IN_1_20                NS_PANGO_UNAVAILABLE(1, 20)
#else
# define NS_PANGO_AVAILABLE_IN_1_20                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_22
# define NS_PANGO_DEPRECATED_IN_1_22               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_22_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_22               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_22_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_22
# define NS_PANGO_AVAILABLE_IN_1_22                NS_PANGO_UNAVAILABLE(1, 22)
#else
# define NS_PANGO_AVAILABLE_IN_1_22                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_24
# define NS_PANGO_DEPRECATED_IN_1_24               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_24_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_24               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_24_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_24
# define NS_PANGO_AVAILABLE_IN_1_24                NS_PANGO_UNAVAILABLE(1, 24)
#else
# define NS_PANGO_AVAILABLE_IN_1_24                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_26
# define NS_PANGO_DEPRECATED_IN_1_26               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_26_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_26               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_26_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_26
# define NS_PANGO_AVAILABLE_IN_1_26                NS_PANGO_UNAVAILABLE(1, 26)
#else
# define NS_PANGO_AVAILABLE_IN_1_26                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_28
# define NS_PANGO_DEPRECATED_IN_1_28               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_28_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_28               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_28_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_28
# define NS_PANGO_AVAILABLE_IN_1_28                NS_PANGO_UNAVAILABLE(1, 28)
#else
# define NS_PANGO_AVAILABLE_IN_1_28                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_30
# define NS_PANGO_DEPRECATED_IN_1_30               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_30_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_30               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_30_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_30
# define NS_PANGO_AVAILABLE_IN_1_30                NS_PANGO_UNAVAILABLE(1, 30)
#else
# define NS_PANGO_AVAILABLE_IN_1_30                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_32
# define NS_PANGO_DEPRECATED_IN_1_32               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_32_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_32               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_32_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_32
# define NS_PANGO_AVAILABLE_IN_1_32                NS_PANGO_UNAVAILABLE(1, 32)
#else
# define NS_PANGO_AVAILABLE_IN_1_32                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_34
# define NS_PANGO_DEPRECATED_IN_1_34               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_34_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_34               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_34_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_34
# define NS_PANGO_AVAILABLE_IN_1_34                NS_PANGO_UNAVAILABLE(1, 34)
#else
# define NS_PANGO_AVAILABLE_IN_1_34                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_36
# define NS_PANGO_DEPRECATED_IN_1_36               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_36_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_36               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_36_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_36
# define NS_PANGO_AVAILABLE_IN_1_36                NS_PANGO_UNAVAILABLE(1, 36)
#else
# define NS_PANGO_AVAILABLE_IN_1_36                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_38
# define NS_PANGO_DEPRECATED_IN_1_38               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_38_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_38               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_38_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_38
# define NS_PANGO_AVAILABLE_IN_1_38                NS_PANGO_UNAVAILABLE(1, 38)
#else
# define NS_PANGO_AVAILABLE_IN_1_38                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_40
# define NS_PANGO_DEPRECATED_IN_1_40               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_40_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_40               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_40_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_40
# define NS_PANGO_AVAILABLE_IN_1_40                NS_PANGO_UNAVAILABLE(1, 40)
#else
# define NS_PANGO_AVAILABLE_IN_1_40                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_42
# define NS_PANGO_DEPRECATED_IN_1_42               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_42_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_42               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_42_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_42
# define NS_PANGO_AVAILABLE_IN_1_42                NS_PANGO_UNAVAILABLE(1, 42)
#else
# define NS_PANGO_AVAILABLE_IN_1_42                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_44
# define NS_PANGO_DEPRECATED_IN_1_44               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_44_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_44               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_44_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_44
# define NS_PANGO_AVAILABLE_IN_1_44                NS_PANGO_UNAVAILABLE(1, 44)
#else
# define NS_PANGO_AVAILABLE_IN_1_44                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_46
# define NS_PANGO_DEPRECATED_IN_1_46               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_46_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_46               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_46_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_46
# define NS_PANGO_AVAILABLE_IN_1_46                NS_PANGO_UNAVAILABLE(1, 46)
#else
# define NS_PANGO_AVAILABLE_IN_1_46                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_48
# define NS_PANGO_DEPRECATED_IN_1_48               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_48_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_48               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_48_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_48
# define NS_PANGO_AVAILABLE_IN_1_48                NS_PANGO_UNAVAILABLE(1, 48)
#else
# define NS_PANGO_AVAILABLE_IN_1_48                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_50
# define NS_PANGO_DEPRECATED_IN_1_50               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_50_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_50               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_50_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_50
# define NS_PANGO_AVAILABLE_IN_1_50                NS_PANGO_UNAVAILABLE(1, 50)
#else
# define NS_PANGO_AVAILABLE_IN_1_50                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_52
# define NS_PANGO_DEPRECATED_IN_1_52               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_52_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_52               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_52_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_52
# define NS_PANGO_AVAILABLE_IN_1_52                NS_PANGO_UNAVAILABLE(1, 52)
#else
# define NS_PANGO_AVAILABLE_IN_1_52                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_54
# define NS_PANGO_DEPRECATED_IN_1_54               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_54_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_54               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_54_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_54
# define NS_PANGO_AVAILABLE_IN_1_54                NS_PANGO_UNAVAILABLE(1, 54)
#else
# define NS_PANGO_AVAILABLE_IN_1_54                _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_55
# define NS_PANGO_DEPRECATED_IN_1_55               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_55_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_55               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_55_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_55
# define NS_PANGO_AVAILABLE_IN_1_55                NS_PANGO_UNAVAILABLE(1, 55)
# define NS_PANGO_AVAILABLE_ENUMERATOR_IN_1_55     NS_PANGO_UNAVAILABLE (1, 55)
#else
# define NS_PANGO_AVAILABLE_IN_1_55                _PANGO_EXTERN
# define NS_PANGO_AVAILABLE_ENUMERATOR_IN_1_55
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_56
# define NS_PANGO_DEPRECATED_IN_1_56               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_56_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_56               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_56_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_56
# define NS_PANGO_AVAILABLE_IN_1_56                NS_PANGO_UNAVAILABLE(1, 56)
# define NS_PANGO_AVAILABLE_ENUMERATOR_IN_1_56     NS_PANGO_UNAVAILABLE (1, 56)
#else
# define NS_PANGO_AVAILABLE_IN_1_56                _PANGO_EXTERN
# define NS_PANGO_AVAILABLE_ENUMERATOR_IN_1_56
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_57
# define NS_PANGO_DEPRECATED_IN_1_57               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_57_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_57               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_57_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_57
# define NS_PANGO_AVAILABLE_IN_1_57                NS_PANGO_UNAVAILABLE(1, 57)
# define NS_PANGO_AVAILABLE_ENUMERATOR_IN_1_57     NS_PANGO_UNAVAILABLE (1, 57)
#else
# define NS_PANGO_AVAILABLE_IN_1_57                _PANGO_EXTERN
# define NS_PANGO_AVAILABLE_ENUMERATOR_IN_1_57
#endif

#if NS_PANGO_VERSION_MIN_REQUIRED >= NS_PANGO_VERSION_1_58
# define NS_PANGO_DEPRECATED_IN_1_58               NS_PANGO_DEPRECATED
# define NS_PANGO_DEPRECATED_IN_1_58_FOR(f)        NS_PANGO_DEPRECATED_FOR(f)
#else
# define NS_PANGO_DEPRECATED_IN_1_58               _PANGO_EXTERN
# define NS_PANGO_DEPRECATED_IN_1_58_FOR(f)        _PANGO_EXTERN
#endif

#if NS_PANGO_VERSION_MAX_ALLOWED < NS_PANGO_VERSION_1_58
# define NS_PANGO_AVAILABLE_IN_1_58                NS_PANGO_UNAVAILABLE(1, 58)
# define NS_PANGO_AVAILABLE_ENUMERATOR_IN_1_58     NS_PANGO_UNAVAILABLE (1, 58)
#else
# define NS_PANGO_AVAILABLE_IN_1_58                _PANGO_EXTERN
# define NS_PANGO_AVAILABLE_ENUMERATOR_IN_1_58
#endif

#endif /* __NS_PANGO_VERSION_H__ */

