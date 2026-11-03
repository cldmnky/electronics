import type { ChipProps } from "@tscircuit/props"

const pinLabels = {
  pin1: ["A"],
  pin2: ["C"],
  pin3: ["B"],
  pin4: ["pin4"],
  pin5: ["pin5"],
  pin7: ["pin7"],
  pin8: ["pin8"]
} as const

export const PEC11R_4215F_S0024 = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      supplierPartNumbers={{
  "jlcpcb": [
    "C143790"
  ]
}}
      manufacturerPartNumber="PEC11R-4215F-S0024"
      footprint={<footprint>
        <platedhole  portHints={["pin4"]} pcbX="-2.499995mm" pcbY="7.249922mm" outerDiameter="1.7999964mm" holeDiameter="1.1999976mm" shape="circle" />
<platedhole  portHints={["pin5"]} pcbX="2.499995mm" pcbY="7.249922mm" outerDiameter="1.7999964mm" holeDiameter="1.1999976mm" shape="circle" />
<platedhole  portHints={["pin1"]} pcbX="-2.499995mm" pcbY="-7.249922mm" outerDiameter="1.7999964mm" holeDiameter="1.1999976mm" shape="circle" />
<platedhole  portHints={["pin2"]} pcbX="-0.000127mm" pcbY="-7.249922mm" outerDiameter="1.7999964mm" holeDiameter="1.1999976mm" shape="circle" />
<platedhole  portHints={["pin3"]} pcbX="2.499995mm" pcbY="-7.249922mm" outerDiameter="1.7999964mm" holeDiameter="1.1999976mm" shape="circle" />
<platedhole  portHints={["pin7"]} pcbX="-5.700141mm" pcbY="0.249936mm" holeWidth="1.3999972mm" holeHeight="2.7999944mm" outerWidth="2.3999952mm" outerHeight="3.7999924mm" shape="pill" />
<platedhole  portHints={["pin8"]} pcbX="5.699887mm" pcbY="0.249936mm" holeWidth="1.3999972mm" holeHeight="2.7999944mm" outerWidth="2.3999952mm" outerHeight="3.7999924mm" shape="pill" />
<silkscreenpath route={[{"x":1.269873000000075,"y":-1.0200386000000208},{"x":-1.2701269999997749,"y":-1.0200386000000208}]} />
<silkscreenpath route={[{"x":1.269873000000075,"y":-1.0200386000000208},{"x":1.4331547052285032,"y":-0.8324007817582242},{"x":1.56894719643617,"y":-0.6240038877592724},{"x":1.674646080614366,"y":-0.39884480626278673},{"x":1.748224136429826,"y":-0.1612419110799692},{"x":1.7882701947341957,"y":0.08424776143419876},{"x":1.7940162036759375,"y":0.3329159127483763},{"x":1.7653519593302462,"y":0.5799932836121116},{"x":1.702827219322785,"y":0.8207411246903575},{"x":1.6076411589122017,"y":1.0505420820433073},{"x":1.4816193717535953,"y":1.2649887543377645},{"x":1.327178856449109,"y":1.4599682233319982},{"x":1.1472816604197078,"y":1.6317409364082778},{"x":0.9453780701689993,"y":1.7770124282458255},{"x":0.72534043750602,"y":1.8929965060708582},{"x":0.491388910887963,"y":1.9774686866386446},{"x":0.24801049629684258,"y":2.028808860069603},{"x":-0.00012699999979304266,"y":2.046032362281494},{"x":-0.24826449629642866,"y":2.028808860069603},{"x":-0.49164291088777645,"y":1.9774686866386446},{"x":-0.7255944375058334,"y":1.8929965060708582},{"x":-0.9456320701688128,"y":1.7770124282458255},{"x":-1.147535660419294,"y":1.6317409364082778},{"x":-1.327432856448695,"y":1.4599682233319982},{"x":-1.481873371753295,"y":1.2649887543377645},{"x":-1.6078951589120152,"y":1.0505420820433073},{"x":-1.703081219322371,"y":0.8207411246903575},{"x":-1.765605959329946,"y":0.5799932836121116},{"x":-1.794270203675751,"y":0.3329159127483763},{"x":-1.7885241947338955,"y":0.08424776143419876},{"x":-1.7484781364294122,"y":-0.1612419110799692},{"x":-1.6749000806140657,"y":-0.39884480626278673},{"x":-1.5692011964358699,"y":-0.6240038877592724},{"x":-1.433408705228203,"y":-0.8324007817582242},{"x":-1.2701269999997749,"y":-1.0200386000000208}]} />
<silkscreencircle pcbX="-0.000127mm" pcbY="0.249936mm" radius="2.999994mm" />
<silkscreenrect pcbX="0mm" pcbY="0.249936mm" width="12.500102mm" height="13.400024mm" strokeWidth="0.254mm" />
<silkscreentext text="{NAME}" pcbX="-0.025527mm" pcbY="9.210042mm" anchorAlignment="center" fontSize="1mm" />
<courtyardoutline outline={[{"x":-7.15013859999965,"y":8.399920199999997},{"x":7.149884600000064,"y":8.399920199999997},{"x":7.149884600000064,"y":-8.399920199999997},{"x":-7.15013859999965,"y":-8.399920199999997},{"x":-7.15013859999965,"y":8.399920199999997}]} />
      </footprint>}
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C143790.obj?uuid=fbc634f30cce46f0b7c3bdcee1371719",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C143790.step?uuid=fbc634f30cce46f0b7c3bdcee1371719",
        pcbRotationOffset: 0,
        modelOriginPosition: { x: 0.00012699999979304266, y: -0.25001270000007025, z: -5.600007199999999 },
      }}
      {...props}
    />
  )
}