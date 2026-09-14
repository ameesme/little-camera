// Handles are subdomains, so the rules are DNS label rules minus the
// awkward cases: lowercase letters, digits and dashes, 2–31 characters,
// not starting with a dash. A few names are kept for the apex itself.

export const HANDLE = /^[a-z0-9][a-z0-9-]{1,30}$/;
export const RESERVED_HANDLES = new Set(['www', 'api', 'dev', 'mail', 'admin', 'app']);

/** `mees.example.com:3000` + `example.com:3000` → `mees`; null when the host is not one label under the apex. */
export function handleFromHost(host: string, baseDomain: string): string | null {
  if (!host.endsWith('.' + baseDomain)) return null;
  const handle = host.slice(0, -(baseDomain.length + 1));
  return HANDLE.test(handle) ? handle : null;
}
