import type { DiodeProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["cathode","neg"],
  pin2: ["anode","pos"]
} as const

const pinAttributes = {
  pin1: {isPassive: true},
  pin2: {isPassive: true}
} satisfies NonNullable<DiodeProps["pinAttributes"]>

export const A_1N4148WS = (props: DiodeProps) => {
  const { name = "D1", ...restProps } = props

  return (
    <diode
      name={name}
      pinLabels={pinLabels}
      supplierPartNumbers={{
  "jlcpcb": [
    "C2128"
  ]
}}
      manufacturerPartNumber="1N4148WS"
      pinAttributes={pinAttributes}
      footprint={<footprint>
        <smtpad portHints={["pin1","cathode","neg"]} pcbX="-1.172464mm" pcbY="0mm" width="0.999998mm" height="0.6999986mm" shape="rect" />
<smtpad portHints={["pin2","anode","pos"]} pcbX="1.172464mm" pcbY="0mm" width="0.999998mm" height="0.6999986mm" shape="rect" />
<silkscreenpath route={[{"x":0.9012681999998904,"y":-0.7262875999999778},{"x":0.9012681999998904,"y":-0.5200903999999582}]} />
<silkscreenpath route={[{"x":0.9012681999998904,"y":0.7260843999999906},{"x":0.9012681999998904,"y":0.5299455999999054}]} />
<silkscreenpath route={[{"x":-0.8512048000001187,"y":0.7260843999999906},{"x":0.9012681999998904,"y":0.7260843999999906}]} />
<silkscreenpath route={[{"x":-0.8512048000001187,"y":-0.7262875999999778},{"x":0.9012681999998904,"y":-0.7262875999999778}]} />
<silkscreenpath route={[{"x":-0.4467352000001483,"y":0.7260843999999906},{"x":-0.4467352000001483,"y":-0.7262875999999778}]} />
<silkscreentext text="{NAME}" pcbX="0.007366mm" pcbY="1.796544mm" anchorAlignment="center" fontSize="1mm" />
<fabricationnotepath route={[{"x":1.0830305999999155,"y":0.5298947999999655},{"x":1.0830305999999155,"y":-0.5101081999999906},{"x":0.9270237999999154,"y":-0.5101081999999906},{"x":0.9270237999999154,"y":0.5298947999999655},{"x":1.0830305999999155,"y":0.5298947999999655}]} strokeWidth="0mm" isFilled hasStroke={false} />
<fabricationnotepath route={[{"x":1.3150087999999869,"y":0.0898651999999629},{"x":1.3150087999999869,"y":-0.06611619999989671},{"x":0.6909815999999864,"y":-0.06611619999989671},{"x":0.6909815999999864,"y":0.0898651999999629},{"x":1.3150087999999869,"y":0.0898651999999629}]} strokeWidth="0mm" isFilled hasStroke={false} />
<fabricationnotepath route={[{"x":-1.2249911999999767,"y":0.06987540000000081},{"x":-1.2249911999999767,"y":-0.0861313999999993},{"x":-0.6009132000002637,"y":-0.0861313999999993},{"x":-0.6009132000002637,"y":0.06987540000000081},{"x":-1.2249911999999767,"y":0.06987540000000081}]} strokeWidth="0mm" isFilled hasStroke={false} />
<courtyardoutline outline={[{"x":-1.9224629999999934,"y":0.8998843999999053},{"x":1.9224629999999934,"y":0.8998843999999053},{"x":1.9224629999999934,"y":-0.900113000000033},{"x":-1.9224629999999934,"y":-0.900113000000033},{"x":-1.9224629999999934,"y":0.8998843999999053}]} />
      </footprint>}
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C2128.obj?uuid=7459fe65e23146c0a8d836e46a0add72",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C2128.step?uuid=7459fe65e23146c0a8d836e46a0add72",
        pcbRotationOffset: 0,
        modelOriginPosition: { x: 0, y: 0.00011430000006384944, z: 0 },
      }}
      {...restProps}
    />
  )
}