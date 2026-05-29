# /pack — Update Claude_Usage_Widget.zip

Pack all runtime files into `Claude_Usage_Widget.zip` in the project root,
ready to deploy on another machine.

## Files to include

| Source | Zip path |
|--------|----------|
| `cmake-build-debug/Claude_Usage.exe` | `Claude_Usage_Widget/cmake-build-debug/Claude_Usage.exe` |
| `cmake-build-debug/libgcc_s_seh-1.dll` | `Claude_Usage_Widget/cmake-build-debug/libgcc_s_seh-1.dll` |
| `cmake-build-debug/libstdc++-6.dll` | `Claude_Usage_Widget/cmake-build-debug/libstdc++-6.dll` |
| `cmake-build-debug/libwinpthread-1.dll` | `Claude_Usage_Widget/cmake-build-debug/libwinpthread-1.dll` |
| `get_limits.py` | `Claude_Usage_Widget/get_limits.py` |
| `get_daily.py` | `Claude_Usage_Widget/get_daily.py` |
| `native_host/host.py` | `Claude_Usage_Widget/native_host/host.py` |
| `native_host/host.bat` | `Claude_Usage_Widget/native_host/host.bat` |
| `extension/manifest.json` | `Claude_Usage_Widget/extension/manifest.json` |
| `extension/background.js` | `Claude_Usage_Widget/extension/background.js` |
| `extension/content.js` | `Claude_Usage_Widget/extension/content.js` |
| `setup.bat` | `Claude_Usage_Widget/setup.bat` |

**Never include** `native_host/com.claude.widget.json` — it contains a hardcoded
absolute path; `setup.bat` regenerates it correctly on the target machine.

## Steps

### 1. Ensure the exe is fresh

Check that `cmake-build-debug/Claude_Usage.exe` exists. If the user just made
source changes and hasn't rebuilt, build first with `/clion-cpp build`.

### 2. Pack

Run via the Bash tool:

```bash
python -c "
import zipfile, os
root = 'F:/____IL_CPP/Claude_Usage'
files = [
    ('cmake-build-debug/Claude_Usage.exe',    'Claude_Usage_Widget/cmake-build-debug/Claude_Usage.exe'),
    ('cmake-build-debug/libgcc_s_seh-1.dll',  'Claude_Usage_Widget/cmake-build-debug/libgcc_s_seh-1.dll'),
    ('cmake-build-debug/libstdc++-6.dll',     'Claude_Usage_Widget/cmake-build-debug/libstdc++-6.dll'),
    ('cmake-build-debug/libwinpthread-1.dll', 'Claude_Usage_Widget/cmake-build-debug/libwinpthread-1.dll'),
    ('native_host/host.py',                   'Claude_Usage_Widget/native_host/host.py'),
    ('native_host/host.bat',                  'Claude_Usage_Widget/native_host/host.bat'),
    ('extension/manifest.json',               'Claude_Usage_Widget/extension/manifest.json'),
    ('extension/background.js',               'Claude_Usage_Widget/extension/background.js'),
    ('extension/content.js',                  'Claude_Usage_Widget/extension/content.js'),
    ('get_limits.py',                         'Claude_Usage_Widget/get_limits.py'),
    ('get_daily.py',                          'Claude_Usage_Widget/get_daily.py'),
    ('setup.bat',                             'Claude_Usage_Widget/setup.bat'),
]
out = root + '/Claude_Usage_Widget.zip'
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for src, dst in files:
        z.write(root + '/' + src, dst)
        print('+', dst)
size_mb = os.path.getsize(out) / 1024 / 1024
print(f'Done: {out}  ({size_mb:.1f} MB)')
"
```

### 3. Confirm

Report the output path and final size to the user.
