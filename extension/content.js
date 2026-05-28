// Fetches Claude.ai session/weekly usage and sends to native host every 5 min.
const INTERVAL_MS = 5 * 60 * 1000;
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

fetchAndSend();
setInterval(fetchAndSend, INTERVAL_MS);
