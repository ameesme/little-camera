# Little camera — electrical audit, 3 October 2026

**Verdict: the saved schematic and PCB agree electrically, but the design is not ready for unconditional electrical sign-off.** The display mode connection is outside specification; display shutdown does not remove power; the USB sense divider lacks voltage margin; and the camera core supply needs vendor confirmation or correction. No design files were edited during this audit.

Scope: `little_camera.kicad_sch`, `little_camera.kicad_pcb`, project rules, `gen/design.py`, and the annotated assembly BOM. This is a file and datasheet review, not a measurement of assembled hardware. The older `verification_v3p.md` was treated as background, not proof of circuit suitability. Exact camera and battery identification remain outstanding.

## Checks completed

| Check | Result |
|---|---|
| KiCad schematic ERC | 0 errors, 0 warnings |
| Exported schematic versus saved PCB | All 272 component pin/net assignments match, normalizing deliberate NC nets |
| `design.py` versus exported schematic | 0 differences |
| Saved-board physical continuity, existing `gen/conncheck.py` | 60/60 multi-pad nets connected; 0 broken nets |
| Fresh KiCad DRC, including zone refill in memory | 0 unconnected items; 16 copper-edge errors; 84 warnings |
| KiCad schematic parity | 96 messages, explained below; no functional pin/net mismatch in independent comparison |
| BOM versus declared values | No value mismatches; manual-fit/pad-only omissions are J4, J5, SW1, SW2, SW3 |

The continuity script uses approximate geometry and is supporting evidence, not a replacement for KiCad or a fresh Gerber geometry check. DRC found no reported copper shorts. Final manufacturing files were not regenerated or independently re-audited in this session.

## Findings that need resolution

### 1. High — J3 pin 8 (EXTMODE) is supplied with the wrong voltage

J3/8 is on `PANEL_EN_SW`, supplied by ESP32 GPIO15 through R29. This is approximately 3.3 V. Sharp specifies EXTMODE high at the display supply voltage, 4.8–5.5 V, and recommends connecting it to VDD. The other five signal inputs accept approximately 3 V; EXTMODE is the exception. Connect J3/8 to the correctly controlled display rail, or deliberately select serial inversion mode and implement its protocol. Keep U8's supply at its intended logic voltage. [Sharp specification, terminal table and electrical characteristics](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/1272/LS027B7DH01_Rev_Jun_2010.pdf).

### 2. High — PANEL_EN does not turn display power off

U7 is a TPS61222. EN low stops switching but leaves a conduction path from VBAT, through L1 and the internal rectifier, to VDISP. Therefore J3/6 and J3/7 remain energized below their specified operating voltage while U8 loses its supply. This is not the power-off state claimed in the design comments. Add genuine supply isolation/discharge or use a converter with true output disconnect. Alternatively, explicitly design and validate an always-powered display strategy. Also verify startup/shutdown timing and input levels during transitions. [TI TPS6122x, §10.3.2 and §12.3](https://www.ti.com/lit/ds/symlink/tps61220.pdf).

### 3. High — R8/R9 can drive VBUS_SENSE above the ESP32 input range

R8=100 kΩ and R9=200 kΩ produce `VBUS_SENSE = 2/3 × VBUS`: 3.33 V at 5 V and 3.67 V at 5.5 V. With 1% resistors, the upper value is 3.691 V. The ESP32 input table limits a high input to VDD+0.3 V; at VDD=3.3 V that is 3.6 V. The divider also feeds the pin before the 3.3 V supply finishes rising. The 100 kΩ resistor limits current but does not establish guaranteed tolerance of that condition.

Reduce the ratio and check both VIH at minimum VBUS and the upper input limit at minimum VDD. For example, 100 kΩ/150 kΩ gives nominal 2.64 V at 4.4 V and 3.30 V at 5.5 V, but still requires startup-current assessment. A suitable comparator/buffer avoids dependence on GPIO clamp behavior. [Espressif module datasheet, §6.3](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf).

### 4. High, conditional on exact camera — U6's 1.2 V core rail is below the available OV2640 specification

U6=TLV75512PDQNR supplies J2/10 with 1.2 V. OmniVision's OV2640 v2.2 specifies DVDD=1.24–1.36 V, nominal 1.3 V. Its revision history explicitly changes the older 1.2 V specification to 1.3 V. Even +1.5% regulator accuracy gives only 1.218 V. Obtain the exact module/sensor specification; if that revision applies, use a suitable 1.3 V supply. Do not assume a generic ESP32-CAM circuit proves compliance. AVDD=2.8 V is consistent with this document; nominal DOVDD=3.3 V is at its upper operating limit, so include regulator tolerance in the module assessment. [OmniVision-authored OV2640 datasheet, Table 6 and revision history, hosted by Welectron](https://www.welectron.com/mediafiles/productimg/arducam/OV2640_DS.pdf).

### 5. High until physically confirmed — camera flex direction and module pinout

The schematic uses the reversed 24-pin mapping declared in `design.py`; the PCB implements it exactly. J2/2 and /15 are GND; /3 SIOD; /4 AVDD; /10 DVDD; /11 DOVDD. The earlier report explicitly left the physical flex orientation unverified. A matching netlist cannot establish which contact of the actual folded flex lands on which connector pad. Confirm the module drawing and insertion direction, then continuity-check its ground contacts in the final fold before applying power. Double-sided connector contacts do not resolve a reversed pin order.

### 6. Medium — USB current budget and charging current are not controlled by source capability

R5=3.3 kΩ programs about 303 mA of charging current. The USB path also supplies the ESP32, camera regulators and other loads. Only 5.1 kΩ CC pull-downs are present; there is no CC-current detection or input-current manager. For illustration, a 250 mA system load plus the charger is already above 500 mA. Do not assume that a USB-C connector grants 1.5 A or 3 A. Validate the supported sources and enumeration/current behavior; reduce charging demand or add current management. The substantial VSYS capacitance behind D1 also merits hot-plug/inrush measurement. [TI explanation of Type-C current advertisements](https://www.ti.com/lit/eb/slyy228/slyy228.pdf).

Battery capacity, maximum charge/discharge current, and protection circuitry are not identified. There is no separate battery protection IC or battery disconnect on this board; the charger is not an overdischarge/short-circuit protection system. A protected cell or appropriate board protection is required for that behavior. The older report inconsistently refers to roughly 300 mAh while code mentions 350 mAh. Do not approve 303 mA from either assumption. TP4054 is a 4.2 V charger; verify cell chemistry. R5=5.1 kΩ would give approximately 196 mA if appropriate for the selected cell. [UMW TP4054 datasheet](https://www.umw-ic.com/static/pdf/068a0a67bace2065268aaa277008bccd.pdf).

### 7. Medium — 3.3 V regulation, heating and local decoupling need validation

AP2112K is nominally rated for 600 mA, but its SOT-25 thermal limit depends on layout. At VSYS=4.6 V and 500 mA, dissipation is 0.65 W. Using the datasheet's 184 °C/W reference gives roughly 120 °C temperature rise; that is an estimate, not the actual board's measured temperature. Simultaneous USB charging adds heat nearby elsewhere on the board. On battery, the LDO loses 3.3 V regulation as VBAT approaches 3.3 V plus dropout; define a load-dependent shutdown threshold. [Diodes AP2112 datasheet](https://www.diodes.com/datasheet/download/AP2112.pdf).

C20/C21/C22 are clustered near U10, roughly 23–25 mm from U1's supply pad. The module has internal decoupling, but these external capacitors do not provide a short local supply loop at the module entrance. Move/add local bulk and ceramic capacitance at U1/2 and inspect the ground return. Scope that pad during capture, radio activity, buzzer operation and USB/battery transitions. [Espressif supply guidance](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html).

### 8. Medium — USB routing is connected but signal integrity is unverified

Both connector orientations correctly reach GPIO19 D− and GPIO20 D+. However, the nets use 0.127 mm traces with aggregate lengths 65.66 mm and 63.84 mm, and 3 versus 2 vias, on F.Cu/In2.Cu. Aggregate lengths include connector branches, so these figures are not an exact endpoint skew measurement. There are no differential-pair dimensions in the project and no series tuning resistors or USB ESD devices. Connectivity alone cannot establish impedance or protection.

Review the actual pair spacing, stackup, reference-plane continuity and return vias, then test enumeration and sustained transfers on both orientations. Espressif recommends a 90 Ω differential pair with equal-length parallel routing and few layer transitions. Reserve the suggested series-tuning components; absence alone does not prove USB will fail. [Espressif PCB USB guidance](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/pcb-layout-design.html).

### 9. Medium — oscillator and buzzer components have incomplete operating-margin evidence

R19=1 MΩ/C18=1 µF uses a typical oscillator approximation to claim 1.25 Hz, close to the display's 1 Hz lower bound. The actual BOM uses TECH PUBLIC 74LVC1G14GW, whereas the readily available detailed threshold/leakage specification is from Nexperia. That manufacturer's limits cannot certify the TECH PUBLIC device. Use the exact supplier's limits, including input leakage and capacitor tolerance, or select a characterized part and target several hertz for margin. Measure frequency and low/high levels over the operating range. [Nexperia oscillator example and threshold limits, for comparison](https://assets.nexperia.com/documents/data-sheet/74LVC1G14.pdf).

Q2 is an ElecSuper 2N7002W with about 3.27 V available at its gate. The design notes cite on-resistance at 4.5 V, which does not guarantee on-resistance at 3.3 V. The topology and flyback-diode orientation are correct, but confirm the exact part's 3.3 V performance and buzzer current. A MOSFET explicitly specified at 2.5 V would make this margin easier to establish. GPIO46 is an I/O on ESP32-S3; it was not flagged as input-only based on ESP32-S2 assumptions.

## Connections and selections that check out

- U1: GPIO35–37 are unused for the N16R8 octal-PSRAM variant. BOOT has a 10 kΩ pull-up; CAM_EN/GPIO45 and BUZZ/GPIO46 have pull-down paths. EN has the recommended 10 kΩ/1 µF delay. Battery ADC uses GPIO3/ADC1_CH2 and a 1:1 divider with filter capacitance. Confirm GPIO3's JTAG strap/eFuse configuration at bring-up. [Espressif module datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf).
- Q1/D1: the P-FET source is VSYS, drain VBAT; its body diode points from the battery toward VSYS. D1's anode is VBUS and cathode VSYS. These orientations avoid the direct uncontrolled USB-to-battery diode path. Q1's actual turn-off behavior during slow USB ramps still needs measurement. [AO3401A datasheet](https://www.aosmd.com/pdfs/datasheet/AO3401A.pdf).
- U2: CHRG/GND/BAT/VCC/PROG pin order matches TP4054 despite the MCP73831 symbol name. CHRG LED polarity and the divided MCU status connection are sensible. CHG_STAT's resistive high is about 2.88 V at 5 V; at low VBUS its digital-high margin shrinks. The LED also affects that node, so scope its actual high level.
- U4/U5/U6: DQN assignments OUT=1, GND=2, EN=3, IN=4 and grounded thermal pad match TI. Switched SCCB pull-ups avoid an always-on pull-up supply. Output capacitors are nominally adequate; check effective capacitance after DC bias and prevent MCU outputs from driving the camera while its rails are disabled. TLV755P actively discharges its outputs when disabled. [TI TLV755P datasheet](https://www.ti.com/lit/ds/symlink/tlv755p.pdf).
- U7: fixed-output FB is correctly tied to VOUT; L1=4.7 µH is an appropriate nominal inductance. SW_L is a short, approximately 3 mm, front-layer route. Confirm inductor saturation current and output-capacitor effectiveness with the actual selected parts.
- J3: all ten named functions follow the Sharp terminal table. R22–R25 provide series resistance; check input edges with the actual flex capacitance. The EXTMODE voltage and shutdown issues above remain.
- BZ1/Q2/D4: low-side drive and flyback clamp have the correct circuit polarity. The third buzzer pad and connector mechanical pads are intentionally unnetted. SW1's logical pull-up/filter connection is consistent, conditional on the custom footprint's middle terminal being NO. SW1 is manual-fit; its mechanical contact mapping still warrants a real-part check.

## DRC and synchronization details

All 16 copper-edge errors are J1 pads at 0.210 mm versus the global 0.300 mm minimum. The existing 0.200 mm J1 custom rule does not clear them in KiCad 10.0.5. In a temporary copy, lowering the global minimum to 0.200 mm and adding a default custom 0.300 mm edge rule before the existing J1 exception removes all 16 errors. The production project was left unchanged. This verifies rule behavior, not the board manufacturer's acceptance of that cutout. [KiCad constraint documentation](https://docs.kicad.org/10.0/en/pcbnew/pcbnew.html).

The 84 warnings comprise 60 silk-over-copper, 7 silk-edge, 10 text-height, 2 silk-overlap and 5 modified-library-footprint warnings (U1, U8, Q2, D4, BZ1). Confirm the local pad geometry is intentional before updating from a library. Do not blindly replace these footprints.

The 96 parity messages comprise 80 Description-field differences, 5 missing PWR_FLAG footprints, and 11 intentionally NC pins whose generated NC net names are absent on the PCB. Independent pin/net comparison confirms these do not conceal a functional net change. Clean up metadata/board-exclusion flags and NC synchronization for a maintainable project.

The antenna has an all-copper-layer keepout; no keepout violation was reported. This is not an RF sign-off for the assembled camera: display, battery and enclosure proximity require a range/throughput test.

## Bring-up checks after corrections

1. Confirm exact camera contact order and battery protection/ratings. Keep the camera disconnected during initial power checks.
2. Apply a current-limited supply and measure VSYS, +3V3, AVDD, DOVDD, DVDD and VDISP. Check all enabled and disabled states, including USB only and battery only.
3. Scope VBUS_SENSE, CHG_STAT and U1/2 during insertion/removal and supply ramps; verify charger behavior and temperatures with the actual battery.
4. Verify display mode voltage, inversion frequency, waveform levels, reset/power sequencing and genuine power-off behavior.
5. Verify camera reset/power sequencing, XCLK and image capture; test USB both orientations and the intended worst-case concurrent loads.

Evidence: `audit_2026-10-03_evidence.txt`, `audit_2026-10-03_netlist.xml`, `audit_2026-10-03_drc_cli.json`, `audit_2026-10-03_drc.rpt`, and `audit_2026-10-03_verify.py`. File hashes are recorded in the evidence file. These findings apply to those saved files, not unsaved GUI edits.
