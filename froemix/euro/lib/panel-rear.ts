// Reverse-side panel identification: shared by rendering and its checks.
import { BOARD_REVISION, buildDate } from "../../shared/build-info"
import { textPaths } from "./panel-art"

export const PANEL_REAR_STROKE = 0.2
export function panelRearPaths() {
  return [
    ...textPaths(`FROEMIX PANEL REV ${BOARD_REVISION}`, 1.3, 0, -57.5),
    ...textPaths(buildDate(), 1.3, 0, -60),
  ].map(path => path.map(point => ({ x: -point.x, y: point.y })))
}
