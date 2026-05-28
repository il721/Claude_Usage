"""
Extracts Chrome sessionKey for claude.ai and probes rate-limit API endpoints.
Handles locked SQLite databases (Chrome open) via FILE_SHARE_READ|WRITE|DELETE.
"""
import os, json, sqlite3, tempfile, ctypes, base64, urllib.request, urllib.error
from ctypes import wintypes
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

CHROME_DATA = r"F:\!___PORT\GoogleChromePortable\Data\profile"
LOCAL_STATE = os.path.join(CHROME_DATA, "Local State")

GENERIC_READ         = 0x80000000
FILE_SHARE_ALL       = 0x00000001 | 0x00000002 | 0x00000004
OPEN_EXISTING        = 3
FILE_FLAG_SEQUENTIAL = 0x08000000

# ── Read a Windows file even when another process has it open ─────────────────
def read_file_shared(path: str) -> bytes:
    h = ctypes.windll.kernel32.CreateFileW(
        path, GENERIC_READ, FILE_SHARE_ALL, None, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL, None)
    if h == ctypes.c_void_p(-1).value:
        raise PermissionError(f"CreateFile failed ({ctypes.GetLastError()}): {path}")
    try:
        size = ctypes.windll.kernel32.GetFileSize(h, None)
        buf  = ctypes.create_string_buffer(size)
        read = wintypes.DWORD()
        ctypes.windll.kernel32.ReadFile(h, buf, size, ctypes.byref(read), None)
        return buf.raw[:read.value]
    finally:
        ctypes.windll.kernel32.CloseHandle(h)

def copy_shared(src: str, dst: str):
    # Copy only the main DB — skip WAL/SHM to avoid inconsistency
    data = read_file_shared(src)
    with open(dst, "wb") as f:
        f.write(data)

# ── Chrome AES key via DPAPI ──────────────────────────────────────────────────
def get_chrome_aes_key() -> bytes:
    with open(LOCAL_STATE, encoding="utf-8") as f:
        enc_b64 = json.load(f)["os_crypt"]["encrypted_key"]
    enc = base64.b64decode(enc_b64)[5:]          # strip "DPAPI" prefix

    class BLOB(ctypes.Structure):
        _fields_ = [("cbData", wintypes.DWORD),
                    ("pbData", ctypes.POINTER(ctypes.c_char))]

    inp = BLOB(len(enc), ctypes.cast(ctypes.c_char_p(enc), ctypes.POINTER(ctypes.c_char)))
    out = BLOB()
    ok  = ctypes.windll.crypt32.CryptUnprotectData(
        ctypes.byref(inp), None, None, None, None, 0, ctypes.byref(out))
    if not ok:
        raise RuntimeError("CryptUnprotectData failed")
    key = ctypes.string_at(out.pbData, out.cbData)
    ctypes.windll.kernel32.LocalFree(out.pbData)
    return key

def decrypt_cookie(enc_val: bytes, key: bytes) -> str:
    if enc_val[:3] != b"v10":
        return enc_val.decode("utf-8", errors="ignore")
    return AESGCM(key).decrypt(enc_val[3:15], enc_val[15:], None).decode("utf-8")

# ── Find sessionKey across all profiles ──────────────────────────────────────
def find_session_key() -> str | None:
    key = get_chrome_aes_key()
    tmp = os.path.join(tempfile.gettempdir(), "cuw_cookies.db")

    tmp = os.path.join(tempfile.gettempdir(), "cuw_cookies.db")
    for profile in ["Default", "Profile 1", "Profile 10", "Profile 2"]:
        db = os.path.join(CHROME_DATA, profile, "Network", "Cookies")
        if not os.path.exists(db):
            continue
        try:
            # Use ctypes shared-read to bypass Chrome's file lock
            copy_shared(db, tmp)
            con  = sqlite3.connect(tmp)
            con.execute("PRAGMA journal_mode=DELETE")  # disable WAL on the copy
            rows = con.execute(
                "SELECT name, encrypted_value FROM cookies "
                "WHERE host_key LIKE '%claude.ai%'"
            ).fetchall()
            con.close()
            print(f"  {profile}: cookies -> {[r[0] for r in rows]}")
            for name, enc in rows:
                if name == "sessionKey":
                    return decrypt_cookie(enc, key)
        except Exception as e:
            print(f"  {profile}: {e}")
        finally:
            for ext in ["", "-wal", "-shm"]:
                try: os.unlink(tmp + ext)
                except: pass
    return None

# ── Probe claude.ai API endpoints ────────────────────────────────────────────
def probe(sk: str):
    headers = {
        "Cookie":          f"sessionKey={sk}",
        "User-Agent":      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                           "AppleWebKit/537.36 (KHTML, like Gecko) "
                           "Chrome/125.0.0.0 Safari/537.36",
        "Accept":          "application/json, */*",
        "Accept-Language": "en-US,en;q=0.9",
        "Referer":         "https://claude.ai/",
    }
    endpoints = [
        "https://claude.ai/api/account",
        "https://claude.ai/api/auth/session",
        "https://claude.ai/api/rate_limits",
        "https://claude.ai/api/usage",
        "https://claude.ai/api/organizations",
        "https://claude.ai/api/bootstrap",
    ]
    for ep in endpoints:
        req = urllib.request.Request(ep, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                body = r.read(2000).decode("utf-8", errors="replace")
                print(f"\n✓ {ep}  [{r.status}]\n{body[:600]}")
        except urllib.error.HTTPError as e:
            body = e.read(300).decode("utf-8", errors="replace")
            print(f"  {e.code}  {ep}")
            if "Just a moment" not in body:
                print(f"       {body[:200]}")
        except Exception as e:
            print(f"  ERR  {ep} → {e}")

if __name__ == "__main__":
    print("Searching for claude.ai sessionKey in Chrome profiles…")
    sk = find_session_key()
    if not sk:
        print("\nNo sessionKey found — open claude.ai in Chrome and log in, then retry.")
    else:
        print(f"\nGot sessionKey (len={len(sk)}), probing API…")
        probe(sk)
