// Service worker: drives polling and bridges data to the native host.
//
// Why an alarm instead of a page setInterval: a content-script timer is throttled
// (and eventually frozen) by Chrome when the claude.ai tab is backgrounded, so the
// widget would silently stop updating. chrome.alarms wakes the service worker on a
// reliable schedule even when the tab is in the background; we then poke each open
// claude.ai tab to fetch fresh usage *in the page context* (first-party cookies).

const ALARM      = 'poll-usage';
const PERIOD_MIN = 1;   // chrome.alarms minimum granularity is 1 minute

// Idempotent — safe to call on every service-worker wake-up.
chrome.alarms.create(ALARM, { periodInMinutes: PERIOD_MIN });

chrome.runtime.onInstalled.addListener(pokeTabs);
chrome.runtime.onStartup.addListener(pokeTabs);

chrome.alarms.onAlarm.addListener((a) => {
  if (a.name === ALARM) pokeTabs();
});

// Ask every open claude.ai tab to fetch fresh usage. Runs in the page, so the
// request carries the user's first-party claude.ai cookies.
async function pokeTabs() {
  try {
    const tabs = await chrome.tabs.query({ url: 'https://claude.ai/*' });
    for (const t of tabs) {
      if (t.id != null && t.discarded !== true) {
        // Swallow "no receiver" errors for tabs whose content script isn't ready.
        chrome.tabs.sendMessage(t.id, { type: 'FETCH_NOW' }, () => void chrome.runtime.lastError);
      }
    }
  } catch (e) {
    console.error('[widget] pokeTabs failed:', e);
  }
}

// Forward rate-limit data from the content script to the native host,
// which saves it to ~/.claude/widget_limits.json
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
