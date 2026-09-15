"""Minimal MSDP-aware telnet client for driving the RotS server in tests."""
import socket, time, re, sys

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240
TELOPT_MSDP, TELOPT_TTYPE, TELOPT_ECHO, TELOPT_NAWS, TELOPT_CHARSET = 69, 24, 1, 31, 42
MSDP_VAR, MSDP_VAL, MSDP_TABLE_OPEN, MSDP_TABLE_CLOSE, MSDP_ARRAY_OPEN, MSDP_ARRAY_CLOSE = 1,2,3,4,5,6

class Mud:
    def __init__(self, host='127.0.0.1', port=1024, ttype='Mudlet', accept_msdp=True):
        self.s = socket.create_connection((host, port), timeout=10)
        self.s.settimeout(0.4)
        self.ttype = ttype
        self.accept_msdp = accept_msdp
        self.text = b''          # game text with telnet stripped
        self.raw  = b''          # everything, for spinner byte inspection
        self.msdp = []           # list of (name, value) in arrival order
        self.msdp_raw = []       # raw msdp payloads
        self.buf = b''
        self._ttype_count = 0

    def _send(self, b): self.s.sendall(b)

    def pump(self, seconds=1.0):
        """Read and process for a while."""
        end = time.time() + seconds
        while time.time() < end:
            try:
                d = self.s.recv(4096)
                if not d: break
                self.raw += d
                self.buf += d
                self._process()
            except socket.timeout:
                pass
            except OSError:
                break
        return self

    def _process(self):
        out = bytearray()
        i = 0
        b = self.buf
        while i < len(b):
            if b[i] == IAC:
                if i+1 >= len(b): break
                c = b[i+1]
                if c in (DO, DONT, WILL, WONT):
                    if i+2 >= len(b): break
                    self._negotiate(c, b[i+2]); i += 3; continue
                if c == SB:
                    end = b.find(bytes([IAC, SE]), i)
                    if end == -1: break
                    payload = b[i+2:end]
                    self._subneg(payload)
                    i = end + 2; continue
                if c == IAC:
                    out.append(IAC); i += 2; continue
                i += 2; continue
            out.append(b[i]); i += 1
        self.buf = b[i:]
        self.text += bytes(out)

    def _negotiate(self, cmd, opt):
        if cmd == WILL:
            if opt == TELOPT_MSDP and self.accept_msdp:
                self._send(bytes([IAC, DO, opt]))
            elif opt == TELOPT_ECHO:
                self._send(bytes([IAC, DO, opt]))
            else:
                self._send(bytes([IAC, DONT, opt]))
        elif cmd == DO:
            if opt == TELOPT_TTYPE:
                self._send(bytes([IAC, WILL, opt]))
            elif opt == TELOPT_NAWS:
                self._send(bytes([IAC, WILL, opt]))
                self._send(bytes([IAC, SB, TELOPT_NAWS, 0, 80, 0, 24, IAC, SE]))
            else:
                self._send(bytes([IAC, WONT, opt]))
        elif cmd == DONT:
            self._send(bytes([IAC, WONT, opt]))

    def _subneg(self, p):
        if not p: return
        if p[0] == TELOPT_TTYPE and len(p) > 1 and p[1] == 1:   # SEND
            names = [self.ttype, 'ANSI', 'MTTS 141']
            n = names[min(self._ttype_count, len(names)-1)]
            self._ttype_count += 1
            self._send(bytes([IAC, SB, TELOPT_TTYPE, 0]) + n.encode() + bytes([IAC, SE]))
        elif p[0] == TELOPT_MSDP:
            self.msdp_raw.append(p[1:])
            self._parse_msdp(p[1:])

    def _parse_msdp(self, p):
        i = 0
        while i < len(p):
            if p[i] == MSDP_VAR:
                j = i+1
                name = bytearray()
                while j < len(p) and p[j] not in (MSDP_VAL, MSDP_VAR):
                    name.append(p[j]); j += 1
                val = bytearray()
                if j < len(p) and p[j] == MSDP_VAL:
                    j += 1
                    depth = 0
                    while j < len(p):
                        if p[j] in (MSDP_TABLE_OPEN, MSDP_ARRAY_OPEN): depth += 1
                        elif p[j] in (MSDP_TABLE_CLOSE, MSDP_ARRAY_CLOSE): depth -= 1
                        elif p[j] == MSDP_VAR and depth == 0: break
                        val.append(p[j]); j += 1
                self.msdp.append((name.decode('latin-1'), bytes(val).decode('latin-1')))
                i = j
            else:
                i += 1

    def send(self, line):
        self._send(line.encode('latin-1') + b'\n')
        return self

    def wait_for(self, pattern, timeout=12.0):
        end = time.time() + timeout
        rx = re.compile(pattern, re.I)
        while time.time() < end:
            if rx.search(self.text.decode('latin-1', 'replace')):
                return True
            self.pump(0.3)
        return False

    def clear(self):
        self.text = b''; self.raw = b''; self.msdp = []; self.msdp_raw = []
        return self

    def msdp_get(self, name):
        vals = [v for (n, v) in self.msdp if n == name]
        return vals[-1] if vals else None

    def close(self):
        try: self.s.close()
        except Exception: pass

    def tail(self, n=1500):
        return self.text.decode('latin-1', 'replace')[-n:]
