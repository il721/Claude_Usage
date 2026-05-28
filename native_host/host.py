"""
Chrome Native Messaging host for Claude Usage Widget.
Receives rate-limit data from the extension and saves it to
%USERPROFILE%\.claude\widget_limits.json
"""
import sys, json, struct, os

OUT = os.path.join(os.environ.get('USERPROFILE', ''), '.claude', 'widget_limits.json')

def read_msg():
    raw = sys.stdin.buffer.read(4)
    if len(raw) < 4:
        return None
    n = struct.unpack('@I', raw)[0]
    return json.loads(sys.stdin.buffer.read(n).decode('utf-8'))

def send_msg(obj):
    data = json.dumps(obj).encode('utf-8')
    sys.stdout.buffer.write(struct.pack('@I', len(data)) + data)
    sys.stdout.buffer.flush()

while True:
    msg = read_msg()
    if msg is None:
        break
    try:
        with open(OUT, 'w', encoding='utf-8') as f:
            json.dump(msg, f, indent=2)
        send_msg({'ok': True})
    except Exception as e:
        send_msg({'ok': False, 'error': str(e)})
