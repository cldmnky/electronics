// Froemix base board - hand-maintained tscircuit source (see froemix/AGENTS.md).
// Ported once from the KiCad project in ../tmp by froemix/tools/kicad2tscircuit.py;
// that tool is retired as an editor, edit this file directly.

// One <net> per KiCad net. KiCad names are not valid tscircuit net
// names (only [A-Za-z0-9_]), the original name is kept in a comment.
//
// Width policy: board minTraceWidth is 0.25mm (no copper below that - a
// deliberate margin over the 0.15mm signal minimum, see index.circuit.tsx), so
// every signal net carries a 0.25mm nominal width and power/GND/reset nets are
// 0.3-0.6mm. Per-net nominal widths are preferences: where a channel is tight
// the router falls back to the board minimum, which is exactly why the floor is
// 0.25mm rather than the fab minimum.

export const Nets = () => (
  <>
    {/* -10V_REF */}<net name="N10V_REF" isPowerNet nominalTraceWidth="0.3mm" routingPhaseIndex={0} />
    {/* -12V */}<net name="N12V" isPowerNet nominalTraceWidth="0.5mm" routingPhaseIndex={0} />
    {/* +12V */}<net name="P12V" isPowerNet nominalTraceWidth="0.5mm" routingPhaseIndex={0} />
    {/* +3V3_A */}<net name="P3V3_A" isPowerNet nominalTraceWidth="0.4mm" routingPhaseIndex={0} />
    {/* +3V3_D */}<net name="P3V3_D" isPowerNet nominalTraceWidth="0.4mm" routingPhaseIndex={0} />
    {/* +5V */}<net name="P5V" isPowerNet nominalTraceWidth="0.5mm" routingPhaseIndex={0} />
    {/* VCC */}<net name="VCC" isPowerNet nominalTraceWidth="0.4mm" routingPhaseIndex={0} />
    {/* VEE */}<net name="VEE" isPowerNet nominalTraceWidth="0.4mm" routingPhaseIndex={0} />
    {/* AUDIO_IN_L */}<net name="AUDIO_IN_L" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* AUDIO_IN_R */}<net name="AUDIO_IN_R" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* AUDIO_OUT_L */}<net name="AUDIO_OUT_L" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* AUDIO_OUT_R */}<net name="AUDIO_OUT_R" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* BICK */}<net name="BICK" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* CV_IN_1 */}<net name="CV_IN_1" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* CV_IN_2 */}<net name="CV_IN_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* CV_OUT_1 */}<net name="CV_OUT_1" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* CV_OUT_2 */}<net name="CV_OUT_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* GND */}<net name="GND" isGroundNet nominalTraceWidth="0.3mm" routingPhaseIndex={0} />
    {/* J_AUDIO_IN_L */}<net name="J_AUDIO_IN_L" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* J_AUDIO_IN_R */}<net name="J_AUDIO_IN_R" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* J_AUDIO_OUT_L */}<net name="J_AUDIO_OUT_L" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* J_AUDIO_OUT_R */}<net name="J_AUDIO_OUT_R" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* J_CV_IN_1 */}<net name="J_CV_IN_1" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* J_CV_IN_2 */}<net name="J_CV_IN_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* J_CV_OUT_1 */}<net name="J_CV_OUT_1" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* J_CV_OUT_2 */}<net name="J_CV_OUT_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* J_MIDI_IN_RING */}<net name="J_MIDI_IN_RING" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* J_MIDI_IN_TIP */}<net name="J_MIDI_IN_TIP" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* J_MIDI_OUT_RING */}<net name="J_MIDI_OUT_RING" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* J_MIDI_OUT_TIP */}<net name="J_MIDI_OUT_TIP" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* J_SYNC_IN */}<net name="J_SYNC_IN" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* J_SYNC_OUT */}<net name="J_SYNC_OUT" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* LED */}<net name="LED" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* LRCK */}<net name="LRCK" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* MCLK */}<net name="MCLK" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* MULTIPLEX_ADC_1 */}<net name="MULTIPLEX_ADC_1" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* MULTIPLEX_S0 */}<net name="MULTIPLEX_S0" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* MULTIPLEX_S1 */}<net name="MULTIPLEX_S1" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* MULTIPLEX_S2 */}<net name="MULTIPLEX_S2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* M_ADC_0 */}<net name="M_ADC_0" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* M_ADC_1 */}<net name="M_ADC_1" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* M_ADC_2 */}<net name="M_ADC_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* M_ADC_3 */}<net name="M_ADC_3" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* M_ADC_4 */}<net name="M_ADC_4" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* M_ADC_5 */}<net name="M_ADC_5" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* M_ADC_6 */}<net name="M_ADC_6" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* M_ADC_7 */}<net name="M_ADC_7" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(C12-Pad2) */}<net name="NET_C12_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(C13-Pad2) */}<net name="NET_C13_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(C2-Pad2) */}<net name="NET_C2_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(C3-Pad2) */}<net name="NET_C3_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(C6-Pad2) */}<net name="NET_C6_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(C7-Pad2) */}<net name="NET_C7_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(C8-Pad2) */}<net name="NET_C8_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(C9-Pad2) */}<net name="NET_C9_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(D1-A) */}<net name="NET_D1_A" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(D1-K) */}<net name="NET_D1_K" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(J1-Pad2) */}<net name="NET_J1_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(Q1-Pad1) */}<net name="NET_Q1_1" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(Q1-Pad3) */}<net name="NET_Q1_3" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(R24-Pad2) */}<net name="NET_R24_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(R28-Pad2) */}<net name="NET_R28_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(R35-Pad2) */}<net name="NET_R35_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(R36-Pad2) */}<net name="NET_R36_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(R39-Pad2) */}<net name="NET_R39_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(R40-Pad2) */}<net name="NET_R40_2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* OLED_CS */}<net name="OLED_CS" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* OLED_DC */}<net name="OLED_DC" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* OLED_RES */}<net name="OLED_RES" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* OLED_SCL */}<net name="OLED_SCL" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* OLED_SDA */}<net name="OLED_SDA" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* SCL */}<net name="SCL" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* SDA */}<net name="SDA" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* SDI */}<net name="SDI" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* SDO */}<net name="SDO" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* SD_CLK */}<net name="SD_CLK" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* SD_CMD */}<net name="SD_CMD" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* SD_DAT0 */}<net name="SD_DAT0" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* SD_DAT1 */}<net name="SD_DAT1" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* SD_DAT2 */}<net name="SD_DAT2" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* SD_DAT3 */}<net name="SD_DAT3" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* SYNC_IN */}<net name="SYNC_IN" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* SYNC_OUT */}<net name="SYNC_OUT" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U11A--) */}<net name="U11A_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U11B--) */}<net name="U11B_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U11C--) */}<net name="U11C_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U11D--) */}<net name="U11D_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U12A--) */}<net name="U12A_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U12B--) */}<net name="U12B_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U12C--) */}<net name="U12C_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U12D--) */}<net name="U12D_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U13--INA) */}<net name="U13_INA_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U13--INB) */}<net name="U13_INB_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U16--VIN) */}<net name="U16_VIN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U16-+VIN) */}<net name="U16_VIN_P" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U2A--) */}<net name="U2A_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U2B--) */}<net name="U2B_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U2C--) */}<net name="U2C_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U2D--) */}<net name="U2D_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U3A--) */}<net name="U3A_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U3B--) */}<net name="U3B_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U3C--) */}<net name="U3C_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* Net-(U3D--) */}<net name="U3D_IN_N" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* USART1_RX */}<net name="USART1_RX" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
    {/* USART1_TX */}<net name="USART1_TX" nominalTraceWidth="0.25mm" routingPhaseIndex={0} />
  </>
)
