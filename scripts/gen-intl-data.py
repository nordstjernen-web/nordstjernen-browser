#!/usr/bin/env python3
# gen-intl-data.py: generate the QuickJS Intl locale data headers from Unicode CLDR JSON.

import argparse
import json
import os
import re
import sys

CLDR = None
OUT = None


def load(*parts):
    with open(os.path.join(CLDR, *parts), encoding="utf-8") as f:
        return json.load(f)


def c_string(s):
    return c_bytes(s.encode("utf-8"))


class Pool:
    def __init__(self, name):
        self.name = name
        self.data = bytearray()
        self.index = {}

    def add(self, s):
        if s in self.index:
            return self.index[s]
        b = s.encode("utf-8")
        pos = self.data.find(b + b"\0")
        if pos < 0:
            pos = len(self.data)
            self.data += b + b"\0"
        self.index[s] = pos
        return pos

    def emit(self, w):
        w.append("static const char %s[%d] =" % (self.name, len(self.data) + 1))
        chunk = bytearray()
        for b in self.data:
            chunk.append(b)
            if b == 0 and len(chunk) > 60:
                w.append("    " + c_bytes(chunk))
                chunk = bytearray()
        if chunk:
            w.append("    " + c_bytes(chunk))
        w[-1] += ";"


def c_bytes(data):
    out = []
    for ch in data:
        if ch == 0x22 or ch == 0x5C:
            out.append("\\" + chr(ch))
        elif 0x20 <= ch < 0x7F and ch != 0x3F:
            out.append(chr(ch))
        else:
            out.append("\\%03o" % ch)
    return '"' + "".join(out) + '"'


def emit_u16_table(w, ctype, name, rows, per_line=8):
    w.append("static const %s %s[%d] = {" % (ctype, name, len(rows)))
    for i in range(0, len(rows), per_line):
        w.append("    " + " ".join(r + "," for r in rows[i:i + per_line]))
    w.append("};")


def is_lang(s):
    return re.fullmatch(r"[a-z]{2,3}|[a-z]{5,8}", s) is not None


def is_script(s):
    return re.fullmatch(r"[A-Za-z]{4}", s) is not None


def is_region(s):
    return re.fullmatch(r"[A-Za-z]{2}|[0-9]{3}", s) is not None


def is_variant(s):
    return re.fullmatch(r"[a-z0-9]{5,8}|[0-9][a-z0-9]{3}", s.lower()) is not None


def parse_langid(tag):
    parts = tag.replace("_", "-").split("-")
    if not parts or not is_lang(parts[0].lower()):
        if parts and is_script(parts[0]):
            parts = ["und"] + parts
        else:
            return None
    lang = parts[0].lower()
    i = 1
    script = region = ""
    if i < len(parts) and is_script(parts[i]):
        script = parts[i].title()
        i += 1
    if i < len(parts) and is_region(parts[i]):
        region = parts[i].upper()
        i += 1
    variants = []
    while i < len(parts) and is_variant(parts[i]):
        variants.append(parts[i].lower())
        i += 1
    if i != len(parts):
        return None
    if len(set(variants)) != len(variants):
        return None
    if lang == "und":
        lang = ""
    return (lang, script, region, sorted(variants))


def rule_sort_key(rule):
    l, s, r, v = rule[0]
    count = (1 if l else 0) + (1 if s else 0) + (1 if r else 0) + len(v)
    presence = tuple(0 if x else 1 for x in (l, s, r, v))
    return (-count, presence, l, s, r, v)


def gen_locale(w):
    alias = load("cldr-core", "supplemental", "aliases.json")["supplemental"]["metadata"]["alias"]
    rules = []
    for typ, val in alias["languageAlias"].items():
        src = parse_langid(typ)
        if src is None:
            continue
        repl = parse_langid(val["_replacement"])
        if repl is None:
            continue
        rules.append((src, [repl]))
    for typ, val in alias["scriptAlias"].items():
        src = parse_langid("und-" + typ)
        repl = parse_langid("und-" + val["_replacement"])
        if src and repl:
            rules.append((src, [repl]))
    for typ, val in alias["territoryAlias"].items():
        src = parse_langid("und-" + typ)
        if src is None:
            continue
        repls = [parse_langid("und-" + r) for r in val["_replacement"].split()]
        if any(r is None for r in repls):
            continue
        rules.append((src, repls))
    for typ, val in alias["variantAlias"].items():
        src = parse_langid("und-" + typ)
        repl = parse_langid("und-" + val["_replacement"])
        if src and repl:
            rules.append((src, [repl]))
    rules.sort(key=rule_sort_key)

    pool = Pool("intl_alias_pool")
    pool.add("")
    rows = []
    for (l, s, r, v), repls in rules:
        rl, rs, rr, rv = repls[0]
        if len(repls) > 1:
            rr = " ".join(x[2] for x in repls)
        offs = [pool.add(l), pool.add(s), pool.add(r), pool.add("-".join(v)),
                pool.add(rl), pool.add(rs), pool.add(rr), pool.add("-".join(rv))]
        rows.append("{ " + ", ".join(str(o) for o in offs) + " }")
    pool.emit(w)
    w.append("typedef struct { uint16_t l, s, r, v, rl, rs, rr, rv; } IntlAliasRule;")
    w.append("static const IntlAliasRule intl_alias_rules[%d] = {" % len(rows))
    for row in rows:
        w.append("    " + row + ",")
    w.append("};")
    assert len(pool.data) < 65536

    subdiv = []
    for typ, val in alias["subdivisionAlias"].items():
        subdiv.append((typ, val["_replacement"].split()[0]))
    subdiv.sort()
    spool = Pool("intl_subdivision_pool")
    w2 = []
    for a, b in subdiv:
        w2.append("{ %d, %d }" % (spool.add(a), spool.add(b)))
    spool.emit(w)
    w.append("static const uint16_t intl_subdivision_alias[%d][2] = {" % len(w2))
    for row in w2:
        w.append("    " + row + ",")
    w.append("};")

    gen_ext_aliases(w)
    gen_likely(w)


def is_uvalue(s):
    return re.fullmatch(r"[a-z0-9]{3,8}(-[a-z0-9]{3,8})*", s) is not None


def gen_ext_aliases(w):
    entries = []
    bcp = os.path.join(CLDR, "cldr-bcp47", "bcp47")
    for fn in sorted(os.listdir(bcp)):
        data = load("cldr-bcp47", "bcp47", fn)["keyword"]
        for ext, keys in data.items():
            for key, types in keys.items():
                for name, info in types.items():
                    if name.startswith("_") or not isinstance(info, dict):
                        continue
                    if not is_uvalue(name):
                        continue
                    if info.get("_preferred"):
                        pref = info["_preferred"]
                        if is_uvalue(pref) and pref != name:
                            entries.append((ext, key, name, pref))
                    for a in info.get("_alias", "").split():
                        a = a.lower()
                        if is_uvalue(a) and a != (info.get("_preferred") or name):
                            target = info.get("_preferred") or name
                            entries.append((ext, key, a, target))
    seen = {}
    for ext, key, a, b in entries:
        seen.setdefault((ext, key, a), b)
    rows = sorted(seen.items())
    pool = Pool("intl_ext_alias_pool")
    out = []
    for (ext, key, a), b in rows:
        out.append("{ '%s', \"%s\", %d, %d }" % (ext, key, pool.add(a), pool.add(b)))
    pool.emit(w)
    w.append("typedef struct { char ext; char key[3]; uint16_t from, to; } IntlExtAlias;")
    w.append("static const IntlExtAlias intl_ext_aliases[%d] = {" % len(out))
    for row in out:
        w.append("    " + row + ",")
    w.append("};")


def gen_likely(w):
    likely = load("cldr-core", "supplemental", "likelySubtags.json")["supplemental"]["likelySubtags"]
    scripts = set()
    regions = set()
    pairs = []
    for k, v in likely.items():
        a = parse_langid(k)
        b = parse_langid(v)
        assert a is not None and b is not None, (k, v)
        assert not a[3] and not b[3]
        scripts.update(x for x in (a[1], b[1]) if x)
        regions.update(x for x in (a[2], b[2]) if x)
        pairs.append((a, b))
    scripts = sorted(scripts)
    regions = sorted(regions)
    assert len(scripts) < 256 and len(regions) < 512
    sidx = {s: i + 1 for i, s in enumerate(scripts)}
    ridx = {r: i + 1 for i, r in enumerate(regions)}

    def lcode(l):
        if not l:
            l = "und"
        assert len(l) <= 3
        v = 0
        for i in range(3):
            v = v * 27 + (ord(l[i]) - 96 if i < len(l) else 0)
        return v

    def pack(t):
        return (lcode(t[0]) << 17) | (sidx.get(t[1], 0) << 9) | ridx.get(t[2], 0)

    table = sorted((pack(a), pack(b)) for a, b in pairs)
    w.append("static const char intl_likely_scripts[%d][4] = {" % len(scripts))
    for i in range(0, len(scripts), 12):
        w.append("    " + " ".join("{'%s','%s','%s','%s'}," % tuple(s) for s in scripts[i:i + 12]))
    w.append("};")
    w.append("static const char intl_likely_regions[%d][4] = {" % len(regions))
    for i in range(0, len(regions), 12):
        w.append("    " + " ".join(c_string(r) + "," for r in regions[i:i + 12]))
    w.append("};")
    w.append("static const uint32_t intl_likely_table[%d][2] = {" % len(table))
    for i in range(0, len(table), 4):
        w.append("    " + " ".join("{ 0x%08x, 0x%08x }," % p for p in table[i:i + 4]))
    w.append("};")


def gen_regions(w):
    supp = os.path.join("cldr-core", "supplemental")
    time_data = load(supp, "timeData.json")["supplemental"]["timeData"]
    week = load(supp, "weekData.json")["supplemental"]["weekData"]
    cal_pref = load(supp, "calendarPreferenceData.json")["supplemental"]["calendarPreferenceData"]
    days = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"]
    hc_map = {"H": "h23", "h": "h12", "K": "h11", "k": "h24", "hb": "h12", "hB": "h12",
              "Hb": "h23", "HB": "h23"}
    hc_bits = {"h11": 0, "h12": 1, "h23": 2, "h24": 3}

    def hour_cycles(td):
        if not td:
            return 0, 0
        order = [hc_map[td["_preferred"]]]
        for a in td["_allowed"].split():
            h = hc_map[a]
            if h not in order:
                order.append(h)
        hc_list = 0
        for i, h in enumerate(order[:4]):
            hc_list |= hc_bits[h] << (2 * i)
        return len(order[:4]), hc_list

    regions = set()
    regions.update(k for k in time_data if "_" not in k)
    regions.update(week["firstDay"].keys())
    regions.update(week["weekendStart"].keys())
    regions.update(cal_pref.keys())
    regions.update(week["minDays"].keys())
    regions = sorted(r for r in regions if "-" not in r and "_" not in r)
    cal_names = ["gregory", "buddhist", "chinese", "coptic", "dangi", "ethioaa", "ethiopic",
                 "hebrew", "indian", "islamic", "islamic-civil", "islamic-rgsa", "islamic-tbla",
                 "islamic-umalqura", "iso8601", "japanese", "persian", "roc"]
    cal_alias = {"gregorian": "gregory", "ethiopic-amete-alem": "ethioaa"}
    w.append("static const char *const intl_region_calendar_names[%d] = {" % len(cal_names))
    w.append("    " + " ".join(c_string(c) + "," for c in cal_names))
    w.append("};")
    w.append("typedef struct { char region[4]; uint8_t first_day, weekend_start, weekend_end, min_days;"
             " uint8_t hc_count, hc_list; uint8_t ncal; uint8_t cals[6]; } IntlRegionInfo;")
    w.append("static const IntlRegionInfo intl_region_infos[%d] = {" % len(regions))
    for r in regions:
        fd = days.index(week["firstDay"][r]) + 1 if r in week["firstDay"] else 0
        ws = days.index(week["weekendStart"][r]) + 1 if r in week["weekendStart"] else 0
        we = days.index(week["weekendEnd"][r]) + 1 if r in week["weekendEnd"] else 0
        md = int(week["minDays"][r]) if r in week["minDays"] else 0
        hc_count, hc_list = hour_cycles(time_data.get(r))
        cals = []
        for c in cal_pref.get(r, []):
            c = cal_alias.get(c, c)
            if c in cal_names and cal_names.index(c) not in cals:
                cals.append(cal_names.index(c))
        cals = cals[:6]
        w.append("    { %s, %d, %d, %d, %d, %d, %d, %d, { %s } }," % (
            c_string(r), fd, ws, we, md, hc_count, hc_list, len(cals),
            ", ".join(str(c) for c in cals) or "0"))
    w.append("};")
    loc_keys = sorted(k for k in time_data if "-" in k or "_" in k)
    w.append("typedef struct { char locale[12]; uint8_t hc_count, hc_list; } IntlLocaleHourCycles;")
    w.append("static const IntlLocaleHourCycles intl_locale_hour_cycles[%d] = {" % len(loc_keys))
    for k in loc_keys:
        n, lst = hour_cycles(time_data[k])
        w.append("    { %s, %d, %d }," % (c_string(k.replace("_", "-")), n, lst))
    w.append("};")


def gen_timezones(w):
    tz = load("cldr-bcp47", "bcp47", "timezone.json")["keyword"]["u"]["tz"]
    ids = {}
    for code, info in tz.items():
        if code.startswith("_") or not isinstance(info, dict):
            continue
        names = info.get("_alias", "").split()
        if not names:
            continue
        primary = info.get("_iana") or names[0]
        if code in ("utc", "gmt"):
            primary = "UTC"
        region = ""
        if re.fullmatch(r"[a-z]{2}[a-z0-9]{3,6}", code) and code not in ("utc", "gmt") \
                and not code.startswith("utc"):
            region = code[:2].upper()
        for n in names:
            ids[n] = (primary, region, code, names[0])
        ids.setdefault(primary, (primary, region, code, names[0]))
    ids["UTC"] = ("UTC", "", "utc", "Etc/UTC")
    names = sorted(ids)
    pool = Pool("intl_tz_pool")
    rows = []
    for n in names:
        primary, region, code, cldr = ids[n]
        rows.append("{ %d, %d, %d, %s }" % (pool.add(n), pool.add(primary), pool.add(cldr), c_string(region)))
    pool.emit(w)
    w.append("typedef struct { uint16_t name, primary, cldr; char region[3]; } IntlTimeZoneId;")
    w.append("static const IntlTimeZoneId intl_time_zones[%d] = {" % len(rows))
    for i in range(0, len(rows), 4):
        w.append("    " + " ".join(r + "," for r in rows[i:i + 4]))
    w.append("};")
    assert len(pool.data) < 65536


def gen_numbering(w):
    ns = load("cldr-core", "supplemental", "numberingSystems.json")["supplemental"]["numberingSystems"]
    rows = []
    for name, info in sorted(ns.items()):
        if info.get("_type") == "numeric":
            digits = info["_digits"]
            assert len(digits) == 10
            rows.append((name, ord(digits[0]), all(ord(digits[i]) == ord(digits[0]) + i for i in range(10)), digits))
    w.append("typedef struct { const char *name; const char *digits; } IntlNumberingSystem;")
    w.append("static const IntlNumberingSystem intl_numbering_systems[%d] = {" % len(rows))
    for name, first, contiguous, digits in rows:
        w.append("    { %s, %s }," % (c_string(name), c_string(digits)))
    w.append("};")
    algorithmic = sorted(n for n, i in ns.items() if i.get("_type") == "algorithmic")
    w.append("static const char *const intl_algorithmic_numbering_systems[%d] = {" % len(algorithmic))
    for i in range(0, len(algorithmic), 8):
        w.append("    " + " ".join(c_string(a) + "," for a in algorithmic[i:i + 8]))
    w.append("};")


def gen_currencies(w):
    cur = load("cldr-bcp47", "bcp47", "currency.json")["keyword"]["u"]["cu"]
    data = load("cldr-core", "supplemental", "currencyData.json")["supplemental"]["currencyData"]
    frac = data["fractions"]
    codes = sorted(c.upper() for c in cur if not c.startswith("_") and re.fullmatch(r"[a-z]{3}", c))
    w.append("typedef struct { char code[4]; uint8_t digits; } IntlCurrency;")
    w.append("static const IntlCurrency intl_currencies[%d] = {" % len(codes))
    line = []
    for c in codes:
        d = int(frac.get(c, frac["DEFAULT"])["_digits"])
        line.append("{ %s, %d }," % (c_string(c), d))
        if len(line) == 6:
            w.append("    " + " ".join(line))
            line = []
    if line:
        w.append("    " + " ".join(line))
    w.append("};")


def gen_scripts(w):
    sm = load("cldr-core", "scriptMetadata.json")["scriptMetadata"]
    rtl = {k for k, v in sm.items() if v.get("rtl") == "YES"}
    ltr = {k for k, v in sm.items() if v.get("rtl") == "NO"}
    if "Arab" in rtl:
        rtl.add("Aran")
    if "Syrc" in rtl:
        rtl.update(["Syre", "Syrj", "Syrn"])
    for variant, base in (("Latf", "Latn"), ("Latg", "Latn"), ("Cyrs", "Cyrl"), ("Hanb", "Hani"),
                          ("Jpan", "Hani"), ("Kore", "Hang"), ("Hans", "Hani"), ("Hant", "Hani")):
        if base in ltr:
            ltr.add(variant)
    rows = sorted([(s, "rtl") for s in rtl] + [(s, "ltr") for s in ltr])
    w.append("static const char intl_script_dirs[%d][6] = {" % len(rows))
    for i in range(0, len(rows), 10):
        w.append("    " + " ".join(c_string(s + ("R" if d == "rtl" else "L")) + "," for s, d in rows[i:i + 10]))
    w.append("};")


def available_locales():
    full = load("cldr-core", "availableLocales.json")["availableLocales"]["full"]
    coverage = load("cldr-core", "coverageLevels.json")["coverageLevels"]
    default_content = load("cldr-core", "defaultContent.json")["defaultContent"]
    langs = {k.split("-")[0] for k, v in coverage.items() if v == "modern"}
    sel = {x for x in full + default_content if x.split("-")[0] in langs}
    sel.discard("root")
    sel = {x for x in sel if not x.startswith("zh-Latn")}
    extra = set()
    for x in sel:
        p = x.split("-")
        if len(p) == 3 and len(p[1]) == 4 and is_region(p[2]):
            extra.add(p[0] + "-" + p[2])
    sel |= extra
    if "no" in sel:
        sel |= {"nb", "nb-NO", "nb-SJ"}
    return sorted(sel)


def gen_available(w):
    locs = available_locales()
    pool = Pool("intl_available_pool")
    offs = [pool.add(x) for x in locs]
    pool.emit(w)
    emit_u16_table(w, "uint16_t", "intl_available_locales", [str(o) for o in offs], 12)
    assert len(pool.data) < 65536


FULL_LANGS = set("""en de fr es it pt nl sv da no nb fi pl cs ru uk tr ar he hi ja ko zh th vi
    id el hu ro fa""".split())

COUNTS = ["zero", "one", "two", "few", "many", "other"]


class DataStore:
    def __init__(self):
        self.nodes = {}
        self.parents = {}

    def node(self, name):
        return self.nodes.setdefault(name, {})

    def put(self, locale, key, value):
        if isinstance(value, dict):
            value = value.get("_value")
        if value is None:
            return
        self.node(locale)[key] = value


def cldr_folder_exists(pkg, loc):
    return os.path.isdir(os.path.join(CLDR, pkg, "main", loc))


def cldr_main(pkg, loc, fn):
    path = os.path.join(CLDR, pkg, "main", loc, fn)
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        data = json.load(f)["main"]
    return next(iter(data.values()))


def data_locales():
    avail = available_locales()
    parents = load("cldr-core", "supplemental", "parentLocales.json")["supplemental"]["parentLocales"]["parentLocale"]
    folders = set(os.listdir(os.path.join(CLDR, "cldr-numbers-full", "main")))
    result = set()
    likely = load("cldr-core", "supplemental", "likelySubtags.json")["supplemental"]["likelySubtags"]

    def parent_of(loc):
        if loc in parents:
            return parents[loc]
        if "-" in loc:
            return loc.rsplit("-", 1)[0]
        return "und"

    def add(loc):
        while loc and loc != "und" and loc not in result:
            if loc in folders:
                result.add(loc)
            loc = parent_of(loc)

    for loc in avail:
        add(loc)
        langid = parse_langid(loc)
        if langid:
            l, s, r, v = langid
            m = likely.get(l + ("-" + r if r else "")) or likely.get(l)
            if m:
                mp = parse_langid(m)
                for cand in (l + "-" + (s or mp[1]) + "-" + (r or mp[2]), l + "-" + (s or mp[1])):
                    if cand in folders:
                        add(cand)
    result.add("und")
    pm = {}
    for loc in result:
        if loc == "und":
            continue
        p = parent_of(loc)
        while p not in result and p != "und":
            p = parent_of(p)
        pm[loc] = p
    return sorted(result), pm


def number_systems_of(nums):
    systems = ["latn"]
    d = nums.get("defaultNumberingSystem")
    if d and d not in systems:
        systems.append(d)
    for v in nums.get("otherNumberingSystems", {}).values():
        if v not in systems and ("symbols-numberSystem-" + v) in nums:
            systems.append(v)
    return systems


def put_numbers(store, loc, nums, full):
    store.put(loc, "nu", nums.get("defaultNumberingSystem"))
    native = nums.get("otherNumberingSystems", {}).get("native")
    if native:
        store.put(loc, "nu.native", native)
    store.put(loc, "n.mingroup", nums.get("minimumGroupingDigits"))
    systems = number_systems_of(nums) if loc != "und" else sorted(
        k[len("symbols-numberSystem-"):] for k in nums if k.startswith("symbols-numberSystem-"))
    for ns in systems:
        sym = nums.get("symbols-numberSystem-" + ns)
        if not sym:
            continue
        p = "n." + ns + "."
        for k in ("decimal", "group", "percentSign", "plusSign", "minusSign", "approximatelySign",
                  "exponential", "superscriptingExponent", "perMille", "infinity", "nan", "timeSeparator"):
            store.put(loc, p + k, sym.get(k))
        dec = nums.get("decimalFormats-numberSystem-" + ns, {})
        store.put(loc, p + "decimal.pat", dec.get("standard"))
        for width in ("short", "long"):
            for k, v in dec.get(width, {}).get("decimalFormat", {}).items():
                mag, count = k.split("-count-")
                store.put(loc, p + ("cs." if width == "short" else "cl.") + str(len(mag) - 1) + "." + count, v)
        store.put(loc, p + "percent.pat", nums.get("percentFormats-numberSystem-" + ns, {}).get("standard"))
        store.put(loc, p + "scientific.pat", nums.get("scientificFormats-numberSystem-" + ns, {}).get("standard"))
        cur = nums.get("currencyFormats-numberSystem-" + ns, {})
        for k in ("standard", "standard-alphaNextToNumber", "accounting", "accounting-alphaNextToNumber"):
            store.put(loc, p + "cur." + k, cur.get(k))
        for k, v in cur.items():
            if k.startswith("unitPattern-count-"):
                store.put(loc, p + "cur.unit." + k[len("unitPattern-count-"):], v)
        for k, v in cur.get("short", {}).get("standard", {}).items():
            mag, count = k.split("-count-")
            store.put(loc, p + "cc." + str(len(mag) - 1) + "." + count.replace("-alt-alphaNextToNumber", ".alpha"), v)
        misc = nums.get("miscPatterns-numberSystem-" + ns, {})
        store.put(loc, p + "approx", misc.get("approximately"))
        store.put(loc, p + "range", misc.get("range"))


def put_currencies(store, loc, curs, full):
    major = set("""USD EUR JPY GBP CNY INR CHF CAD AUD KRW BRL RUB MXN SEK NOK DKK PLN CZK HUF TRY
        ILS SAR AED EGP ZAR NZD SGD HKD TWD THB IDR MYR PHP VND PKR BDT NGN KES UAH RON BGN ISK""".split())
    for code, c in curs.items():
        if not re.fullmatch(r"[A-Z]{3}", code):
            continue
        sym = c.get("symbol")
        if sym is not None and sym != code:
            store.put(loc, "c." + code + ".s", sym)
        nar = c.get("symbol-alt-narrow")
        if nar is not None and nar != (sym if sym is not None else code):
            store.put(loc, "c." + code + ".n", nar)
        if full and code in major:
            store.put(loc, "c." + code + ".d", c.get("displayName"))
            for k, v in c.items():
                if k.startswith("displayName-count-"):
                    store.put(loc, "c." + code + "." + k[len("displayName-count-"):], v)


UNIT_IDS = None


def put_units(store, loc, units):
    global UNIT_IDS
    sanctioned = ["acre", "bit", "byte", "celsius", "centimeter", "day", "degree", "fahrenheit",
                  "fluid-ounce", "foot", "gallon", "gigabit", "gigabyte", "gram", "hectare", "hour",
                  "inch", "kilobit", "kilobyte", "kilogram", "kilometer", "liter", "megabit", "megabyte",
                  "meter", "microsecond", "mile", "mile-scandinavian", "milliliter", "millimeter",
                  "millisecond", "minute", "month", "nanosecond", "ounce", "percent", "petabyte", "pound",
                  "second", "stone", "terabit", "terabyte", "week", "yard", "year"]
    for width in ("long", "short", "narrow"):
        w = units.get(width, {})
        store.put(loc, "u.per." + width, w.get("per", {}).get("compoundUnitPattern"))
        for full_id, data in w.items():
            if not isinstance(data, dict) or "-" not in full_id:
                continue
            uid = full_id.split("-", 1)[1]
            parts = uid.split("-per-")
            if uid not in sanctioned and not (len(parts) == 2 and all(x in sanctioned for x in parts)):
                continue
            for k, v in data.items():
                if k.startswith("unitPattern-count-"):
                    store.put(loc, "u." + uid + "." + width + "." + k[len("unitPattern-count-"):], v)
                elif k == "perUnitPattern":
                    store.put(loc, "u." + uid + "." + width + ".per", v)
                elif k == "displayName":
                    store.put(loc, "u." + uid + "." + width + ".name", v)


def put_plurals(store, loc, lang_key, plurals, ordinals, ranges):
    def strip(rule):
        return rule.split("@")[0].strip()
    for kind, src in (("c", plurals), ("o", ordinals)):
        rules = src.get(lang_key)
        if not rules:
            continue
        for k, v in rules.items():
            cat = k[len("pluralRule-count-"):]
            store.put(loc, "p." + kind + "." + cat, strip(v) or "-")
    rr = ranges.get(lang_key)
    if rr:
        for k, v in rr.items():
            m = re.fullmatch(r"pluralRange-start-(\w+)-end-(\w+)", k)
            store.put(loc, "p.r." + m.group(1) + "." + m.group(2), v)


CALENDAR_CODES = [
    ("gregorian", "g", "cldr-dates-full"),
    ("generic", "x", "cldr-dates-full"),
    ("buddhist", "b", "cldr-cal-buddhist-full"),
    ("chinese", "c", "cldr-cal-chinese-full"),
    ("coptic", "k", "cldr-cal-coptic-full"),
    ("dangi", "d", "cldr-cal-dangi-full"),
    ("ethiopic", "e", "cldr-cal-ethiopic-full"),
    ("ethiopic-amete-alem", "a", "cldr-cal-ethiopic-full"),
    ("hebrew", "h", "cldr-cal-hebrew-full"),
    ("indian", "i", "cldr-cal-indian-full"),
    ("islamic", "l", "cldr-cal-islamic-full"),
    ("japanese", "j", "cldr-cal-japanese-full"),
    ("persian", "p", "cldr-cal-persian-full"),
    ("roc", "r", "cldr-cal-roc-full"),
]

PATTERN_CALENDARS = {"g", "x", "c", "d", "b", "p", "l", "j", "r", "h"}
TZ_LANGS = set("en de fr es it pt ja zh ko ru nl sv pl".split())

CTX = {"format": "f", "stand-alone": "s"}
WIDTH = {"abbreviated": "a", "wide": "w", "narrow": "n", "short": "s"}
DAYS = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"]


def ascii_spaces(v):
    if isinstance(v, str):
        return v.replace("\u202f", " ")
    return v


def put_calendar(store, loc, code, cal, full):
    p = code
    orig_put = store.put

    def put(l, k, v):
        if k.startswith(p + "af.") or k.startswith(p + "if") or k.startswith(p + "tf.") or \
                k.startswith(p + "df.") or k.startswith(p + "dt"):
            v = ascii_spaces(v)
        orig_put(l, k, v)
    store = type("S", (), {"put": staticmethod(put)})
    if code != "x":
        for ctx, widths in cal.get("months", {}).items():
            for w, months in widths.items():
                if w not in WIDTH:
                    continue
                for n, v in months.items():
                    n = n.replace("-yeartype-leap", "L")
                    store.put(loc, "%sM%s%s.%s" % (p, CTX[ctx], WIDTH[w], n), v)
        eras = cal.get("eras", {})
        for k, w in (("eraAbbr", "a"), ("eraNames", "w"), ("eraNarrow", "n")):
            for n, v in eras.get(k, {}).items():
                if "alt" in n:
                    continue
                if code == "j" and n.isdigit() and int(n) < 232:
                    continue
                store.put(loc, "%sE%s.%s" % (p, w, n), v)
    if code == "g":
        for ctx, widths in cal.get("days", {}).items():
            for w, days in widths.items():
                if w not in WIDTH:
                    continue
                for d, v in days.items():
                    store.put(loc, "%sD%s%s.%d" % (p, CTX[ctx], WIDTH[w], DAYS.index(d)), v)
        for ctx, widths in cal.get("dayPeriods", {}).items():
            for w, periods in widths.items():
                if w not in WIDTH:
                    continue
                for k, v in periods.items():
                    if "alt" in k:
                        continue
                    store.put(loc, "%sP%s%s.%s" % (p, CTX[ctx], WIDTH[w], k), v)
    if code in PATTERN_CALENDARS:
        for k in ("full", "long", "medium", "short"):
            store.put(loc, "%sdf.%s" % (p, k), cal.get("dateFormats", {}).get(k))
            store.put(loc, "%stf.%s" % (p, k), cal.get("timeFormats", {}).get(k))
            store.put(loc, "%sdt.%s" % (p, k), cal.get("dateTimeFormats", {}).get(k))
            at = cal.get("dateTimeFormats-atTime", {}).get("standard", {}).get(k)
            if at is not None:
                store.put(loc, "%sdta.%s" % (p, k), at)
        dtf = cal.get("dateTimeFormats", {})
        for k, v in dtf.get("availableFormats", {}).items():
            if "alt" in k or "count" in k or "W" in k or "w" in k or "Q" in k:
                continue
            store.put(loc, "%saf.%s" % (p, k), v)
        if full:
            for k, v in dtf.get("intervalFormats", {}).items():
                if k == "intervalFormatFallback":
                    store.put(loc, "%sif" % p, v)
                    continue
                if "alt" in k:
                    continue
                for f, pat in v.items():
                    if "alt" in f:
                        continue
                    store.put(loc, "%sif.%s.%s" % (p, k, f), pat)
        else:
            store.put(loc, "%sif" % p, dtf.get("intervalFormats", {}).get("intervalFormatFallback"))
        for k, v in dtf.get("appendItems", {}).items():
            store.put(loc, "%sai.%s" % (p, k), v)
    if "monthPatterns" in cal:
        for ctx, widths in cal["monthPatterns"].items():
            for w, pats in widths.items():
                c = "n" if ctx == "numeric" else CTX.get(ctx)
                ww = "a" if w == "all" else WIDTH.get(w)
                if c and ww and "leap" in pats:
                    store.put(loc, "%smp%s%s" % (p, c, ww), pats["leap"])
    cyc = cal.get("cyclicNameSets", {}).get("years", {}).get("format", {})
    for w, names in cyc.items():
        if w not in WIDTH:
            continue
        for n, v in names.items():
            store.put(loc, "%sY%s.%s" % (p, WIDTH[w], n), v)


def put_dates(store, loc, full):
    for name, code, pkg in CALENDAR_CODES:
        if code != "g" and not full:
            continue
        data = cldr_main(pkg, loc, "ca-%s.json" % name)
        if not data:
            continue
        put_calendar(store, loc, code, data["dates"]["calendars"][name], full)
    node = store.nodes.get(loc, {})
    for k in list(node):
        if k[0] in "bpljrh" and len(k) > 2 and (k[1:3] in ("af", "if", "tf", "df", "dt", "ai") or k[1:] == "if"):
            if node.get("x" + k[1:]) == node[k]:
                del node[k]


def put_time_zone_names(store, loc, names_too):
    data = cldr_main("cldr-dates-full", loc, "timeZoneNames.json")
    if not data:
        return
    tz = data["dates"]["timeZoneNames"]
    for k, key in (("hourFormat", "zh"), ("gmtFormat", "zg"), ("gmtZeroFormat", "zg0"),
                   ("regionFormat", "zr"), ("regionFormat-type-daylight", "zrd"),
                   ("regionFormat-type-standard", "zrs"), ("fallbackFormat", "zf")):
        store.put(loc, key, tz.get(k))
    if not names_too:
        return
    for mz, names in tz.get("metazone", {}).items():
        for length, kinds in names.items():
            for kind, v in kinds.items():
                store.put(loc, "zm.%s.%s%s" % (mz, length[0], kind[0]), v)

    def walk(prefix, node):
        for k, v in node.items():
            if not isinstance(v, dict):
                continue
            name = prefix + "/" + k if prefix else k
            if v.get("_type") == "zone":
                for length in ("long", "short"):
                    for kind, s in v.get(length, {}).items():
                        store.put(loc, "zz.%s.%s%s" % (name, length[0], kind[0]), s)
            else:
                walk(name, v)
    walk("", tz.get("zone", {}))


def put_day_period_rules(store, loc, lang_key, rules):
    r = rules.get(lang_key)
    if not r:
        return
    for k, v in r.items():
        if "_at" in v:
            store.put(loc, "dpr.%s" % k, "@" + v["_at"])
        else:
            store.put(loc, "dpr.%s" % k, v["_from"] + "-" + v["_before"])


def build_store():
    locs, parents = data_locales()
    store = DataStore()
    store.parents = parents
    supp = ("cldr-core", "supplemental")
    plurals = load(*supp, "plurals.json")["supplemental"]["plurals-type-cardinal"]
    ordinals = load(*supp, "ordinals.json")["supplemental"]["plurals-type-ordinal"]
    ranges = load(*supp, "pluralRanges.json")["supplemental"]["plurals"]
    day_periods = load(*supp, "dayPeriods.json")["supplemental"]["dayPeriodRuleSet"]
    root_systems = {}
    for rep in ["ar", "fa", "hi", "bn", "mr", "ne", "my", "th", "ps", "ur-IN", "as", "dz", "sat", "mni", "ks",
                "sd", "km", "lo", "ckb", "pa-Arab"] + list(locs):
        rnums = cldr_main("cldr-numbers-full", rep, "numbers.json")
        if not rnums:
            continue
        for k, v in rnums["numbers"].items():
            if k.startswith("symbols-numberSystem-") and k not in root_systems:
                root_systems[k] = v
    for loc in locs:
        lang = loc.split("-")[0]
        full = lang in FULL_LANGS or loc == "und"
        nums = cldr_main("cldr-numbers-full", loc, "numbers.json")
        if nums and loc == "und":
            for k, v in root_systems.items():
                nums["numbers"].setdefault(k, v)
        if nums:
            put_numbers(store, loc, nums["numbers"], full)
        curs = cldr_main("cldr-numbers-full", loc, "currencies.json")
        if curs:
            put_currencies(store, loc, curs["numbers"]["currencies"], full)
        if full:
            units = cldr_main("cldr-units-full", loc, "units.json")
            if units:
                put_units(store, loc, units["units"])
        put_dates(store, loc, full)
        put_time_zone_names(store, loc, lang in TZ_LANGS or loc == "und")
        put_day_period_rules(store, loc, "root" if loc == "und" else loc, day_periods)
        pk = "root" if loc == "und" else loc
        if pk in plurals or pk in ordinals or pk in ranges:
            put_plurals(store, loc, pk, plurals, ordinals, ranges)
        elif loc == "und":
            put_plurals(store, loc, "root", plurals, ordinals, ranges)
    for lang in sorted(set(plurals) | set(ordinals)):
        if lang == "root" or "-" in lang or "_" in lang or lang in store.nodes or lang in locs:
            continue
        put_plurals(store, lang, lang, plurals, ordinals, ranges)
        store.parents[lang] = "und"
    return store


def resolved(store, loc, cache):
    if loc in cache:
        return cache[loc]
    base = {} if loc == "und" else dict(resolved(store, store.parents[loc], cache))
    base.update(store.nodes.get(loc, {}))
    cache[loc] = base
    return base


def gen_data_store(w):
    store = build_store()
    locs = sorted(set(store.nodes) | set(store.parents) | {"und"})
    cache = {}
    diffs = {}
    for loc in locs:
        full = resolved(store, loc, cache)
        parent = {} if loc == "und" else resolved(store, store.parents[loc], cache)
        diffs[loc] = {k: v for k, v in full.items() if parent.get(k) != v}
        if loc != "und" and store.parents[loc] != "und":
            for k in parent:
                if k not in store.nodes.get(loc, {}) and (k.startswith("zm.") or k.startswith("zz.")):
                    diffs[loc][k] = "\x7f"
    keys = sorted({k for d in diffs.values() for k in d})
    kidx = {k: i for i, k in enumerate(keys)}
    kpool = Pool("intl_data_key_pool")
    koffs = [kpool.add(k) for k in keys]
    vpool = Pool("intl_data_pool")
    lidx = {l: i for i, l in enumerate(locs)}
    entries_k = []
    entries_v = []
    nodes = []
    for loc in locs:
        d = diffs[loc]
        start = len(entries_k)
        for k in sorted(d, key=lambda x: kidx[x]):
            entries_k.append(kidx[k])
            entries_v.append(vpool.add(d[k]))
        parent = -1 if loc == "und" else lidx[store.parents[loc]]
        nodes.append((loc, parent, start, len(entries_k) - start))
    kpool.emit(w)
    vpool.emit(w)
    w.append("static const uint32_t intl_data_keys[%d] = {" % len(keys))
    for i in range(0, len(koffs), 10):
        w.append("    " + " ".join("%d," % o for o in koffs[i:i + 10]))
    w.append("};")
    w.append("typedef struct { const char *name; int16_t parent; uint32_t start; uint16_t count; } IntlDataNode;")
    w.append("static const IntlDataNode intl_data_nodes[%d] = {" % len(nodes))
    for n in nodes:
        w.append("    { %s, %d, %d, %d }," % (c_string(n[0]), n[1], n[2], n[3]))
    w.append("};")
    w.append("static const uint16_t intl_data_entry_keys[%d] = {" % len(entries_k))
    for i in range(0, len(entries_k), 16):
        w.append("    " + " ".join("%d," % o for o in entries_k[i:i + 16]))
    w.append("};")
    assert len(vpool.data) < (1 << 24)
    w.append("static const uint8_t intl_data_entry_vals[%d][3] = {" % len(entries_v))
    for i in range(0, len(entries_v), 10):
        w.append("    " + " ".join("{%d,%d,%d}," % (o & 255, (o >> 8) & 255, o >> 16)
                                   for o in entries_v[i:i + 10]))
    w.append("};")
    print("data store: %d nodes, %d keys, %d entries, %d key bytes, %d value bytes"
          % (len(nodes), len(keys), len(entries_k), len(kpool.data), len(vpool.data)))


def parse_cldr_time(t):
    import datetime
    d = datetime.datetime.strptime(t, "%Y-%m-%d %H:%M")
    return int((d - datetime.datetime(1970, 1, 1)).total_seconds() // 60)


def gen_tz_meta(w):
    tz = load("cldr-bcp47", "bcp47", "timezone.json")["keyword"]["u"]["tz"]
    names = set()
    for code, info in tz.items():
        if code.startswith("_") or not isinstance(info, dict):
            continue
        for n in info.get("_alias", "").split():
            names.add(n)
    meta = load("cldr-core", "supplemental", "metaZones.json")["supplemental"]["metaZones"]["metazoneInfo"]["timezone"]

    def walk(prefix, node):
        for k, v in node.items():
            name = prefix + "/" + k if prefix else k
            if isinstance(v, list):
                yield name, v
            else:
                yield from walk(name, v)
    rows = []
    mzs = set()
    for zone, uses in walk("", meta):
        for u in uses:
            m = u["usesMetazone"]
            mzs.add(m["_mzone"])
            frm = parse_cldr_time(m["_from"]) if "_from" in m else -2147483647
            to = parse_cldr_time(m["_to"]) if "_to" in m else 2147483647
            rows.append((zone, m["_mzone"], frm, to))
    rows.sort()
    pool = Pool("intl_metazone_pool")
    out = []
    for zone, mz, frm, to in rows:
        out.append("{ %d, %d, %d, %d }" % (pool.add(zone), pool.add(mz), frm, to))
    pool.emit(w)
    w.append("typedef struct { uint16_t zone, metazone; int32_t from, to; } IntlZoneMetazone;")
    w.append("static const IntlZoneMetazone intl_zone_metazones[%d] = {" % len(out))
    for i in range(0, len(out), 3):
        w.append("    " + " ".join(r + "," for r in out[i:i + 3]))
    w.append("};")
    win = load("cldr-core", "supplemental", "windowsZones.json")["supplemental"]["windowsZones"]["mapTimezones"]
    wrows = sorted({(e["mapZone"]["_other"], e["mapZone"]["_type"].split()[0])
                    for e in win if e["mapZone"]["_territory"] == "001"})
    wpool = Pool("intl_windows_zone_pool")
    wout = ["{ %d, %d }" % (wpool.add(a), wpool.add(b)) for a, b in wrows]
    rmap = {}
    for e in win:
        for z in e["mapZone"]["_type"].split():
            rmap.setdefault(z, e["mapZone"]["_other"])
    rrows = sorted(rmap.items())
    rout = ["{ %d, %d }" % (wpool.add(a), wpool.add(b)) for a, b in rrows]
    wpool.emit(w)
    w.append("static const uint16_t intl_windows_zones[%d][2] = {" % len(wout))
    for i in range(0, len(wout), 6):
        w.append("    " + " ".join(r + "," for r in wout[i:i + 6]))
    w.append("};")
    w.append("static const uint16_t intl_iana_windows_zones[%d][2] = {" % len(rout))
    for i in range(0, len(rout), 6):
        w.append("    " + " ".join(r + "," for r in rout[i:i + 6]))
    w.append("};")


def write_header(name, desc, body):
    path = os.path.join(OUT, name)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* %s: %s (generated by scripts/gen-intl-data.py from CLDR %s; do not edit). */\n\n"
                % (name, desc, CLDR_VERSION))
        f.write("\n".join(body))
        f.write("\n")
    print("wrote %s (%d bytes)" % (path, os.path.getsize(path)))


def main():
    global CLDR, OUT, CLDR_VERSION
    ap = argparse.ArgumentParser()
    ap.add_argument("cldr_json", help="directory of the unpacked cldr-json full release")
    ap.add_argument("out_dir", help="src/quickjs directory")
    args = ap.parse_args()
    CLDR = args.cldr_json
    OUT = args.out_dir
    CLDR_VERSION = load("cldr-core", "package.json")["version"]

    w = []
    gen_locale(w)
    gen_regions(w)
    gen_timezones(w)
    gen_numbering(w)
    gen_currencies(w)
    gen_scripts(w)
    gen_available(w)
    gen_tz_meta(w)
    write_header("quickjs-intl-locale-data.h", "Unicode locale identifier data", w)
    w = []
    gen_data_store(w)
    write_header("quickjs-intl-cldr-data.h", "CLDR locale data for the Intl services", w)


if __name__ == "__main__":
    main()
