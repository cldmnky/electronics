// Board silkscreen identity. Rev changes are an explicit design decision.
//
// Keep identical to `tscircuit/lib/build-info.ts`: the base gate compares both
// copies. The duplication exists because `tsci dev` cannot serve imports from
// outside a project directory.
export const BOARD_REVISION = "0.1"

/** UTC build date, captured by the release scripts. Override for reproducible builds. */
export function buildDate(): string {
  const value = typeof process === "undefined" ? undefined : process.env.FROEMIX_BUILD_DATE
  const date = value ?? new Date().toISOString().slice(0, 10)
  if (!/^\d{4}-\d{2}-\d{2}$/.test(date)) throw new Error(`Invalid FROEMIX_BUILD_DATE: ${date}`)
  const parsed = new Date(`${date}T00:00:00Z`)
  if (Number.isNaN(parsed.valueOf()) || parsed.toISOString().slice(0, 10) !== date) {
    throw new Error(`Invalid FROEMIX_BUILD_DATE: ${date}`)
  }
  return date
}
