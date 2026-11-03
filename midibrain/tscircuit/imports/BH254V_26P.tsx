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
  pin26: ["pin26"]
} as const

export const BH254V_26P = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      supplierPartNumbers={{
  "jlcpcb": [
    "C48687629"
  ]
}}
      manufacturerPartNumber="BH254V-26P"
      footprint={<footprint insertionDirection="from_below">
        <platedhole  portHints={["pin1"]} pcbX="-15.24mm" pcbY="-1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin2"]} pcbX="-15.24mm" pcbY="1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin3"]} pcbX="-12.7mm" pcbY="-1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin4"]} pcbX="-12.7mm" pcbY="1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin5"]} pcbX="-10.16mm" pcbY="-1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin6"]} pcbX="-10.16mm" pcbY="1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin7"]} pcbX="-7.62mm" pcbY="-1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin8"]} pcbX="-7.62mm" pcbY="1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin9"]} pcbX="-5.08mm" pcbY="-1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin10"]} pcbX="-5.08mm" pcbY="1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin11"]} pcbX="-2.54mm" pcbY="-1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin12"]} pcbX="-2.54mm" pcbY="1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin13"]} pcbX="0mm" pcbY="-1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin14"]} pcbX="0mm" pcbY="1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin15"]} pcbX="2.54mm" pcbY="-1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin16"]} pcbX="2.54mm" pcbY="1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin17"]} pcbX="5.08mm" pcbY="-1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin18"]} pcbX="5.08mm" pcbY="1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin19"]} pcbX="7.62mm" pcbY="-1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin20"]} pcbX="7.62mm" pcbY="1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin21"]} pcbX="10.16mm" pcbY="-1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin22"]} pcbX="10.16mm" pcbY="1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin23"]} pcbX="12.7mm" pcbY="-1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin24"]} pcbX="12.7mm" pcbY="1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin25"]} pcbX="15.24mm" pcbY="-1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<platedhole  portHints={["pin26"]} pcbX="15.24mm" pcbY="1.27mm" outerDiameter="1.5999968mm" holeDiameter="1.0200132mm" shape="circle" />
<silkscreenpath route={[{"x":-2.0000214000001506,"y":-2.999994000000015},{"x":-2.0000214000001506,"y":-4.450003799999877}]} />
<silkscreenpath route={[{"x":-2.0000214000001506,"y":-2.999994000000015},{"x":1.9999705999998696,"y":-2.999994000000015},{"x":1.9999705999998696,"y":-4.450003799999877}]} />
<silkscreenpath route={[{"x":20.33493519999979,"y":4.449978400000077},{"x":20.33493519999979,"y":-4.450003799999877}]} />
<silkscreenpath route={[{"x":-20.334986000000185,"y":4.449978400000077},{"x":20.33493519999979,"y":4.449978400000077}]} />
<silkscreenpath route={[{"x":-20.334986000000185,"y":-4.450003799999877},{"x":-20.334986000000185,"y":4.449978400000077}]} />
<silkscreenpath route={[{"x":20.33493519999979,"y":-4.450003799999877},{"x":-20.334986000000185,"y":-4.450003799999877}]} />
<silkscreentext text="{NAME}" pcbX="-0.0254mm" pcbY="5.445mm" anchorAlignment="center" fontSize="1mm" />
<fabricationnotepath route={[{"x":-2.0000214000001506,"y":-4.45000379999999},{"x":1.9999705999998696,"y":-4.45000379999999},{"x":1.9999705999998696,"y":-2.999994000000015},{"x":-2.0000214000001506,"y":-2.999994000000015},{"x":-2.0000214000001506,"y":-4.45000379999999}]} strokeWidth="0mm" isFilled hasStroke={false} />
<courtyardoutline outline={[{"x":-20.62080000000003,"y":4.694999999999936},{"x":20.569999999999936,"y":4.694999999999936},{"x":20.569999999999936,"y":-4.745799999999917},{"x":-20.62080000000003,"y":-4.745799999999917},{"x":-20.62080000000003,"y":4.694999999999936}]} />
      </footprint>}
      
      {...props}
    />
  )
}