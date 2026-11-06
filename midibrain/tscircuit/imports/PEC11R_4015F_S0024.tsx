import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["pin1"],
  pin2: ["pin2"],
  pin3: ["A"],
  pin4: ["C"],
  pin5: ["B"],
  pin6: ["MT1"],
  pin7: ["MT2"]
} as const

export const PEC11R_4015F_S0024 = (props: ChipProps<typeof pinLabels>) => {
  return (
    <switch
      pinLabels={pinLabels}
      symbol={
        <symbol>
          <schematicrect schX={0} schY={0.2} width={1.2} height={1.2} strokeWidth={0.02} color="#8D2323" />
          <schematicpath points={[{"x":-0.4,"y":0.2},{"x":-0.4,"y":0.4}]} strokeColor="#8D2323" />
          <schematiccircle center={{ x: -0.4, y: 0.2 }} radius={0.02} strokeWidth={0.02} color="#8D2323" />
          <port name="pin5" pinNumber={5} aliases={["B"]} direction="left" schX={-0.8} schY={-0.2} schStemLength={0.4} />
          <port name="pin4" pinNumber={4} aliases={["C"]} direction="left" schX={-0.8} schY={0.2} schStemLength={0.4} />
          <port name="pin3" pinNumber={3} aliases={["A"]} direction="left" schX={-0.8} schY={0.6} schStemLength={0.4} />
          <schematicrect schX={0.39} schY={0.3} width={0.02} height={0.52} strokeWidth={0.02} color="#8D2323" />
          <schematicpath points={[{"x":0.38,"y":0.22},{"x":0.3,"y":0.22},{"x":0.3,"y":0.38},{"x":0.38,"y":0.38}]} strokeColor="#8D2323" />
          <port name="pin1" pinNumber={1} aliases={["1"]} direction="right" schX={0.8} schY={0.6} schStemLength={0.2} />
          <port name="pin2" pinNumber={2} aliases={["2"]} direction="right" schX={0.8} schY={0} schStemLength={0.2} />
          <schematiccircle center={{ x: 0.52, y: 0.08 }} radius={0.04} strokeWidth={0.02} color="#8D2323" />
          <schematiccircle center={{ x: 0.52, y: 0.52 }} radius={0.04} strokeWidth={0.02} color="#8D2323" />
          <schematicpath points={[{"x":0.52,"y":0},{"x":0.52,"y":0.04}]} strokeColor="#8D2323" />
          <schematicpath points={[{"x":0.52,"y":0.56},{"x":0.52,"y":0.6}]} strokeColor="#8D2323" />
          <port name="pin6" pinNumber={6} aliases={["MT1"]} direction="down" schX={-0.2} schY={-0.6} schStemLength={0.2} />
          <port name="pin7" pinNumber={7} aliases={["MT2"]} direction="down" schX={0.2} schY={-0.6} schStemLength={0.2} />
          <schematicpath points={[{"x":-0.4,"y":0.2},{"x":-0.4,"y":0}]} strokeColor="#8D2323" />
          <schematicpath points={[{"x":-0.4,"y":0.4},{"x":-0.34,"y":0.5}]} strokeColor="#8D2323" />
          <schematicpath points={[{"x":-0.4,"y":0.5},{"x":-0.4,"y":0.6}]} strokeColor="#8D2323" />
          <schematicpath points={[{"x":-0.4,"y":-0.2},{"x":-0.4,"y":-0.1}]} strokeColor="#8D2323" />
          <schematicpath points={[{"x":-0.4,"y":0},{"x":-0.34,"y":-0.1}]} strokeColor="#8D2323" />
          <schematicpath svgPath="M -0.04 -0.04 A 0.24 0.24 0 0 1 -0.04 0.44" strokeColor="#840000" />
          <schematicpath svgPath="M 0.11 0.2 A 0.15 0.15 0 1 1 0.109852 0.193352" strokeColor="#840000" />
          <schematicpath points={[{"x":-0.06,"y":0.06},{"x":-0.06,"y":0.34}]} strokeColor="#840000" />
          <schematicpath points={[{"x":-0.04,"y":-0.04},{"x":0,"y":-0.08}]} strokeColor="#840000" />
          <schematicpath points={[{"x":-0.04,"y":-0.04},{"x":0,"y":0}]} strokeColor="#840000" />
          <schematicpath points={[{"x":-0.04,"y":0.06},{"x":-0.04,"y":0.34}]} strokeColor="#840000" />
          <schematicpath points={[{"x":-0.04,"y":0.44},{"x":0,"y":0.4}]} strokeColor="#840000" />
          <schematicpath points={[{"x":-0.02,"y":0.34},{"x":-0.02,"y":0.06}]} strokeColor="#840000" />
          <schematicpath points={[{"x":0,"y":0.48},{"x":-0.04,"y":0.44}]} strokeColor="#840000" />
          <schematicpath points={[{"x":0.6,"y":0.6},{"x":0.52,"y":0.6}]} strokeColor="#8D2323" />
          <schematicpath points={[{"x":0.6,"y":0},{"x":0.52,"y":0}]} strokeColor="#8D2323" />
          <schematictext schX={0} schY={1.02} text="{NAME}" fontSize={0.2} anchor="bottom_center" />
        </symbol>
      }
      supplierPartNumbers={{
  "jlcpcb": [
    "C143789"
  ]
}}
      manufacturerPartNumber="PEC11R-4015F-S0024"
      footprint={<footprint>
        <platedhole  portHints={["pin1"]} pcbX="-2.499868mm" pcbY="7.249922mm" outerDiameter="1.7999964mm" holeDiameter="1.1999976mm" shape="circle" />
<platedhole  portHints={["pin2"]} pcbX="2.500122mm" pcbY="7.249922mm" outerDiameter="1.7999964mm" holeDiameter="1.1999976mm" shape="circle" />
<platedhole  portHints={["pin3"]} pcbX="-2.499868mm" pcbY="-7.249922mm" outerDiameter="1.7999964mm" holeDiameter="1.1999976mm" shape="circle" />
<platedhole  portHints={["pin4"]} pcbX="0mm" pcbY="-7.249922mm" outerDiameter="1.7999964mm" holeDiameter="1.1999976mm" shape="circle" />
<platedhole  portHints={["pin5"]} pcbX="2.500122mm" pcbY="-7.249922mm" outerDiameter="1.7999964mm" holeDiameter="1.1999976mm" shape="circle" />
<platedhole  portHints={["pin6"]} pcbX="-5.700014mm" pcbY="0.249936mm" holeWidth="1.3999972mm" holeHeight="2.7999944mm" outerWidth="2.3999952mm" outerHeight="3.7999924mm" shape="pill" />
<platedhole  portHints={["pin7"]} pcbX="5.700014mm" pcbY="0.249936mm" holeWidth="1.3999972mm" holeHeight="2.7999944mm" outerWidth="2.3999952mm" outerHeight="3.7999924mm" shape="pill" />
<silkscreenpath route={[{"x":1.2699999999998681,"y":-1.0200386000000208},{"x":-1.2699999999999818,"y":-1.0200386000000208}]} />
<silkscreenpath route={[{"x":-6.249898600000051,"y":-6.450075999999967},{"x":-6.249898600000051,"y":-1.9000977999999122}]} />
<silkscreenpath route={[{"x":6.250177999999892,"y":-6.450075999999967},{"x":6.250177999999892,"y":-1.9000977999999122}]} />
<silkscreenpath route={[{"x":-6.249924000000192,"y":2.399969800000008},{"x":-6.249924000000192,"y":6.949948000000063}]} />
<silkscreenpath route={[{"x":6.250152600000092,"y":2.399995200000035},{"x":6.250152600000092,"y":6.94997340000009}]} />
<silkscreenpath route={[{"x":3.700272000000041,"y":6.94997340000009},{"x":6.250432000000046,"y":6.94997340000009}]} />
<silkscreenpath route={[{"x":3.400221800000054,"y":-6.450075999999967},{"x":6.250101800000039,"y":-6.450075999999967}]} />
<silkscreenpath route={[{"x":0.9199879999998757,"y":-6.450075999999967},{"x":1.6200119999999743,"y":-6.450075999999967}]} />
<silkscreenpath route={[{"x":-1.6000222000000122,"y":-6.450075999999967},{"x":-0.8999981999999136,"y":-6.450075999999967}]} />
<silkscreenpath route={[{"x":-6.249924000000192,"y":-6.450075999999967},{"x":-3.3999932000001536,"y":-6.450075999999967}]} />
<silkscreenpath route={[{"x":-1.2749784000001227,"y":6.94997340000009},{"x":1.2749529999998686,"y":6.94997340000009}]} />
<silkscreenpath route={[{"x":-6.249924000000192,"y":6.94997340000009},{"x":-3.699992600000087,"y":6.94997340000009}]} />
<silkscreenpath route={[{"x":1.2699999999998681,"y":-1.0200386000000208},{"x":1.4332817052282962,"y":-0.8324007817582242},{"x":1.5690741964359631,"y":-0.6240038877592724},{"x":1.674773080614159,"y":-0.39884480626278673},{"x":1.7483511364296191,"y":-0.1612419110799692},{"x":1.7883971947339887,"y":0.08424776143419876},{"x":1.7941432036757305,"y":0.3329159127483763},{"x":1.7654789593300393,"y":0.5799932836121116},{"x":1.702954219322578,"y":0.8207411246903575},{"x":1.6077681589119948,"y":1.0505420820433073},{"x":1.4817463717533883,"y":1.2649887543377645},{"x":1.327305856448902,"y":1.4599682233319982},{"x":1.1474086604195008,"y":1.6317409364082778},{"x":0.9455050701687924,"y":1.7770124282458255},{"x":0.725467437505813,"y":1.8929965060708582},{"x":0.49151591088775604,"y":1.9774686866386446},{"x":0.24813749629663562,"y":2.028808860069603},{"x":0,"y":2.046032362281494},{"x":-0.24813749629663562,"y":2.028808860069603},{"x":-0.4915159108879834,"y":1.9774686866386446},{"x":-0.7254674375060404,"y":1.8929965060708582},{"x":-0.9455050701690197,"y":1.7770124282458255},{"x":-1.1474086604195008,"y":1.6317409364082778},{"x":-1.327305856448902,"y":1.4599682233319982},{"x":-1.481746371753502,"y":1.2649887543377645},{"x":-1.6077681589122221,"y":1.0505420820433073},{"x":-1.702954219322578,"y":0.8207411246903575},{"x":-1.765478959330153,"y":0.5799932836121116},{"x":-1.794143203675958,"y":0.3329159127483763},{"x":-1.7883971947341024,"y":0.08424776143419876},{"x":-1.7483511364296191,"y":-0.1612419110799692},{"x":-1.6747730806142727,"y":-0.39884480626278673},{"x":-1.5690741964360768,"y":-0.6240038877592724},{"x":-1.43328170522841,"y":-0.8324007817582242},{"x":-1.2699999999999818,"y":-1.0200386000000208}]} />
<silkscreencircle pcbX="0mm" pcbY="0.249936mm" radius="2.999994mm" />
<silkscreentext text="{NAME}" pcbX="0mm" pcbY="9.210042mm" anchorAlignment="center" fontSize="1mm" />
<fabricationnotepath route={[{"x":-6.299987400000077,"y":6.999986000000035},{"x":6.299987400000077,"y":6.999986000000035},{"x":6.299987400000077,"y":6.699986599999988},{"x":-6.299987400000077,"y":6.699986599999988},{"x":-6.299987400000077,"y":6.999986000000035}]} strokeWidth="0mm" isFilled hasStroke={false} />
<fabricationnotepath route={[{"x":-6.299987400000077,"y":6.699986599999988},{"x":-6.299987400000077,"y":-6.499987000000033},{"x":-5.99998800000003,"y":-6.499987000000033},{"x":-5.99998800000003,"y":6.699986599999988},{"x":-6.299987400000077,"y":6.699986599999988}]} strokeWidth="0mm" isFilled hasStroke={false} />
<fabricationnotepath route={[{"x":6.299987400000077,"y":6.699986599999988},{"x":6.299987400000077,"y":-6.499987000000033},{"x":5.99998800000003,"y":-6.499987000000033},{"x":5.99998800000003,"y":6.699986599999988},{"x":6.299987400000077,"y":6.699986599999988}]} strokeWidth="0mm" isFilled hasStroke={false} />
<fabricationnotepath route={[{"x":-5.99998800000003,"y":-6.499987000000033},{"x":5.99998800000003,"y":-6.499987000000033},{"x":5.99998800000003,"y":-6.199987599999986},{"x":-5.99998800000003,"y":-6.199987599999986},{"x":-5.99998800000003,"y":-6.499987000000033}]} strokeWidth="0mm" isFilled hasStroke={false} />
<fabricationnotepath route={[{"x":-2.099995799999988,"y":0.09999980000009145},{"x":1.9999959999998964,"y":0.09999980000009145},{"x":-2.099995799999988,"y":0.09999980000009145}]} strokeWidth="0mm" isFilled hasStroke={false} />
<courtyardoutline outline={[{"x":-7.150011599999857,"y":8.399920199999997},{"x":7.150011599999857,"y":8.399920199999997},{"x":7.150011599999857,"y":-8.399920199999997},{"x":-7.150011599999857,"y":-8.399920199999997},{"x":-7.150011599999857,"y":8.399920199999997}]} />
      </footprint>}
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C143789.obj?uuid=80dcd71b767c46f0b1face608b10b9c4",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C143789.step?uuid=80dcd71b767c46f0b1face608b10b9c4",
        pcbRotationOffset: 0,
        modelOriginPosition: { x: 0, y: -0.25001270000007025, z: -0.000006999999999646178 },
      }}
      {...props}
    />
  )
}