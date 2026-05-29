// Fetches Claude.ai session/weekly usage and forwards it to the service worker.
// Polling is primarily driven by the service-worker alarm (FETCH_NOW message),
// which keeps working even when this tab is backgrounded and page timers are
// throttled. The setInterval below is only a best-effort fallback.
const INTERVAL_MS = 3 * 60 * 1000;
let cachedOrgId = null;

async function fetchOrgId() {
  // Try /api/organizations first (returns list of orgs)
  try {
    const r = await fetch('/api/organizations', { headers: { Accept: 'application/json' } });
    if (r.ok) {
      const data = await r.json();
      const orgs = Array.isArray(data) ? data : (data.organizations ?? []);
      if (orgs[0]?.uuid) return orgs[0].uuid;
    }
  } catch (_) {}
  // Fall back to /api/account
  try {
    const r = await fetch('/api/account', { headers: { Accept: 'application/json' } });
    if (r.ok) {
      const data = await r.json();
      return data?.memberships?.[0]?.organization?.uuid ?? null;
    }
  } catch (_) {}
  return null;
}

async function fetchAndSend() {
  try {
    if (!cachedOrgId) cachedOrgId = await fetchOrgId();
    if (!cachedOrgId) { console.error('[widget] could not resolve org UUID'); return; }
    const r = await fetch(`/api/organizations/${cachedOrgId}/usage`, {
      headers: { Accept: 'application/json' },
    });
    if (!r.ok) { console.error('[widget] usage endpoint returned', r.status); return; }
    const data = await r.json();
    chrome.runtime.sendMessage({
      type: 'RATE_LIMITS',
      endpoint: `/api/organizations/${cachedOrgId}/usage`,
      data,
    });
    console.log('[widget] sent usage data');
  } catch (e) {
    console.error('[widget] fetch failed:', e.message);
  }
}

// Fetch once on load.
fetchAndSend();

// Primary trigger: the service-worker alarm pokes us even when our own timers are
// throttled by Chrome's background-tab throttling.
chrome.runtime.onMessage.addListener((msg) => {
  if (msg?.type === 'FETCH_NOW') fetchAndSend();
});

// Fallback only (subject to background-tab throttling).
setInterval(fetchAndSend, INTERVAL_MS);
