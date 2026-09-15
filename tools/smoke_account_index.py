"""Smoke test: the account store index (feat/account-store, merged as 03f84a8).

Covers the client-observable half of
docs/superpowers/plans/2026-09-05-account-store-index-manual-test-checklist.md -- the parts a
scripted telnet client can actually prove. Sections 1 (boot) and the section 5 quarantine drill
are NOT here: both need a server restart against a doctored account tree, so they stay manual.

Requires a booted server (scripts/rots-docker.sh boot) and an account fixture with at least one
linked character at LEVEL_GRGOD or above -- the `account` command is immortal-only.

    ROTS_SMOKE_EMAIL=... ROTS_SMOKE_PASSWORD=... ROTS_SMOKE_CHARACTER=... \
        python3 tools/smoke_account_index.py [port]

The fixture is supplied through the environment rather than defaulted, because this file lives in
the repo and the fixture's password should not.

Why these particular assertions, so a later reader does not weaken them by accident:

- The unknown-email string is asserted VERBATIM. interpre.cpp string-compares it to decide whether
  to offer account creation, and the index path had to reproduce the directory scan's text exactly.
  A reworded message is a real regression even though it reads like a cosmetic change.
- The save check compares the account-directory file's mtime, not the game's "saved" output. This
  is the path where a wrong owner lookup is silently destructive: save_char picks the directory
  from the owner answer and CREATES a file there if absent, so a wrong answer migrates a
  character's saves into an account that does not own it while the game still says it saved.
"""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mudclient import Mud

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 1024
EMAIL = os.environ.get('ROTS_SMOKE_EMAIL')
PASSWORD = os.environ.get('ROTS_SMOKE_PASSWORD')
if not EMAIL or not PASSWORD:
    sys.exit('Set ROTS_SMOKE_EMAIL and ROTS_SMOKE_PASSWORD to an account fixture with at\n'
             'least one linked immortal character. They are deliberately not defaulted:\n'
             'this file is in the repo and the fixture password is not.')
CHARACTER = os.environ.get('ROTS_SMOKE_CHARACTER')
if not CHARACTER:
    sys.exit('Set ROTS_SMOKE_CHARACTER to a linked character on that account at LEVEL_GRGOD or\n'
             'above -- the `account` command is immortal-only, so a mortal cannot run this.')
UNKNOWN_EMAIL = 'definitely-not-a-real-account@example.com'
NO_ACCOUNT = 'No account exists for that email address.'

def account_bucket(email):
    """Mirrors account_bucket_for_name (account_management_storage.cpp)."""
    c = (email or ' ')[0].lower()
    for lo, hi, name in (('a', 'e', 'A-E'), ('f', 'j', 'F-J'), ('k', 'o', 'K-O'),
                         ('p', 't', 'P-T'), ('u', 'z', 'U-Z')):
        if lo <= c <= hi:
            return name
    return 'ZZZ'


ACCOUNT_DIR = os.path.join('lib', 'accounts', account_bucket(EMAIL), EMAIL.lower())

fails = []
def check(label, ok, detail=''):
    print(('  PASS  ' if ok else '  FAIL  ') + label + (('\n         ' + str(detail)) if detail and not ok else ''))
    if not ok:
        fails.append(label)

def connect():
    return Mud('127.0.0.1', PORT, ttype='Mudlet', accept_msdp=True)

def login(m):
    """Email + password -> account menu. Returns the tail for assertions."""
    assert m.wait_for(r'Account email:'), 'no email prompt'
    m.send(EMAIL); assert m.wait_for(r'Account password:'), 'no password prompt'
    m.clear(); m.send(PASSWORD)
    ok = m.wait_for(r'Play a linked character|linked characters')
    return ok, m.tail()

def run_cmd(m, command, pattern, timeout=6.0):
    """Send an in-game command and return the text that followed it."""
    m.clear(); m.send(command)
    m.wait_for(pattern, timeout=timeout)
    return m.tail()


# --- 1. Login paths ------------------------------------------------------------------------------
m = connect()
try:
    assert m.wait_for(r'Account email:'), 'no email prompt'
    m.clear(); m.send(UNKNOWN_EMAIL); m.wait_for(r'.', timeout=3.0); time.sleep(0.8)
    tail = m.tail()
    check('unknown email reports the verbatim non-enumerating string',
          NO_ACCOUNT in tail, tail[-300:])
finally:
    m.close()

m = connect()
try:
    assert m.wait_for(r'Account email:'), 'no email prompt'
    m.send(EMAIL); assert m.wait_for(r'Account password:'), 'no password prompt'
    m.clear(); m.send('WrongPassword123!'); m.wait_for(r'.', timeout=3.0); time.sleep(0.8)
    rejected = m.tail()
    check('a wrong password is rejected',
          'Play a linked character' not in rejected, rejected[-300:])
    m.clear(); m.send(PASSWORD)
    ok = m.wait_for(r'Play a linked character|linked characters')
    check('the right password then works on the same connection', ok, m.tail()[-300:])
finally:
    m.close()

# --- 2. Roster and selection ---------------------------------------------------------------------
m = connect()
try:
    ok, _ = login(m)
    check('login by email reaches the account menu', ok)
    roster = run_cmd(m, '2', r'\d\) \[')
    rows = re.findall(r'(\d+)\) \[\s*(\d+)\s+\S+\] (\S+)', roster)
    check('roster lists linked characters', len(rows) > 0, roster[-400:])
    matching = [r for r, _, n in rows if n.lower() == CHARACTER.lower()]
    check('%s appears on the roster' % CHARACTER, len(matching) == 1, rows[:5])
finally:
    m.close()

# --- 3. Save lands in the ACCOUNT directory, not players/ ----------------------------------------
char_file = os.path.join(ACCOUNT_DIR, CHARACTER.lower() + '.character.json')
before = os.path.getmtime(char_file) if os.path.exists(char_file) else None
check('account-native character file exists to begin with', before is not None, char_file)

m = connect()
try:
    login(m)
    m.clear(); m.send('2'); roster = m.wait_for(r'\d\) \[') and m.tail()
    row = next((r for r, _, n in re.findall(r'(\d+)\) \[\s*(\d+)\s+\S+\] (\S+)', roster)
                if n.lower() == CHARACTER.lower()), None)
    check('found %s on the roster to select' % CHARACTER, row is not None, roster[-400:])
    m.send(row); m.wait_for(r'Enter the game|1\)')
    m.msdp = []
    m.clear(); m.send('1'); m.pump(3.0)
    name = m.msdp_get('CHARACTER_NAME')
    check('selecting that row loads %s (MSDP, not menu text)' % CHARACTER,
          (name or '').lower() == CHARACTER.lower(), 'CHARACTER_NAME=%r' % name)

    # --- 4. The immortal surface, while we are in the game ---------------------------------------
    out = run_cmd(m, 'account index', r'Account index:')
    counts = re.search(r'Account index: (\d+) record\(s\) indexed, (\d+) quarantined, '
                       r'(\d+) unreadable since boot\.', out)
    check('`account index` reports counts', counts is not None, out[-400:])
    if counts:
        on_disk = len([1 for root, _, files in os.walk('lib/accounts') for f in files
                       if f == 'account.json'])
        check('indexed count matches account.json files on disk (%s)' % on_disk,
              int(counts.group(1)) == on_disk,
              'index=%s disk=%s' % (counts.group(1), on_disk))
        check('nothing is quarantined on a healthy tree', counts.group(2) == '0', out[-400:])
        check('nothing is unreadable since boot on a healthy tree', counts.group(3) == '0', out[-400:])

    out = run_cmd(m, 'account index frobnicate', r'Usage: account index')
    check('an unrecognized action prints usage, not the summary',
          'Usage: account index' in out and 'record(s) indexed' not in out,
          out[-300:])

    m.clear(); m.send('quit'); m.pump(2.5)
finally:
    m.close()

time.sleep(1.0)
after = os.path.getmtime(char_file) if os.path.exists(char_file) else None
check('the save landed in the ACCOUNT directory (mtime advanced)',
      before is not None and after is not None and after > before,
      'before=%s after=%s' % (before, after))

print()
print('ALL PASS' if not fails else '%d FAILED: %s' % (len(fails), ', '.join(fails)))
sys.exit(1 if fails else 0)
