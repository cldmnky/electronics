import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["VCCA"],
  pin2: ["GND"],
  pin3: ["A"],
  pin4: ["B"],
  pin5: ["DIR"],
  pin6: ["VCCB"]
} as const

const pinAttributes = {
  pin2: {requiresGround: true},
  pin3: {isBidirectional: true, canUseTriState: true, canUsePushPull: true},
  pin1: {requiresPower: true, shouldHaveDecouplingCapacitor: true},
  pin6: {requiresPower: true, shouldHaveDecouplingCapacitor: true},
  pin5: {isInput: true},
  pin4: {isBidirectional: true, canUseTriState: true, canUsePushPull: true}
} satisfies NonNullable<ChipProps["pinAttributes"]>

export const SN74LVC1T45DBVR = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      pinAttributes={pinAttributes}
      supplierPartNumbers={{
  "jlcpcb": [
    "C7843"
  ]
}}
      manufacturerPartNumber="SN74LVC1T45DBVR"
      footprint={<footprint>
        <smtpad portHints={["pin3"]} pcbX="1.35001mm" pcbY="0.94996mm" width="1.0999978mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin2"]} pcbX="1.35001mm" pcbY="-0mm" width="1.0999978mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin1"]} pcbX="1.35001mm" pcbY="-0.94996mm" width="1.0999978mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin6"]} pcbX="-1.35001mm" pcbY="-0.94996mm" width="1.0999978mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin5"]} pcbX="-1.35001mm" pcbY="-0mm" width="1.0999978mm" height="0.5999988mm" shape="rect" />
<smtpad portHints={["pin4"]} pcbX="-1.35001mm" pcbY="0.94996mm" width="1.0999978mm" height="0.5999988mm" shape="rect" />
<silkscreenpath route={[{"x":-0.899998200000141,"y":1.5499080000000731},{"x":0.9000236000000541,"y":1.5499080000000731}]} />
<silkscreenpath route={[{"x":-0.899998200000141,"y":-1.5501111999999466},{"x":0.9000236000000541,"y":-1.5501111999999466}]} />
<silkscreencircle pcbX="1.397mm" pcbY="-1.651mm" radius="0.127mm" />
<silkscreentext text="{NAME}" pcbX="0.012446mm" pcbY="2.562354mm" anchorAlignment="center" fontSize="1mm" />
<courtyardoutline outline={[{"x":-2.150008899999989,"y":1.6999081999999817},{"x":2.150008899999989,"y":1.6999081999999817},{"x":2.150008899999989,"y":-1.700085999999942},{"x":-2.150008899999989,"y":-1.700085999999942},{"x":-2.150008899999989,"y":1.6999081999999817}]} />
      </footprint>}
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C7843.obj?uuid=229b69761e2c45dba6a83d8866dec72d",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C7843.step?uuid=229b69761e2c45dba6a83d8866dec72d",
        pcbRotationOffset: 180,
        modelOriginPosition: { x: 0.000025399999913133797, y: -0.0000889000000370288, z: -0.048939 },
      }}
      {...props}
    />
  )
}