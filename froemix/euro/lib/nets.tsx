// Froemix euro board (front board) - the nets that leave this board.
//
// Every net here also exists on the base board under the same name; the base
// board is the electrical source of truth (froemix/tscircuit/sections/nets.tsx).
// tscircuit net names allow only [A-Za-z0-9_], which is why the KiCad names
// (+3V3_A, -12V, ...) appear as P3V3_A, N12V, ...

export const Nets = () => (
  <>
    {/* rails: wider than signal, 2 layers on this board */}
    <net name="GND" isGroundNet nominalTraceWidth="0.5mm" routingPhaseIndex={0} />
    <net name="P3V3_A" isPowerNet nominalTraceWidth="0.4mm" routingPhaseIndex={0} />
    <net name="P3V3_D" isPowerNet nominalTraceWidth="0.4mm" routingPhaseIndex={0} />

    {/* jack bus - CN2 */}
    <net name="J_AUDIO_IN_L" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="J_AUDIO_IN_R" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="J_AUDIO_OUT_L" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="J_AUDIO_OUT_R" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="J_SYNC_IN" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="J_SYNC_OUT" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="J_CV_IN_1" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="J_CV_IN_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="J_CV_OUT_1" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="J_CV_OUT_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />

    {/* MIDI TRS - CN1 pins 1-4 */}
    <net name="J_MIDI_IN_TIP" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="J_MIDI_IN_RING" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="J_MIDI_OUT_TIP" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="J_MIDI_OUT_RING" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />

    {/* muxed pot wipers - CN1 pins 5-12 */}
    <net name="M_ADC_0" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="M_ADC_1" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="M_ADC_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="M_ADC_3" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="M_ADC_4" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="M_ADC_5" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="M_ADC_6" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="M_ADC_7" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />


    {/* --- UI controls on the two freed mux channels --- */}
    {/* joystick ladder: each direction pin has its own resistor to GND */}
    <net name="JOY_UP" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="JOY_DOWN" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="JOY_LEFT" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="JOY_RIGHT" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* illuminated button: 1k -> button LED -> M_ADC_7 -> switch -> GND */}
    <net name="BTN_LED" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />

    {/* --- CV-output activity LEDs: jack net -> 2.2k -> diode -> LED -> GND --- */}
    {/* _D is the node at the diode anode, _K the node at the LED anode */}
    <net name="CV1_D" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="CV1_K" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="CV2_D" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="CV2_K" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />

    {/* display (CN4) + panel indicator (CN1 pin 13) */}
    <net name="OLED_CS" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="OLED_SCL" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="OLED_DC" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="OLED_SDA" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="OLED_RES" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    <net name="LED" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* LED anode node: CN1 pin 13 -> R -> LED -> GND */}
    <net name="LED_ANODE" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
  </>
)
