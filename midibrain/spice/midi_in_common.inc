* MIDI IN current loop and receiver threshold, shared deck.
*
* Loop, Type A: sender 5 V -> sender 220R -> ring (DIN 4) -> this design's
* 220R receiver -> opto LED anode -> cathode -> tip (DIN 5) -> sender 220R ->
* sender output transistor -> ground.
*
* The receiver is behavioural: an inverting, voltage-output stage that switches
* at the datasheet threshold current passed in as {ifhl}, with the design's
* 2.2k pull-up across it. It is NOT a Toshiba TLP2361 SPICE model: it does not
* model propagation delay, CTR, supply current, isolation or temperature.
* The LED is a diode fitted to the datasheet forward voltage at 2 mA.
*
* Parameters set by the including deck: vsrc, ifhl, is_led.
.param rsend = 220

Vsrc src 0 PWL(0 {vsrc} 1m {vsrc} 1.001m 0 2m 0)
Rsend1 src ring {rsend}
Rrecv ring anode 220
Dled anode cathode DLED
Rsend2 cathode tip {rsend}
Vsw tip 0 0.2
V3V3 3v3 0 3.3
Rpu 3v3 vo 2.2k

* Inverting output: high while the loop current is below the threshold.
Bout vo 0 V = 3.3/(1+exp(5e4*(-I(Vsrc)-{ifhl})))

.model DLED D(Is={is_led} N=2.0 Rs=5)

.tran 1u 2m
.meas tran i_active FIND I(Vsrc) AT=0.5m
.meas tran v_active FIND V(vo) AT=0.5m
.meas tran i_idle FIND I(Vsrc) AT=1.5m
.meas tran v_idle FIND V(vo) AT=1.5m
.end
