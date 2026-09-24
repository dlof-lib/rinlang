#!/usr/bin/env python3
"""فحص ثابت لـ firebase/database.rules.json (لا emulator هنا): JSON صالح، توازن الأقواس/الاقتباس في كل تعبير، وثوابت الأمان."""
import json, sys
d = json.load(open(__file__.rsplit('/tests/', 1)[0] + '/firebase/database.rules.json'))['rules']
bad = []
def walk(n, path):
    for k, v in n.items():
        if k.startswith('.'):
            if isinstance(v, str):
                if v.count('(') != v.count(')') or v.count("'") % 2: bad.append((path + '/' + k, 'unbalanced'))
            elif not isinstance(v, (bool, list)): bad.append((path + '/' + k, 'bad type'))
        elif isinstance(v, dict): walk(v, path + '/' + k)
        else: bad.append((path + '/' + k, 'non-object'))
walk(d, '')
assert not bad, bad
assert '.write' not in d and '.read' not in d, 'root must not be open'
assert d['packages']['.read'] is True and d['libraries']['.read'] is True and d['libraries']['.write'] is False
p = d['packages']['$packageId']
assert "newData.child('publisherUid').val() === auth.uid" in p['.write'], 'create must bind publisherUid to auth.uid'
for node in (p, d['libraries']['$libKey']):
    assert node['likes']['$uid']['.write'].endswith('auth.uid === $uid') and 'anonymous' in node['likes']['$uid']['.write']
    assert 'newData.parent().child(\'likes\')' in node['likeCount']['.validate'] and 'downloaders' in node['downloadCount']['.validate']
    assert '!data.exists()' in node['downloaders']['$uid']['.write']
print('rules OK: %d top-level nodes, counters bound to per-uid markers' % len(d))
