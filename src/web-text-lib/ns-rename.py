#!/usr/bin/env python3
"""Rename every public symbol so the fork can be loaded alongside GTK's pango.

GObject registers type names in a process-global registry, so a second copy of
Pango sharing the type names it already registered aborts at registration. Run
this after merging upstream; it is idempotent.
"""
import io
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
EXT = ('.c', '.h', '.cpp', '.hpp', '.build', '.in', '.template', '.meson',
       '.options', '.py')
SKIP_DIRS = {'.git', 'build', 'subprojects', '.tmp'}
SKIP_FILES = {os.path.basename(__file__)}

P = 'p' + 'ango'
RULES = (
    # Include guards, and the __PANGO_H_INSIDE__ single-include sentinel. These
    # have to be renamed for the same reason the symbols do, only the failure is
    # at compile time rather than at type registration: a translation unit that
    # includes both this fork and the system Pango -- which is any file in a GTK
    # application that also talks to this fork -- gets whichever header comes
    # first, and the second one silently expands to nothing because its guard is
    # already defined. The rule below has to come before the bare PANGO_ rule,
    # whose negative lookbehind deliberately skips anything preceded by an
    # underscore.
    (re.compile(r'(?<![A-Za-z0-9_])__%s([A-Z0-9_]*__)' % P.upper()),
     '__NS_%s\\1' % P.upper()),
    (re.compile(r'#include\s*<%s/' % P), '#include <ns-pango/'),
    (re.compile(r'#include\s*"%s/' % P), '#include "ns-pango/'),
    (re.compile(r'(?<![A-Za-z0-9_])%sCAIRO_BACKEND' % P.upper()),
     'NS_PANGOCAIRO_BACKEND'),
    (re.compile(r'(?<![A-Za-z0-9_])%s_TYPE_' % P.upper()), 'NS_TYPE_PANGO_'),
    (re.compile(r'(?<![A-Za-z0-9_])%s_' % P), 'ns_pango_'),
    (re.compile(r'(?<![A-Za-z0-9_])_%s_' % P), '_ns_pango_'),
    (re.compile(r'(?<![A-Za-z0-9_])%s(?=[A-Z])' % P.capitalize()), 'NsPango'),
    (re.compile(r'(?<![A-Za-z0-9_])%s_' % P.upper()), 'NS_PANGO_'),
)

MESON_RULES = (
    (re.compile(r"^project\('%s'" % P), "project('ns-pango'"),
    (re.compile(r"ns_pango_api_name = '%s-" % P), "ns_pango_api_name = 'ns-pango-"),
    (re.compile(r"join_paths\(ns_pango_api_name, '%s'\)" % P),
     "join_paths(ns_pango_api_name, 'ns-pango')"),
    (re.compile(r"subdir\('%s'\)" % P), "subdir('ns-pango')"),
    (re.compile(r"include_directories\('%s'\)" % P),
     "include_directories('ns-pango')"),
    (re.compile(r"override_dependency\('%s" % P), "override_dependency('ns-pango"),
    (re.compile(r"'%scairo-@0@'" % P), "'ns-pangocairo-@0@'"),
    (re.compile(r"'%sft2-@0@'" % P), "'ns-pangoft2-@0@'"),
    (re.compile(r'G_LOG_DOMAIN=\\?"%s\\?"' % P.capitalize()),
     'G_LOG_DOMAIN="NsPango"'),
    (re.compile(r"filebase: '%s" % P), "filebase: 'ns-pango"),
    (re.compile(r"header: '%s/" % P), "header: 'ns-pango/"),
    (re.compile(r"-D%s_" % P.upper()), "-DNS_PANGO_"),
)


def rewrite(path):
    with io.open(path, encoding='utf-8', errors='surrogateescape') as f:
        src = f.read()
    out = src
    for rx, rep in RULES:
        out = rx.sub(rep, out)
    if os.path.basename(path) == 'meson.build' or path.endswith('.options'):
        for rx, rep in MESON_RULES:
            out = rx.sub(rep, out)
    if out != src:
        with io.open(path, 'w', encoding='utf-8', errors='surrogateescape',
                     newline='') as f:
            f.write(out)
        return True
    return False


def main():
    if os.path.isdir(os.path.join(ROOT, P)):
        subprocess.check_call(['git', 'mv', P, 'ns-pango'], cwd=ROOT)
    changed = 0
    for base, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if name in SKIP_FILES:
                continue
            if name.endswith(EXT) or name.endswith('.rc.in'):
                if rewrite(os.path.join(base, name)):
                    changed += 1
    print('rewrote %d files' % changed)


if __name__ == '__main__':
    sys.exit(main())
