// Receives rate-limit data from the content script and forwards it
// to the native host, which saves it to ~/.claude/widget_limits.json

chrome.runtime.onMessage.addListener((msg) => {
  if (msg.type !== 'RATE_LIMITS') return;

  const payload = { _endpoint: msg.endpoint, _ts: Date.now(), ...msg.data };

  chrome.runtime.sendNativeMessage('com.claude.widget', payload, (response) => {
    if (chrome.runtime.lastError) {
      console.error('[widget] native host error:', chrome.runtime.lastError.message);
    } else {
      console.log('[widget] saved via native host:', response);
    }
  });
});
