// The blog's date line. CSS does the uppercasing, and DISPLAY_TZ is one zone for the whole server.

const dateFormatters = new Map<string, Intl.DateTimeFormat>();
const timeFormatters = new Map<string, Intl.DateTimeFormat>();

function fmt(cache: Map<string, Intl.DateTimeFormat>, tz: string, opts: Intl.DateTimeFormatOptions) {
  let f = cache.get(tz);
  if (!f) {
    f = new Intl.DateTimeFormat('en-GB', { ...opts, timeZone: tz });
    cache.set(tz, f);
  }
  return f;
}

export function formatMeta(unix: number, tz: string): { date: string; time: string } {
  const d = new Date(unix * 1000);
  // en-GB says "Sept" for September; the design wants three letters everywhere.
  const parts = fmt(dateFormatters, tz, { weekday: 'short', day: 'numeric', month: 'short' }).formatToParts(d);
  const part = (type: string) => parts.find((p) => p.type === type)?.value ?? '';
  const date = `${part('weekday')} ${part('day')} ${part('month').slice(0, 3)}`;
  const time = fmt(timeFormatters, tz, { hour: '2-digit', minute: '2-digit', hourCycle: 'h23' }).format(d);
  return { date, time };
}

/** Full timestamp for the dev mailbox. */
export function formatFull(unix: number, tz: string): string {
  return new Intl.DateTimeFormat('en-GB', { dateStyle: 'medium', timeStyle: 'short', timeZone: tz }).format(new Date(unix * 1000));
}
