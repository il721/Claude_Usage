"""
Reads ~/.claude/projects/**/*.jsonl (last 30 days) and outputs:
  Daily lines:  YYYY-MM-DD|input|output|cache_read|cache_creation
  Model lines:  MODEL|model_name|total_tokens
"""
import os, json
from datetime import datetime, timedelta, timezone
from pathlib import Path
from collections import defaultdict

def main():
    userprofile  = os.environ.get('USERPROFILE', '')
    projects_dir = Path(userprofile) / '.claude' / 'projects'
    if not projects_dir.exists():
        return

    cutoff = datetime.now(timezone.utc) - timedelta(days=30)
    daily  = defaultdict(lambda: [0, 0, 0, 0])
    models = defaultdict(int)

    for jf in projects_dir.glob('**/*.jsonl'):
        try:
            with open(jf, encoding='utf-8', errors='ignore') as f:
                for raw in f:
                    raw = raw.strip()
                    if not raw:
                        continue
                    try:
                        obj = json.loads(raw)
                        if obj.get('type') != 'assistant':
                            continue
                        msg   = obj.get('message') or {}
                        usage = msg.get('usage') or {}
                        if not usage:
                            continue
                        ts = obj.get('timestamp', '')
                        if not ts:
                            continue
                        if ts.endswith('Z'):
                            ts = ts[:-1] + '+00:00'
                        dt = datetime.fromisoformat(ts)
                        if dt.tzinfo is None:
                            dt = dt.replace(tzinfo=timezone.utc)
                        if dt < cutoff:
                            continue
                        day = dt.astimezone().strftime('%Y-%m-%d')
                        inp = usage.get('input_tokens', 0)
                        out = usage.get('output_tokens', 0)
                        cr  = usage.get('cache_read_input_tokens', 0)
                        cc  = usage.get('cache_creation_input_tokens', 0)
                        daily[day][0] += inp
                        daily[day][1] += out
                        daily[day][2] += cr
                        daily[day][3] += cc
                        model = msg.get('model', '')
                        if model:
                            models[model] += inp + out + cr + cc
                    except Exception:
                        pass
        except Exception:
            pass

    for day in sorted(daily):
        d = daily[day]
        if any(d):
            print(f'{day}|{d[0]}|{d[1]}|{d[2]}|{d[3]}')

    for model, total in sorted(models.items(), key=lambda x: -x[1]):
        if total > 0:
            print(f'MODEL|{model}|{total}')

if __name__ == '__main__':
    main()
