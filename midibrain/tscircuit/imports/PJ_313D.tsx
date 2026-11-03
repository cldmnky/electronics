import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["pin1"],
  pin2: ["pin2"],
  pin3: ["pin3"],
  pin11: ["pin11"],
  pin12: ["pin12"],
  pin13: ["pin13"]
} as const

export const PJ_313D = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      symbol={
        <symbol>
          <schematicrect schX={-0.4} schY={-0.05} width={0.12} height={0.3} strokeWidth={0.02} color="#880000" />
          <schematicpath points={[{"x":-0.4,"y":0.1},{"x":-0.4,"y":0.2},{"x":0.2,"y":0.2}]} strokeColor="#880000" />
          <schematicpath points={[{"x":0.2,"y":0},{"x":-0.18,"y":0},{"x":-0.22,"y":-0.08},{"x":-0.26,"y":0}]} strokeColor="#880000" />
          <schematicpath points={[{"x":0.2,"y":-0.2},{"x":-0.12,"y":-0.2},{"x":-0.16,"y":-0.14},{"x":-0.18,"y":-0.2}]} strokeColor="#880000" />
          <port name="pin1" pinNumber={1} aliases={["1"]} direction="right" schX={0.4} schY={0.2} schStemLength={0.2} />
          <port name="pin2" pinNumber={2} aliases={["2"]} direction="right" schX={0.4} schY={0} schStemLength={0.2} />
          <port name="pin3" pinNumber={3} aliases={["3"]} direction="right" schX={0.4} schY={-0.2} schStemLength={0.2} />
          <port name="pin11" pinNumber={11} aliases={["pin11"]} direction="right" schX={0.4} schY={-0.4} schStemLength={0.2} />
          <port name="pin12" pinNumber={12} aliases={["pin12"]} direction="right" schX={0.4} schY={-0.6} schStemLength={0.2} />
          <port name="pin13" pinNumber={13} aliases={["pin13"]} direction="right" schX={0.4} schY={-0.8} schStemLength={0.2} />
          <schematictext schX={-0.02} schY={0.606} text="{NAME}" fontSize={0.2} anchor="bottom_center" />
        </symbol>
      }
      supplierPartNumbers={{
  "jlcpcb": [
    "C668608"
  ]
}}
      manufacturerPartNumber="PJ-313D"
      footprint={<footprint insertionDirection="from_above">
        <hole pcbX="0mm" pcbY="2.66911455mm" diameter="1.3000228mm" />
<hole pcbX="-0.00762mm" pcbY="-3.34410685mm" diameter="1.3000228mm" />
<smtpad portHints={["pin1"]} pcbX="3.599942mm" pcbY="3.36911315mm" width="2.499995mm" height="1.2500102mm" shape="rect" />
<smtpad portHints={["pin3"]} pcbX="3.599942mm" pcbY="-3.34143985mm" width="2.499995mm" height="1.2500102mm" shape="rect" />
<smtpad portHints={["pin2"]} pcbX="3.599942mm" pcbY="0.15855315mm" width="2.499995mm" height="1.2500102mm" shape="rect" />
<smtpad portHints={["pin11"]} pcbX="-3.599942mm" pcbY="3.36911315mm" width="2.499995mm" height="1.2500102mm" shape="rect" />
<smtpad portHints={["pin13"]} pcbX="-3.599942mm" pcbY="-3.34143985mm" width="2.499995mm" height="1.2500102mm" shape="rect" />
<smtpad portHints={["pin12"]} pcbX="-3.599942mm" pcbY="0.15852775mm" width="2.499995mm" height="1.2500102mm" shape="rect" />
<silkscreenpath route={[{"x":2.999994000000015,"y":-2.4853074499999366},{"x":2.999994000000015,"y":-2.185612849999984}]} />
<silkscreenpath route={[{"x":-2.999994000000129,"y":-4.197597650000034},{"x":-2.999994000000129,"y":-6.3261430500000415},{"x":2.7450033999999732,"y":-6.3261430500000415},{"x":2.999994000000015,"y":-6.3261430500000415},{"x":2.999994000000015,"y":-4.197597650000034}]} />
<silkscreenpath route={[{"x":-2.999994000000129,"y":-2.185612849999984},{"x":-2.999994000000129,"y":-2.4853074499999366}]} />
<silkscreenpath route={[{"x":-2.999994000000129,"y":5.179625149999993},{"x":2.999994000000015,"y":5.179625149999993}]} />
<silkscreenpath route={[{"x":2.5001219999999194,"y":7.669079149999902},{"x":2.5001219999999194,"y":5.168957150000097}]} />
<silkscreenpath route={[{"x":-2.499868000000106,"y":7.668825150000089},{"x":-2.499868000000106,"y":5.169465150000065}]} />
<silkscreenpath route={[{"x":-2.499868000000106,"y":7.668825150000089},{"x":2.5001219999999194,"y":7.668825150000089}]} />
<silkscreenpath route={[{"x":-2.999994000000129,"y":5.179625149999993},{"x":-2.999994000000129,"y":4.2253471500000614}]} />
<silkscreenpath route={[{"x":-2.999994000000129,"y":2.512879150000117},{"x":-2.999994000000129,"y":1.0147871500000747}]} />
<silkscreenpath route={[{"x":-2.999994000000129,"y":-0.6976808499998697},{"x":-2.999994000000129,"y":-2.185612849999984}]} />
<silkscreenpath route={[{"x":2.999994000000015,"y":-2.185612849999984},{"x":2.999994000000015,"y":-0.6976808499998697}]} />
<silkscreenpath route={[{"x":2.999994000000015,"y":1.0147871500000747},{"x":2.999994000000015,"y":2.512879150000117}]} />
<silkscreenpath route={[{"x":2.999994000000015,"y":4.2253471500000614},{"x":2.999994000000015,"y":5.168957150000097}]} />
<silkscreentext text="{NAME}" pcbX="0mm" pcbY="8.68711315mm" anchorAlignment="center" fontSize="1mm" />
<courtyardoutline outline={[{"x":-5.099939499999891,"y":7.9136181500000475},{"x":5.099939499999778,"y":7.9136181500000475},{"x":5.099939499999778,"y":-6.586353850000023},{"x":-5.099939499999891,"y":-6.586353850000023},{"x":-5.099939499999891,"y":7.9136181500000475}]} />
      </footprint>}
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C668608.obj?uuid=b4d18d9c17f14266a82ccb70c0335909",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C668608.step?uuid=b4d18d9c17f14266a82ccb70c0335909",
        pcbRotationOffset: 0,
        modelOriginPosition: { x: 0.000012700000070253736, y: -0.6636321500001259, z: -0.100001 },
      }}
      {...props}
    />
  )
}