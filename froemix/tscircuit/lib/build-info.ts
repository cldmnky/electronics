// Board silkscreen identity. Rev changes are an explicit design decision.
//
// Duplicated in `euro/lib/build-info.ts` on purpose: `tsci dev` serves only the
// files inside its own project directory, so a shared import outside this
// folder breaks the browser preview. `scripts/test_silk.test.ts` asserts both
// copies carry the same revision and the same build date.
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
