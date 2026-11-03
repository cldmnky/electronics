import type { PushButtonProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["pin1"],
  pin2: ["pin2"],
  pin3: ["pin3"],
  pin4: ["pin4"]
} as const

const pinAttributes = {
  pin1: {isPassive: true},
  pin3: {isPassive: true},
  pin2: {isPassive: true},
  pin4: {isPassive: true}
} satisfies NonNullable<PushButtonProps["pinAttributes"]>

export const B3F_1000 = (props: PushButtonProps<typeof pinLabels>) => {
  const { name = "SW1", ...restProps } = props

  return (
    <pushbutton
      name={name}
      pinLabels={pinLabels}
      supplierPartNumbers={{
  "jlcpcb": [
    "C93157"
  ]
}}
      manufacturerPartNumber="B3F-1000"
      pinAttributes={pinAttributes}
      footprint={<footprint>
        <platedhole  portHints={["pin1"]} pcbX="-3.2512mm" pcbY="2.25044mm" outerDiameter="1.7999964mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin3"]} pcbX="-3.2512mm" pcbY="-2.25044mm" outerDiameter="1.7999964mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin2"]} pcbX="3.2512mm" pcbY="2.25044mm" outerDiameter="1.7999964mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin4"]} pcbX="3.2512mm" pcbY="-2.25044mm" outerDiameter="1.7999964mm" holeDiameter="1.1000232mm" shape="circle" />
<silkscreenpath route={[{"x":-2.5057862000001023,"y":-3.1000699999999597},{"x":2.505786199999875,"y":-3.1000699999999597}]} />
<silkscreenpath route={[{"x":3.1000699999999597,"y":-1.1294617999999446},{"x":3.1000699999999597,"y":1.1294618000000582}]} />
<silkscreenpath route={[{"x":2.505786199999875,"y":3.1000699999999597},{"x":-2.5057862000001023,"y":3.1000699999999597}]} />
<silkscreenpath route={[{"x":-3.1000700000000734,"y":1.1294618000000582},{"x":-3.1000700000000734,"y":-1.1294617999999446}]} />
<silkscreencircle pcbX="0mm" pcbY="0mm" radius="1.143mm" />
<silkscreencircle pcbX="0mm" pcbY="0mm" radius="1.50114mm" />
<silkscreencircle pcbX="1.778mm" pcbY="1.778mm" radius="0.299974mm" />
<silkscreencircle pcbX="1.778mm" pcbY="-1.778mm" radius="0.299974mm" />
<silkscreencircle pcbX="-1.778mm" pcbY="1.778mm" radius="0.299974mm" />
<silkscreencircle pcbX="-1.778mm" pcbY="-1.524mm" radius="0.299974mm" />
<silkscreentext text="{NAME}" pcbX="0.0127mm" pcbY="4.2004mm" anchorAlignment="center" fontSize="1mm" />
<courtyardoutline outline={[{"x":-4.401198200000067,"y":3.400438200000167},{"x":4.401198199999953,"y":3.400438200000167},{"x":4.401198199999953,"y":-3.4004382000000533},{"x":-4.401198200000067,"y":-3.4004382000000533},{"x":-4.401198200000067,"y":3.400438200000167}]} />
      </footprint>}
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C93157.obj?uuid=71864a7a016f4b2fbc820529eb6ce19c",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C93157.step?uuid=71864a7a016f4b2fbc820529eb6ce19c",
        pcbRotationOffset: 0,
        modelOriginPosition: { x: 0.000012700000070253736, y: 0, z: -0.000005999999999950489 },
      }}
      {...restProps}
    />
  )
}