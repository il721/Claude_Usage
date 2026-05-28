import os, sqlite3, shutil, tempfile

chrome = r"F:\!___PORT\GoogleChromePortable\Data\profile"
for profile in ["Default", "Profile 1", "Profile 10"]:
    db = os.path.join(chrome, profile, "Network", "Cookies")
    if not os.path.exists(db):
        continue
    tmp = os.path.join(tempfile.gettempdir(), "cuw_probe.db")
    shutil.copy2(db, tmp)
    con = sqlite3.connect(tmp)
    rows = con.execute(
        "SELECT host_key, name FROM cookies WHERE host_key LIKE '%claude%' OR host_key LIKE '%anthropic%'"
    ).fetchall()
    con.close()
    os.unlink(tmp)
    print(f"{profile}: {rows if rows else 'no claude/anthropic cookies'}")
