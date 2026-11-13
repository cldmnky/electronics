import type { ChipProps } from "@tscircuit/props"

// Toshiba TLP2361(TPL,E, LCSC/JLCPCB C107626. Primary source: Toshiba TLP2361
// datasheet Rev.5.0 (2015-11-02), pages 2-5 and 15:
//   * package 11-4L1S (SO6), pin 1 = Anode, 3 = Cathode, 4 = GND,
//     5 = VO (output), 6 = VCC; 1.27 mm lead pitch, 2.54 mm row pitch;
//   * package height 2.1 +-0.1 mm, so 2.2 mm maximum installed height;
//   * inverting totem-pole output (not open collector);
//   * threshold input current IFHL: 1.3 mA max at Ta <= 105 C, 1.6 mA max at
//     Ta <= 125 C, measured at VCC = 2.7-5.5 V;
//   * input forward voltage VF = 1.35/1.50/1.65 V min/typ/max at IF = 2 mA,
//     Ta = 25 C, temperature coefficient -2.0 mV/C (typ);
//   * recommended IF(ON) 2-6 mA, absolute maximum continuous IF 10 mA,
//     input power dissipation 20 mW (-0.5 mW/C above 110 C);
//   * VOH >= 3.2 V at IO = -20 uA, VCC = 3.3 V (2.3 V only at 3.2 mA load);
//     VOL <= 0.1 V at IO = 20 uA and <= 0.4 V at IO = 3.2 mA;
//   * tpHL/tpLH 80 ns max, pulse width distortion 25 ns max at IF = 2 mA;
//   * 0.1 uF bypass between pin 6 and pin 4, placed within 1 cm of each pin.
// The pad geometry is the same 11-4L1S land pattern already verified for the
// TLP2362 in this project (identical package and pinout); no CAD model is
// attached because the TLP2362 model must not follow this part.
const pinLabels = {
  pin1: ["pin1"],
  pin3: ["pin3"],
  pin4: ["pin4"],
  pin5: ["pin5"],
  pin6: ["pin6"]
} as const

export const TLP2361_TPL_E = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      symbol={
        <symbol>
          <schematicrect schX={0} schY={0} width={1.6} height={1.2} strokeWidth={0.02} color="#880000" />
          <port name="pin1" pinNumber={1} aliases={["1"]} direction="left" schX={-1.2} schY={0.2} schStemLength={0.4} />
          <port name="pin3" pinNumber={3} aliases={["3"]} direction="left" schX={-1.2} schY={-0.2} schStemLength={0.4} />
          <port name="pin4" pinNumber={4} aliases={["4"]} direction="right" schX={1.2} schY={-0.4} schStemLength={0.4} />
          <port name="pin5" pinNumber={5} aliases={["5"]} direction="right" schX={1.2} schY={0} schStemLength={0.4} />
          <schematicpath points={[{"x":0.8,"y":0},{"x":0.6,"y":-0.14}]} strokeColor="#880000" />
          <schematicpath points={[{"x":0.6,"y":-0.26},{"x":0.8,"y":-0.4}]} strokeColor="#880000" />
          <schematicpath points={[{"x":0.6,"y":-0.02},{"x":0.6,"y":-0.38}]} strokeColor="#880000" />
          <schematicpath points={[{"x":0.8,"y":-0.4},{"x":0.74,"y":-0.3},{"x":0.68,"y":-0.38},{"x":0.8,"y":-0.4}]} strokeColor="#880000" isFilled fillColor="#880000" />
          <schematicpath svgPath="M -0.24 -0.16 L -0.28 -0.08 L -0.32 -0.12 Z" strokeColor="#880000" isFilled fillColor="#880000" />
          <schematicpath svgPath="M -0.18 -0.08 L -0.22 0 L -0.26 -0.04 Z" strokeColor="#880000" isFilled fillColor="#880000" />
          <schematicpath points={[{"x":-0.38,"y":-0.02},{"x":-0.24,"y":-0.16}]} strokeColor="#880000" />
          <schematicpath points={[{"x":-0.32,"y":0.06},{"x":-0.18,"y":-0.08}]} strokeColor="#880000" />
          <schematicpath points={[{"x":-0.38,"y":-0.1},{"x":-0.7,"y":-0.1}]} strokeColor="#880000" />
          <schematicpath svgPath="M -0.66 0.1 L -0.54 -0.1 L -0.4 0.1 Z" strokeColor="#880000" isFilled fillColor="#880000" />
          <schematicpath points={[{"x":-0.8,"y":0.2},{"x":-0.54,"y":0.2},{"x":-0.54,"y":-0.2},{"x":-0.8,"y":-0.2}]} strokeColor="#880000" />
          <port name="pin6" pinNumber={6} aliases={["6"]} direction="right" schX={1.2} schY={0.4} schStemLength={0.4} />
          <schematicpath points={[{"x":0.48,"y":-0.2},{"x":0.2,"y":0.12},{"x":0.2,"y":-0.48},{"x":0.48,"y":-0.2}]} strokeColor="#880000" />
          <schematicpath points={[{"x":-0.08,"y":-0.1},{"x":0.08,"y":-0.1}]} strokeColor="#880000" />
          <schematicpath svgPath="M 0.08 -0.24 L 0 -0.1 L -0.08 -0.24 Z" strokeColor="#880000" isFilled fillColor="#880000" />
          <schematicpath points={[{"x":0,"y":-0.1},{"x":0,"y":0},{"x":0.2,"y":0}]} strokeColor="#880000" />
          <schematicpath points={[{"x":0,"y":-0.08},{"x":0,"y":-0.4},{"x":0.2,"y":-0.4}]} strokeColor="#880000" />
          <schematicpath points={[{"x":0.48,"y":-0.2},{"x":0.6,"y":-0.2}]} strokeColor="#880000" />
          <schematicpath points={[{"x":0.34,"y":-0.34},{"x":0.34,"y":-0.42},{"x":0.8,"y":-0.42}]} strokeColor="#880000" />
          <schematicpath points={[{"x":0.8,"y":0.4},{"x":0.34,"y":0.4},{"x":0.34,"y":-0.04}]} strokeColor="#880000" />
          <schematictext schX={0} schY={0.8} text="{NAME}" fontSize={0.2} anchor="bottom_center" />
          {/* Part identity is printed literally: this symbol belongs to the
              TLP2361 only, so the sheet cannot hide which opto is fitted. */}
          <schematictext schX={0} schY={-0.95} text="TLP2361" fontSize={0.16} anchor="top_center" />
        </symbol>
      }
      supplierPartNumbers={{
        jlcpcb: ["C107626"],
      }}
      manufacturerPartNumber="TLP2361(TPL,E"

      /* 3D model: tscircuit model CDN copy of the JLCPCB/EasyEDA model for
         C107626, fetched 2026-11-13 (HTTP 200, 1321216 bytes). Verified against this
         footprint before use: the STEP bounding box is 7.000 x 3.837 x 2.469 mm, which matches the
         pad geometry here (pads at x +-3.15 on 1.27 pitch), z runs 0..2.469 so the body sits ON the board,
         and the axes already agree with the footprint's - hence no
         pcbRotationOffset and no modelOriginPosition fudge. */
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C107626.obj",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C107626.step",
        pcbRotationOffset: 0,
        modelOriginPosition: { x: 0, y: 0, z: 0 },
      }}
      footprint={<footprint>
        <smtpad portHints={["pin6"]} pcbX="-3.149854mm" pcbY="-1.27mm" width="1.999996mm" height="0.6500114mm" radius="0.3250057mm" shape="pill" />
<smtpad portHints={["pin3"]} pcbX="3.175mm" pcbY="1.27mm" width="1.999996mm" height="0.6500114mm" radius="0.3250057mm" shape="pill" />
<smtpad portHints={["pin1"]} pcbX="3.149854mm" pcbY="-1.27mm" width="1.999996mm" height="0.6500114mm" radius="0.3250057mm" shape="pill" />
<smtpad portHints={["pin4"]} pcbX="-3.149854mm" pcbY="1.27mm" width="1.999996mm" height="0.6500114mm" radius="0.3250057mm" shape="pill" />
<smtpad portHints={["pin5"]} pcbX="-3.175mm" pcbY="0mm" width="1.999996mm" height="0.6500114mm" radius="0.3250057mm" shape="pill" />
<silkscreenpath route={[{"x":-2.299995399999716,"y":-1.797811999999908},{"x":-2.299995399999716,"y":-1.899869200000012}]} />
<silkscreenpath route={[{"x":-2.299995399999716,"y":-0.518642600000021},{"x":-2.299995399999716,"y":-0.7421879999999419}]} />
<silkscreenpath route={[{"x":-2.299995399999716,"y":0.7421879999999419},{"x":-2.299995399999716,"y":0.5188966000000619}]} />
<silkscreenpath route={[{"x":-2.299995399999716,"y":1.900072399999999},{"x":2.2999700000002576,"y":1.900072399999999}]} />
<silkscreenpath route={[{"x":-2.299995399999716,"y":-1.899869200000012},{"x":-0.508025399999724,"y":-1.899869200000012}]} />
<silkscreenpath route={[{"x":2.2999700000002576,"y":-1.899869200000012},{"x":0.5079746000002388,"y":-1.899869200000012}]} />
<silkscreenpath route={[{"x":-2.299995399999716,"y":-1.899869200000012},{"x":-2.299995399999716,"y":-1.797685000000115}]} />
<silkscreenpath route={[{"x":-2.299995399999716,"y":1.7978881999999885},{"x":-2.299995399999716,"y":1.900072399999999}]} />
<silkscreenpath route={[{"x":2.2999700000002576,"y":-1.899869200000012},{"x":2.2999700000002576,"y":-1.797685000000115}]} />
<silkscreenpath route={[{"x":2.2999700000002576,"y":-0.7421118000000888},{"x":2.2999700000002576,"y":0.7421371999998883}]} />
<silkscreenpath route={[{"x":2.2999700000002576,"y":1.7980659999999489},{"x":2.2999700000002576,"y":1.900072399999999}]} />
<silkscreenpath route={[{"x":3.048000000000229,"y":-2.285898399999951},{"x":3.1967410559977907,"y":-2.1352563702960197},{"x":3.0467300000001387,"y":-1.9858789759853153},{"x":2.8967189440024868,"y":-2.135256370296247},{"x":3.0454600000002756,"y":-2.285898399999951}]} />
<silkscreenpath route={[{"x":-0.5079999999998108,"y":-1.899869200000012},{"x":0.5077460000002247,"y":-1.8998183999999583}]} />
<silkscreentext text="{NAME}" pcbX="0.11684mm" pcbY="2.893316mm" anchorAlignment="center" fontSize="1mm" />
<courtyardoutline outline={[{"x":-4.42499799999996,"y":2.10011059999988},{"x":4.424998000000073,"y":2.10011059999988},{"x":4.424998000000073,"y":-2.0998820000000933},{"x":-4.42499799999996,"y":-2.0998820000000933},{"x":-4.42499799999996,"y":2.10011059999988}]} />
      </footprint>}
      {...props}
    />
  )
}
