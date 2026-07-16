// Fetches Claude.ai session/weekly usage and forwards it to the service worker.
// Polling is primarily driven by the service-worker alarm (FETCH_NOW message),
// which keeps working even when this tab is backgrounded and page timers are
// throttled. The setInterval below is only a best-effort fallback.
const INTERVAL_MS = 1 * 60 * 1000;
let cachedOrgId = null;

async function fetchOrgId() {
  // Resolve the org UUID from the cheapest reliable source first, falling back
  // through progressively more expensive ones. Each source is tolerant of the
  // several response shapes claude.ai has shipped over time.

  // 1. lastActiveOrg cookie — set by claude.ai, no network, no shape guessing.
  const c = document.cookie.match(/(?:^|;\s*)lastActiveOrg=([0-9a-fA-F-]{36})/);
  if (c) return c[1];

  // 2. /api/organizations — array, or wrapped in {organizations} / {data};
  //    id field may be `uuid` or `id`.
  try {
    const r = await fetch('/api/organizations', { headers: { Accept: 'application/json' } });
    if (r.ok) {
      const data = await r.json();
      const orgs = Array.isArray(data) ? data : (data.organizations ?? data.data ?? []);
      if (orgs[0]) return orgs[0].uuid ?? orgs[0].id ?? null;
    }
  } catch (_) {}

  // 3. /api/account — org lives under memberships[0].organization.
  try {
    const r = await fetch('/api/account', { headers: { Accept: 'application/json' } });
    if (r.ok) {
      const data = await r.json();
      const m = data?.memberships ?? data?.account?.memberships;
      const org = m?.[0]?.organization;
      if (org) return org.uuid ?? org.id ?? null;
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
