/* Pango
 * pango-script.h: Script tag handling
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

#ifndef __NS_PANGO_SCRIPT_H__
#define __NS_PANGO_SCRIPT_H__

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * NsPangoScriptIter:
 *
 * A `NsPangoScriptIter` is used to iterate through a string
 * and identify ranges in different scripts.
 **/
typedef struct _PangoScriptIter NsPangoScriptIter;

/**
 * NsPangoScript:
 * @NS_PANGO_SCRIPT_INVALID_CODE: a value never returned from ns_pango_script_for_unichar()
 * @NS_PANGO_SCRIPT_COMMON: a character used by multiple different scripts
 * @NS_PANGO_SCRIPT_INHERITED: a mark glyph that takes its script from the
 * base glyph to which it is attached
 * @NS_PANGO_SCRIPT_ARABIC:        Arabic
 * @NS_PANGO_SCRIPT_ARMENIAN: Armenian
 * @NS_PANGO_SCRIPT_BENGALI:       Bengali
 * @NS_PANGO_SCRIPT_BOPOMOFO: Bopomofo
 * @NS_PANGO_SCRIPT_CHEROKEE:      Cherokee
 * @NS_PANGO_SCRIPT_COPTIC:        Coptic
 * @NS_PANGO_SCRIPT_CYRILLIC:      Cyrillic
 * @NS_PANGO_SCRIPT_DESERET:       Deseret
 * @NS_PANGO_SCRIPT_DEVANAGARI:    Devanagari
 * @NS_PANGO_SCRIPT_ETHIOPIC:      Ethiopic
 * @NS_PANGO_SCRIPT_GEORGIAN:      Georgian
 * @NS_PANGO_SCRIPT_GOTHIC:        Gothic
 * @NS_PANGO_SCRIPT_GREEK:         Greek
 * @NS_PANGO_SCRIPT_GUJARATI:      Gujarati
 * @NS_PANGO_SCRIPT_GURMUKHI:      Gurmukhi
 * @NS_PANGO_SCRIPT_HAN:   Han
 * @NS_PANGO_SCRIPT_HANGUL:        Hangul
 * @NS_PANGO_SCRIPT_HEBREW:        Hebrew
 * @NS_PANGO_SCRIPT_HIRAGANA:      Hiragana
 * @NS_PANGO_SCRIPT_KANNADA:       Kannada
 * @NS_PANGO_SCRIPT_KATAKANA:      Katakana
 * @NS_PANGO_SCRIPT_KHMER:         Khmer
 * @NS_PANGO_SCRIPT_LAO:   Lao
 * @NS_PANGO_SCRIPT_LATIN:         Latin
 * @NS_PANGO_SCRIPT_MALAYALAM:     Malayalam
 * @NS_PANGO_SCRIPT_MONGOLIAN:     Mongolian
 * @NS_PANGO_SCRIPT_MYANMAR:       Myanmar
 * @NS_PANGO_SCRIPT_OGHAM:         Ogham
 * @NS_PANGO_SCRIPT_OLD_ITALIC:    Old Italic
 * @NS_PANGO_SCRIPT_ORIYA:         Oriya
 * @NS_PANGO_SCRIPT_RUNIC:         Runic
 * @NS_PANGO_SCRIPT_SINHALA:       Sinhala
 * @NS_PANGO_SCRIPT_SYRIAC:        Syriac
 * @NS_PANGO_SCRIPT_TAMIL:         Tamil
 * @NS_PANGO_SCRIPT_TELUGU:        Telugu
 * @NS_PANGO_SCRIPT_THAANA:        Thaana
 * @NS_PANGO_SCRIPT_THAI:  Thai
 * @NS_PANGO_SCRIPT_TIBETAN:       Tibetan
 * @NS_PANGO_SCRIPT_CANADIAN_ABORIGINAL:   Canadian Aboriginal
 * @NS_PANGO_SCRIPT_YI:    Yi
 * @NS_PANGO_SCRIPT_TAGALOG:       Tagalog
 * @NS_PANGO_SCRIPT_HANUNOO:       Hanunoo
 * @NS_PANGO_SCRIPT_BUHID:         Buhid
 * @NS_PANGO_SCRIPT_TAGBANWA:      Tagbanwa
 * @NS_PANGO_SCRIPT_BRAILLE:       Braille
 * @NS_PANGO_SCRIPT_CYPRIOT:       Cypriot
 * @NS_PANGO_SCRIPT_LIMBU:         Limbu
 * @NS_PANGO_SCRIPT_OSMANYA:       Osmanya
 * @NS_PANGO_SCRIPT_SHAVIAN:       Shavian
 * @NS_PANGO_SCRIPT_LINEAR_B:      Linear B
 * @NS_PANGO_SCRIPT_TAI_LE:        Tai Le
 * @NS_PANGO_SCRIPT_UGARITIC:      Ugaritic
 * @NS_PANGO_SCRIPT_NEW_TAI_LUE:   New Tai Lue. Since 1.10
 * @NS_PANGO_SCRIPT_BUGINESE:      Buginese. Since 1.10
 * @NS_PANGO_SCRIPT_GLAGOLITIC:    Glagolitic. Since 1.10
 * @NS_PANGO_SCRIPT_TIFINAGH:      Tifinagh. Since 1.10
 * @NS_PANGO_SCRIPT_SYLOTI_NAGRI:  Syloti Nagri. Since 1.10
 * @NS_PANGO_SCRIPT_OLD_PERSIAN:   Old Persian. Since 1.10
 * @NS_PANGO_SCRIPT_KHAROSHTHI:    Kharoshthi. Since 1.10
 * @NS_PANGO_SCRIPT_UNKNOWN:               an unassigned code point. Since 1.14
 * @NS_PANGO_SCRIPT_BALINESE:              Balinese. Since 1.14
 * @NS_PANGO_SCRIPT_CUNEIFORM:     Cuneiform. Since 1.14
 * @NS_PANGO_SCRIPT_PHOENICIAN:    Phoenician. Since 1.14
 * @NS_PANGO_SCRIPT_PHAGS_PA:              Phags-pa. Since 1.14
 * @NS_PANGO_SCRIPT_NKO:           N'Ko. Since 1.14
 * @NS_PANGO_SCRIPT_KAYAH_LI:   Kayah Li. Since 1.20.1
 * @NS_PANGO_SCRIPT_LEPCHA:     Lepcha. Since 1.20.1
 * @NS_PANGO_SCRIPT_REJANG:     Rejang. Since 1.20.1
 * @NS_PANGO_SCRIPT_SUNDANESE:  Sundanese. Since 1.20.1
 * @NS_PANGO_SCRIPT_SAURASHTRA: Saurashtra. Since 1.20.1
 * @NS_PANGO_SCRIPT_CHAM:       Cham. Since 1.20.1
 * @NS_PANGO_SCRIPT_OL_CHIKI:   Ol Chiki. Since 1.20.1
 * @NS_PANGO_SCRIPT_VAI:        Vai. Since 1.20.1
 * @NS_PANGO_SCRIPT_CARIAN:     Carian. Since 1.20.1
 * @NS_PANGO_SCRIPT_LYCIAN:     Lycian. Since 1.20.1
 * @NS_PANGO_SCRIPT_LYDIAN:     Lydian. Since 1.20.1
 * @NS_PANGO_SCRIPT_BATAK:      Batak. Since 1.32
 * @NS_PANGO_SCRIPT_BRAHMI:     Brahmi. Since 1.32
 * @NS_PANGO_SCRIPT_MANDAIC:    Mandaic. Since 1.32
 * @NS_PANGO_SCRIPT_CHAKMA:               Chakma. Since: 1.32
 * @NS_PANGO_SCRIPT_MEROITIC_CURSIVE:     Meroitic Cursive. Since: 1.32
 * @NS_PANGO_SCRIPT_MEROITIC_HIEROGLYPHS: Meroitic Hieroglyphs. Since: 1.32
 * @NS_PANGO_SCRIPT_MIAO:                 Miao. Since: 1.32
 * @NS_PANGO_SCRIPT_SHARADA:              Sharada. Since: 1.32
 * @NS_PANGO_SCRIPT_SORA_SOMPENG:         Sora Sompeng. Since: 1.32
 * @NS_PANGO_SCRIPT_TAKRI:                Takri. Since: 1.32
 * @NS_PANGO_SCRIPT_BASSA_VAH:            Bassa. Since: 1.40
 * @NS_PANGO_SCRIPT_CAUCASIAN_ALBANIAN:   Caucasian Albanian. Since: 1.40
 * @NS_PANGO_SCRIPT_DUPLOYAN:             Duployan. Since: 1.40
 * @NS_PANGO_SCRIPT_ELBASAN:              Elbasan. Since: 1.40
 * @NS_PANGO_SCRIPT_GRANTHA:              Grantha. Since: 1.40
 * @NS_PANGO_SCRIPT_KHOJKI:               Kjohki. Since: 1.40
 * @NS_PANGO_SCRIPT_KHUDAWADI:            Khudawadi, Sindhi. Since: 1.40
 * @NS_PANGO_SCRIPT_LINEAR_A:             Linear A. Since: 1.40
 * @NS_PANGO_SCRIPT_MAHAJANI:             Mahajani. Since: 1.40
 * @NS_PANGO_SCRIPT_MANICHAEAN:           Manichaean. Since: 1.40
 * @NS_PANGO_SCRIPT_MENDE_KIKAKUI:        Mende Kikakui. Since: 1.40
 * @NS_PANGO_SCRIPT_MODI:                 Modi. Since: 1.40
 * @NS_PANGO_SCRIPT_MRO:                  Mro. Since: 1.40
 * @NS_PANGO_SCRIPT_NABATAEAN:            Nabataean. Since: 1.40
 * @NS_PANGO_SCRIPT_OLD_NORTH_ARABIAN:    Old North Arabian. Since: 1.40
 * @NS_PANGO_SCRIPT_OLD_PERMIC:           Old Permic. Since: 1.40
 * @NS_PANGO_SCRIPT_PAHAWH_HMONG:         Pahawh Hmong. Since: 1.40
 * @NS_PANGO_SCRIPT_PALMYRENE:            Palmyrene. Since: 1.40
 * @NS_PANGO_SCRIPT_PAU_CIN_HAU:          Pau Cin Hau. Since: 1.40
 * @NS_PANGO_SCRIPT_PSALTER_PAHLAVI:      Psalter Pahlavi. Since: 1.40
 * @NS_PANGO_SCRIPT_SIDDHAM:              Siddham. Since: 1.40
 * @NS_PANGO_SCRIPT_TIRHUTA:              Tirhuta. Since: 1.40
 * @NS_PANGO_SCRIPT_WARANG_CITI:          Warang Citi. Since: 1.40
 * @NS_PANGO_SCRIPT_AHOM:                 Ahom. Since: 1.40
 * @NS_PANGO_SCRIPT_ANATOLIAN_HIEROGLYPHS: Anatolian Hieroglyphs. Since: 1.40
 * @NS_PANGO_SCRIPT_HATRAN:               Hatran. Since: 1.40
 * @NS_PANGO_SCRIPT_MULTANI:              Multani. Since: 1.40
 * @NS_PANGO_SCRIPT_OLD_HUNGARIAN:        Old Hungarian. Since: 1.40
 * @NS_PANGO_SCRIPT_SIGNWRITING:          Signwriting. Since: 1.40
 *
 * The `NsPangoScript` enumeration identifies different writing
 * systems.
 *
 * The values correspond to the names as defined in the Unicode standard. See
 * [Unicode Standard Annex 24: Script names](http://www.unicode.org/reports/tr24/)
 *
 * Note that this enumeration is deprecated and will not be updated to include values
 * in newer versions of the Unicode standard. Applications should use the
 * [enum@GLib.UnicodeScript] enumeration instead,
 * whose values are interchangeable with `NsPangoScript`.
 */
typedef enum {                         /* ISO 15924 code */
      NS_PANGO_SCRIPT_INVALID_CODE = -1,
      NS_PANGO_SCRIPT_COMMON       = 0,   /* Zyyy */
      NS_PANGO_SCRIPT_INHERITED,          /* Qaai */
      NS_PANGO_SCRIPT_ARABIC,             /* Arab */
      NS_PANGO_SCRIPT_ARMENIAN,           /* Armn */
      NS_PANGO_SCRIPT_BENGALI,            /* Beng */
      NS_PANGO_SCRIPT_BOPOMOFO,           /* Bopo */
      NS_PANGO_SCRIPT_CHEROKEE,           /* Cher */
      NS_PANGO_SCRIPT_COPTIC,             /* Qaac */
      NS_PANGO_SCRIPT_CYRILLIC,           /* Cyrl (Cyrs) */
      NS_PANGO_SCRIPT_DESERET,            /* Dsrt */
      NS_PANGO_SCRIPT_DEVANAGARI,         /* Deva */
      NS_PANGO_SCRIPT_ETHIOPIC,           /* Ethi */
      NS_PANGO_SCRIPT_GEORGIAN,           /* Geor (Geon, Geoa) */
      NS_PANGO_SCRIPT_GOTHIC,             /* Goth */
      NS_PANGO_SCRIPT_GREEK,              /* Grek */
      NS_PANGO_SCRIPT_GUJARATI,           /* Gujr */
      NS_PANGO_SCRIPT_GURMUKHI,           /* Guru */
      NS_PANGO_SCRIPT_HAN,                /* Hani */
      NS_PANGO_SCRIPT_HANGUL,             /* Hang */
      NS_PANGO_SCRIPT_HEBREW,             /* Hebr */
      NS_PANGO_SCRIPT_HIRAGANA,           /* Hira */
      NS_PANGO_SCRIPT_KANNADA,            /* Knda */
      NS_PANGO_SCRIPT_KATAKANA,           /* Kana */
      NS_PANGO_SCRIPT_KHMER,              /* Khmr */
      NS_PANGO_SCRIPT_LAO,                /* Laoo */
      NS_PANGO_SCRIPT_LATIN,              /* Latn (Latf, Latg) */
      NS_PANGO_SCRIPT_MALAYALAM,          /* Mlym */
      NS_PANGO_SCRIPT_MONGOLIAN,          /* Mong */
      NS_PANGO_SCRIPT_MYANMAR,            /* Mymr */
      NS_PANGO_SCRIPT_OGHAM,              /* Ogam */
      NS_PANGO_SCRIPT_OLD_ITALIC,         /* Ital */
      NS_PANGO_SCRIPT_ORIYA,              /* Orya */
      NS_PANGO_SCRIPT_RUNIC,              /* Runr */
      NS_PANGO_SCRIPT_SINHALA,            /* Sinh */
      NS_PANGO_SCRIPT_SYRIAC,             /* Syrc (Syrj, Syrn, Syre) */
      NS_PANGO_SCRIPT_TAMIL,              /* Taml */
      NS_PANGO_SCRIPT_TELUGU,             /* Telu */
      NS_PANGO_SCRIPT_THAANA,             /* Thaa */
      NS_PANGO_SCRIPT_THAI,               /* Thai */
      NS_PANGO_SCRIPT_TIBETAN,            /* Tibt */
      NS_PANGO_SCRIPT_CANADIAN_ABORIGINAL, /* Cans */
      NS_PANGO_SCRIPT_YI,                 /* Yiii */
      NS_PANGO_SCRIPT_TAGALOG,            /* Tglg */
      NS_PANGO_SCRIPT_HANUNOO,            /* Hano */
      NS_PANGO_SCRIPT_BUHID,              /* Buhd */
      NS_PANGO_SCRIPT_TAGBANWA,           /* Tagb */

      /* Unicode-4.0 additions */
      NS_PANGO_SCRIPT_BRAILLE,            /* Brai */
      NS_PANGO_SCRIPT_CYPRIOT,            /* Cprt */
      NS_PANGO_SCRIPT_LIMBU,              /* Limb */
      NS_PANGO_SCRIPT_OSMANYA,            /* Osma */
      NS_PANGO_SCRIPT_SHAVIAN,            /* Shaw */
      NS_PANGO_SCRIPT_LINEAR_B,           /* Linb */
      NS_PANGO_SCRIPT_TAI_LE,             /* Tale */
      NS_PANGO_SCRIPT_UGARITIC,           /* Ugar */

      /* Unicode-4.1 additions */
      NS_PANGO_SCRIPT_NEW_TAI_LUE,        /* Talu */
      NS_PANGO_SCRIPT_BUGINESE,           /* Bugi */
      NS_PANGO_SCRIPT_GLAGOLITIC,         /* Glag */
      NS_PANGO_SCRIPT_TIFINAGH,           /* Tfng */
      NS_PANGO_SCRIPT_SYLOTI_NAGRI,       /* Sylo */
      NS_PANGO_SCRIPT_OLD_PERSIAN,        /* Xpeo */
      NS_PANGO_SCRIPT_KHAROSHTHI,         /* Khar */

      /* Unicode-5.0 additions */
      NS_PANGO_SCRIPT_UNKNOWN,            /* Zzzz */
      NS_PANGO_SCRIPT_BALINESE,           /* Bali */
      NS_PANGO_SCRIPT_CUNEIFORM,          /* Xsux */
      NS_PANGO_SCRIPT_PHOENICIAN,         /* Phnx */
      NS_PANGO_SCRIPT_PHAGS_PA,           /* Phag */
      NS_PANGO_SCRIPT_NKO,                /* Nkoo */

      /* Unicode-5.1 additions */
      NS_PANGO_SCRIPT_KAYAH_LI,           /* Kali */
      NS_PANGO_SCRIPT_LEPCHA,             /* Lepc */
      NS_PANGO_SCRIPT_REJANG,             /* Rjng */
      NS_PANGO_SCRIPT_SUNDANESE,          /* Sund */
      NS_PANGO_SCRIPT_SAURASHTRA,         /* Saur */
      NS_PANGO_SCRIPT_CHAM,               /* Cham */
      NS_PANGO_SCRIPT_OL_CHIKI,           /* Olck */
      NS_PANGO_SCRIPT_VAI,                /* Vaii */
      NS_PANGO_SCRIPT_CARIAN,             /* Cari */
      NS_PANGO_SCRIPT_LYCIAN,             /* Lyci */
      NS_PANGO_SCRIPT_LYDIAN,             /* Lydi */

      /* Unicode-6.0 additions */
      NS_PANGO_SCRIPT_BATAK,              /* Batk */
      NS_PANGO_SCRIPT_BRAHMI,             /* Brah */
      NS_PANGO_SCRIPT_MANDAIC,            /* Mand */

      /* Unicode-6.1 additions */
      NS_PANGO_SCRIPT_CHAKMA,             /* Cakm */
      NS_PANGO_SCRIPT_MEROITIC_CURSIVE,   /* Merc */
      NS_PANGO_SCRIPT_MEROITIC_HIEROGLYPHS,/* Mero */
      NS_PANGO_SCRIPT_MIAO,               /* Plrd */
      NS_PANGO_SCRIPT_SHARADA,            /* Shrd */
      NS_PANGO_SCRIPT_SORA_SOMPENG,       /* Sora */
      NS_PANGO_SCRIPT_TAKRI,              /* Takr */

      /* Unicode 7.0 additions */
      NS_PANGO_SCRIPT_BASSA_VAH,              /* Bass */
      NS_PANGO_SCRIPT_CAUCASIAN_ALBANIAN,     /* Aghb */
      NS_PANGO_SCRIPT_DUPLOYAN,               /* Dupl */
      NS_PANGO_SCRIPT_ELBASAN,                /* Elba */
      NS_PANGO_SCRIPT_GRANTHA,                /* Gran */
      NS_PANGO_SCRIPT_KHOJKI,                 /* Khoj */
      NS_PANGO_SCRIPT_KHUDAWADI,              /* Sind */
      NS_PANGO_SCRIPT_LINEAR_A,               /* Lina */
      NS_PANGO_SCRIPT_MAHAJANI,               /* Mahj */
      NS_PANGO_SCRIPT_MANICHAEAN,             /* Manu */
      NS_PANGO_SCRIPT_MENDE_KIKAKUI,          /* Mend */
      NS_PANGO_SCRIPT_MODI,                   /* Modi */
      NS_PANGO_SCRIPT_MRO,                    /* Mroo */
      NS_PANGO_SCRIPT_NABATAEAN,              /* Nbat */
      NS_PANGO_SCRIPT_OLD_NORTH_ARABIAN,      /* Narb */
      NS_PANGO_SCRIPT_OLD_PERMIC,             /* Perm */
      NS_PANGO_SCRIPT_PAHAWH_HMONG,           /* Hmng */
      NS_PANGO_SCRIPT_PALMYRENE,              /* Palm */
      NS_PANGO_SCRIPT_PAU_CIN_HAU,            /* Pauc */
      NS_PANGO_SCRIPT_PSALTER_PAHLAVI,        /* Phlp */
      NS_PANGO_SCRIPT_SIDDHAM,                /* Sidd */
      NS_PANGO_SCRIPT_TIRHUTA,                /* Tirh */
      NS_PANGO_SCRIPT_WARANG_CITI,            /* Wara */

      /* Unicode 8.0 additions */
      NS_PANGO_SCRIPT_AHOM,                   /* Ahom */
      NS_PANGO_SCRIPT_ANATOLIAN_HIEROGLYPHS,  /* Hluw */
      NS_PANGO_SCRIPT_HATRAN,                 /* Hatr */
      NS_PANGO_SCRIPT_MULTANI,                /* Mult */
      NS_PANGO_SCRIPT_OLD_HUNGARIAN,          /* Hung */
      NS_PANGO_SCRIPT_SIGNWRITING             /* Sgnw */
} NsPangoScript;

#include <ns-pango/pango-version-macros.h>

NS_PANGO_DEPRECATED_IN_1_44_FOR(g_unichar_get_script)
NsPangoScript ns_pango_script_for_unichar         (gunichar             ch) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_1_44
GType            ns_pango_script_iter_get_type  (void) G_GNUC_CONST;

NS_PANGO_AVAILABLE_IN_1_4
NsPangoScriptIter *ns_pango_script_iter_new       (const char          *text,
					      int                  length);
NS_PANGO_AVAILABLE_IN_1_4
void             ns_pango_script_iter_get_range (NsPangoScriptIter     *iter,
                                              const char         **start,
                                              const char         **end,
                                              NsPangoScript         *script);
NS_PANGO_AVAILABLE_IN_1_4
gboolean         ns_pango_script_iter_next      (NsPangoScriptIter     *iter);
NS_PANGO_AVAILABLE_IN_1_4
void             ns_pango_script_iter_free      (NsPangoScriptIter     *iter);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(NsPangoScriptIter, ns_pango_script_iter_free)

#include <ns-pango/pango-language.h>

NS_PANGO_AVAILABLE_IN_1_4
NsPangoLanguage *ns_pango_script_get_sample_language (NsPangoScript    script) G_GNUC_PURE;

G_END_DECLS

#endif /* __NS_PANGO_SCRIPT_H__ */
