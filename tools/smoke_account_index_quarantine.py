"""Smoke test: the account index quarantine path.

Split from smoke_account_index.py because it needs a server BOOTED AGAINST A DOCTORED account tree
-- the operator (or the driver script) corrupts a record and reboots first, then runs this. It
asserts what should be true while exactly one record is unreadable:

  1. The server is running at all. Before this work, one unparseable account.json called exit(1)
     and the game did not boot.
  2. `account index` lists the record as QUARANTINED.

    ROTS_SMOKE_EMAIL=... ROTS_SMOKE_PASSWORD=... ROTS_SMOKE_CHARACTER=... \
        python3 tools/smoke_account_index_quarantine.py [port] [quarantined-email]

NEVER run this against live player data. Corrupt a copy.
"""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mudclient import Mud

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 1024
QUARANTINED = sys.argv[2] if len(sys.argv) > 2 else None
EMAIL = os.environ.get('ROTS_SMOKE_EMAIL')
PASSWORD = os.environ.get('ROTS_SMOKE_PASSWORD')
if not EMAIL or not PASSWORD:
    sys.exit('Set ROTS_SMOKE_EMAIL and ROTS_SMOKE_PASSWORD to an account fixture with at\n'
             'least one linked immortal character. They are deliberately not defaulted:\n'
             'this file is in the repo and the fixture password is not.')
CHARACTER = os.environ.get('ROTS_SMOKE_CHARACTER')
if not CHARACTER:
    sys.exit('Set ROTS_SMOKE_CHARACTER to a linked character at LEVEL_GRGOD or above.')

fails = []
def check(label, ok, detail=''):
    print(('  PASS  ' if ok else '  FAIL  ') + label + (('\n         ' + str(detail)) if detail and not ok else ''))
    if not ok:
        fails.append(label)

def run_cmd(m, command, pattern, timeout=6.0):
    m.clear(); m.send(command); m.wait_for(pattern, timeout=timeout)
    return m.tail()

m = Mud('127.0.0.1', PORT, ttype='Mudlet', accept_msdp=True)
try:
    reachable = m.wait_for(r'Account email:')
    check('the server booted and accepts connections with a quarantined record', reachable)
    if not reachable:
        raise SystemExit(1)

    m.send(EMAIL); m.wait_for(r'Account password:')
    m.clear(); m.send(PASSWORD); m.wait_for(r'Play a linked character|linked characters')
    m.clear(); m.send('2'); roster = m.wait_for(r'\d\) \[') and m.tail()
    row = next((r for r, _, n in re.findall(r'(\d+)\) \[\s*(\d+)\s+\S+\] (\S+)', roster)
                if n.lower() == CHARACTER.lower()), None)
    check('found %s on the roster' % CHARACTER, row is not None, roster[-400:])
    m.send(row); m.wait_for(r'Enter the game|1\)')
    m.clear(); m.send('1'); m.pump(3.0)
    check('the immortal fixture can still enter the game',
          (m.msdp_get('CHARACTER_NAME') or '').lower() == CHARACTER.lower(),
          m.msdp_get('CHARACTER_NAME'))

    out = run_cmd(m, 'account index', r'Account index:')
    counts = re.search(r'Account index: (\d+) record\(s\) indexed, (\d+) quarantined, '
                       r'(\d+) unreadable since boot\.', out)
    check('`account index` reports a non-zero quarantined count',
          counts is not None and int(counts.group(2)) > 0, out[-400:])
    check('the quarantined record is listed with its path and reason',
          'QUARANTINED' in out and (QUARANTINED is None or QUARANTINED in out), out[-500:])

    m.clear(); m.send('quit'); m.pump(2.0)
finally:
    m.close()

print()
print('ALL PASS' if not fails else '%d FAILED: %s' % (len(fails), ', '.join(fails)))
sys.exit(1 if fails else 0)
