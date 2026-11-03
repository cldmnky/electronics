import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin2: ["pin2"],
  pin3: ["pin3"],
  pin4: ["pin4"]
} as const

export const PJ_3200 = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      symbol={
        <symbol>
          <port name="pin4" pinNumber={4} aliases={["4"]} direction="right" schX={0.6} schY={-0.2} schStemLength={0.4} />
          <port name="pin2" pinNumber={2} aliases={["2"]} direction="right" schX={0.6} schY={0} schStemLength={0.4} />
          <port name="pin3" pinNumber={3} aliases={["3"]} direction="right" schX={0.6} schY={0.2} schStemLength={0.4} />
          <schematicpath points={[{"x":0.3,"y":-0.2},{"x":-0.08,"y":-0.2},{"x":-0.12,"y":-0.12},{"x":-0.16,"y":-0.2}]} strokeColor="#880000" />
          <schematicpath points={[{"x":0.2,"y":0},{"x":-0.2,"y":0},{"x":-0.24,"y":-0.08},{"x":-0.28,"y":0}]} strokeColor="#880000" />
          <schematicrect schX={-0.4} schY={-0.06} width={0.08} height={0.28} strokeWidth={0.02} color="#880000" />
          <schematicpath points={[{"x":0.22,"y":0.2},{"x":-0.4,"y":0.2},{"x":-0.4,"y":0.08}]} strokeColor="#880000" />
          <schematictext schX={0.09} schY={0.606} text="{NAME}" fontSize={0.2} anchor="bottom_center" />
        </symbol>
      }
      supplierPartNumbers={{
  "jlcpcb": [
    "C2689690"
  ]
}}
      manufacturerPartNumber="PJ-3200"
      footprint={<footprint>
        <hole pcbX="-3.2249936mm" pcbY="0mm" diameter="1.1999976mm" />
<hole pcbX="2.7749944mm" pcbY="0mm" diameter="1.1999976mm" />
<platedhole  portHints={["pin3"]} pcbX="-3.2249936mm" pcbY="-2.400046mm" holeWidth="1.1999976mm" holeHeight="0.5999988mm" outerWidth="1.7999964mm" outerHeight="1.1999976mm" shape="pill" />
<platedhole  portHints={["pin4"]} pcbX="3.7749924mm" pcbY="-2.400046mm" holeWidth="1.1999976mm" holeHeight="0.5999988mm" outerWidth="1.7999964mm" outerHeight="1.1999976mm" shape="pill" />
<platedhole  portHints={["pin2"]} pcbX="-1.2249976mm" pcbY="2.400046mm" holeWidth="1.1999976mm" holeHeight="0.5999988mm" outerWidth="1.7999964mm" outerHeight="1.1999976mm" shape="pill" />
<silkscreenpath route={[{"x":-4.0439404000001105,"y":-3.047999999999888},{"x":-5.324989400000163,"y":-3.047999999999888}]} />
<silkscreenpath route={[{"x":2.956070999999838,"y":-3.047999999999888},{"x":-2.406046800000013,"y":-3.047999999999888}]} />
<silkscreenpath route={[{"x":-0.35098360000006323,"y":2.999994000000129},{"x":5.736024799999768,"y":2.999994000000129},{"x":5.736024799999768,"y":-3.047999999999888},{"x":4.593964599999936,"y":-3.047999999999888}]} />
<silkscreenpath route={[{"x":-5.324989400000163,"y":2.999994000000129},{"x":-2.09901160000004,"y":2.999994000000129}]} />
<silkscreenpath route={[{"x":-5.324989400000163,"y":2.549753600000031},{"x":-8.360975200000098,"y":2.549753600000031},{"x":-8.360975200000098,"y":-2.5399999999999636},{"x":-5.324989400000163,"y":-2.5399999999999636}]} />
<silkscreenpath route={[{"x":-5.324989400000163,"y":2.999994000000129},{"x":-5.324989400000163,"y":-2.999994000000015}]} />
<silkscreentext text="{NAME}" pcbX="-1.3072936mm" pcbY="4.0226mm" anchorAlignment="center" fontSize="1mm" />
<fabricationnotepath route={[{"x":-8.361000600000125,"y":2.5400000000000773},{"x":-5.313000600000123,"y":2.5400000000000773},{"x":-5.313000600000123,"y":-2.5399999999999636},{"x":-8.361000600000125,"y":-2.5399999999999636},{"x":-8.361000600000125,"y":2.5400000000000773}]} strokeWidth="0mm" isFilled hasStroke={false} />
<courtyardoutline outline={[{"x":-8.57696460000011,"y":3.250044800000069},{"x":5.92300739999996,"y":3.250044800000069},{"x":5.92300739999996,"y":-3.25898559999996},{"x":-8.57696460000011,"y":-3.25898559999996},{"x":-8.57696460000011,"y":3.250044800000069}]} />
      </footprint>}
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C2689690.obj?uuid=0b6dd304872845a09e234e5a8f2421a7",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C2689690.step?uuid=0b6dd304872845a09e234e5a8f2421a7",
        pcbRotationOffset: 0,
        modelOriginPosition: { x: -0.17302139999992505, y: 0.00899159999994481, z: -0.0000055999999997169425 },
      }}
      {...props}
    />
  )
}