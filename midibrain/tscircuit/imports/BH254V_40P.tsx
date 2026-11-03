import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["pin1"],
  pin2: ["pin2"],
  pin3: ["pin3"],
  pin4: ["pin4"],
  pin5: ["pin5"],
  pin6: ["pin6"],
  pin7: ["pin7"],
  pin8: ["pin8"],
  pin9: ["pin9"],
  pin10: ["pin10"],
  pin11: ["pin11"],
  pin12: ["pin12"],
  pin13: ["pin13"],
  pin14: ["pin14"],
  pin15: ["pin15"],
  pin16: ["pin16"],
  pin17: ["pin17"],
  pin18: ["pin18"],
  pin19: ["pin19"],
  pin20: ["pin20"],
  pin21: ["pin21"],
  pin22: ["pin22"],
  pin23: ["pin23"],
  pin24: ["pin24"],
  pin25: ["pin25"],
  pin26: ["pin26"],
  pin27: ["pin27"],
  pin28: ["pin28"],
  pin29: ["pin29"],
  pin30: ["pin30"],
  pin31: ["pin31"],
  pin32: ["pin32"],
  pin33: ["pin33"],
  pin34: ["pin34"],
  pin35: ["pin35"],
  pin36: ["pin36"],
  pin37: ["pin37"],
  pin38: ["pin38"],
  pin39: ["pin39"],
  pin40: ["pin40"]
} as const

export const BH254V_40P = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      supplierPartNumbers={{
  "jlcpcb": [
    "C48687632"
  ]
}}
      manufacturerPartNumber="BH254V-40P"
      footprint={<footprint>
        <platedhole  portHints={["pin1"]} pcbX="-24.13mm" pcbY="-0.10749915mm" holeWidth="1.1000232mm" holeHeight="1.1000232mm" outerWidth="1.6999966mm" outerHeight="1.6999966mm" rectPad={true} pcbRotation="0deg" shape="pill" />
<platedhole  portHints={["pin2"]} pcbX="-24.13mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin3"]} pcbX="-21.59mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin4"]} pcbX="-21.59mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin5"]} pcbX="-19.05mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin6"]} pcbX="-19.05mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin7"]} pcbX="-16.51mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin8"]} pcbX="-16.51mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin9"]} pcbX="-13.97mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin10"]} pcbX="-13.97mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin11"]} pcbX="-11.43mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin12"]} pcbX="-11.43mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin13"]} pcbX="-8.89mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin14"]} pcbX="-8.89mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin15"]} pcbX="-6.35mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin16"]} pcbX="-6.35mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin17"]} pcbX="-3.81mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin18"]} pcbX="-3.81mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin19"]} pcbX="-1.27mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin20"]} pcbX="-1.27mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin21"]} pcbX="1.27mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin22"]} pcbX="1.27mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin23"]} pcbX="3.81mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin24"]} pcbX="3.81mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin25"]} pcbX="6.35mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin26"]} pcbX="6.35mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin27"]} pcbX="8.89mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin28"]} pcbX="8.89mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin29"]} pcbX="11.43mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin30"]} pcbX="11.43mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin31"]} pcbX="13.97mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin32"]} pcbX="13.97mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin33"]} pcbX="16.51mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin34"]} pcbX="16.51mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin35"]} pcbX="19.05mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin36"]} pcbX="19.05mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin37"]} pcbX="21.59mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin38"]} pcbX="21.59mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin39"]} pcbX="24.13mm" pcbY="-0.10749915mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<platedhole  portHints={["pin40"]} pcbX="24.13mm" pcbY="2.43250085mm" outerDiameter="1.6999966mm" holeDiameter="1.1000232mm" shape="circle" />
<silkscreenpath route={[{"x":2.548381999999947,"y":-2.647499150000044},{"x":28.456382000000076,"y":-2.647499150000044}]} />
<silkscreenpath route={[{"x":-28.448000000000093,"y":-2.647499150000044},{"x":-2.5654762000001483,"y":-2.647499150000044}]} />
<silkscreenpath route={[{"x":-28.448000000000093,"y":4.97250084999996},{"x":28.447898399999985,"y":4.97250084999996}]} />
<silkscreenpath route={[{"x":-29.19999239999993,"y":5.662466450000011},{"x":29.19989080000005,"y":5.662466450000011}]} />
<silkscreenpath route={[{"x":28.447898399999985,"y":4.97250084999996},{"x":28.447898399999985,"y":-2.647499150000044}]} />
<silkscreenpath route={[{"x":29.199865400000135,"y":-3.3375155500001483},{"x":29.199865400000135,"y":5.662441050000098}]} />
<silkscreenpath route={[{"x":-28.448000000000093,"y":4.97250084999996},{"x":-28.448000000000093,"y":-2.647499150000044}]} />
<silkscreenpath route={[{"x":-29.19999239999993,"y":-3.337490150000008},{"x":-29.19999239999993,"y":5.662466450000011}]} />
<silkscreenpath route={[{"x":-29.19999239999993,"y":-3.337490150000008},{"x":29.19989080000005,"y":-3.337490150000008}]} />
<silkscreenrect pcbX="0mm" pcbY="-2.58399915mm" width="5.08mm" height="1.397mm" strokeWidth="0.254mm" />
<silkscreentext text="{NAME}" pcbX="-0.0254mm" pcbY="6.70910085mm" anchorAlignment="center" fontSize="1mm" />
<fabricationnotepath route={[{"x":-2.550032999999985,"y":-3.337490150000008},{"x":-2.550032999999985,"y":-1.8875057500000594},{"x":2.549956800000018,"y":-1.8875057500000594},{"x":2.549956800000018,"y":-3.337490150000008},{"x":-2.550032999999985,"y":-3.337490150000008}]} strokeWidth="0mm" isFilled hasStroke={false} />
<courtyardoutline outline={[{"x":-29.44999239999993,"y":5.967482850000124},{"x":29.44989080000005,"y":5.967482850000124},{"x":29.44989080000005,"y":-3.442481550000025},{"x":-29.44999239999993,"y":-3.442481550000025},{"x":-29.44999239999993,"y":5.967482850000124}]} />
      </footprint>}
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C48687632.obj?uuid=86e8b92956a14ac7b40b6a1e839e1ff4",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C48687632.step?uuid=86e8b92956a14ac7b40b6a1e839e1ff4",
        pcbRotationOffset: 0,
        modelOriginPosition: { x: 0.00005080000005364127, y: -1.3575006499999924, z: -0.20000640000000036 },
      }}
      {...props}
    />
  )
}