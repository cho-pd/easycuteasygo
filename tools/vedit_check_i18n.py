"""VEDIT: after a build, check the Korean catalogue.

  * every message in i18n/drift_ko.ts has a translation (lupdate adds new strings as unfinished);
  * every run-time name in tools/vedit_runtime_strings.json made it into build/drift_ko.qm
    (lupdate drops strings it cannot see in source — the bug this guards against).

Prints "I18N OK" or the problems; exit status 1 on problems.

    python tools/vedit_check_i18n.py
"""
import json
import os
import re
import sys

APP = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def main():
    ts = open(os.path.join(APP, 'i18n', 'drift_ko.ts'), encoding='utf-8').read()
    problems = []
    for m in re.finditer(r'<message( numerus="yes")?>((?:(?!</message>).)*?)</message>', ts, re.S):
        body = m.group(2)
        if 'type="unfinished"' in body or re.search(r'<translation>\s*</translation>', body):
            src = re.search(r'<source>(.*?)</source>', body, re.S).group(1)
            problems.append('untranslated: ' + src[:80])
    qm_path = os.path.join(APP, 'build', 'drift_ko.qm')
    if os.path.exists(qm_path):
        qm = open(qm_path, 'rb').read()
        table = json.load(open(os.path.join(APP, 'tools', 'vedit_runtime_strings.json'), encoding='utf-8'))
        for ctx, entries in table.items():
            for src, tr in entries.items():
                if tr.encode('utf-16-be') not in qm:
                    problems.append(f'missing from .qm: {ctx} / {src} → {tr}')
    else:
        problems.append('build/drift_ko.qm not found')
    if problems:
        print('I18N PROBLEMS', len(problems))
        for p in problems[:40]:
            print('  ' + p)
        return 1
    print('I18N OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
