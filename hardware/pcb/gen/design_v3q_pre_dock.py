# -*- coding: utf-8 -*-
"""little camera v3 (small PCB) - single source of truth for parts and every connection.

CONN maps  refdes -> { pin_number : net_name }.
Nothing is implicit: every pin of every part appears here, including NC pins
(net None) so the audit can prove nothing was forgotten.
"""

R_ = "Device:R"; C_ = "Device:C"; L_ = "Device:L"
SOT235 = "Package_TO_SOT_SMD:SOT-23-5"
R0402 = "Resistor_SMD:R_0402_1005Metric"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"

PARTS = {}   # ref -> dict(sym, val, fp, block, pos)
CONN  = {}   # ref -> {pin: net}

def part(ref, sym, val, fp, block, x, y, rot=0, conn=None, note=""):
    PARTS[ref] = dict(sym=sym, val=val, fp=fp, block=block, x=x, y=y, rot=rot, note=note)
    CONN[ref] = conn or {}

# ============================================================ MCU ==========
part("U1", "lc:ESP32-S3-WROOM-1", "ESP32-S3-WROOM-1-N16R8",
     "lc:ESP32-S3-WROOM-1", "MCU", 150, 110, conn={
    1:"GND", 2:"+3V3", 3:"MCU_EN",
    # v3g GPIO map: pins 4-12 face the lane between U1 and the cell. Everything bound for the bottom
    # edge (panel, USB side) sits low on that side, the camera bus is ordered to match J2 left to right.
    4:"LCD_SCLK", 5:"LCD_SI", 6:"LCD_SCS", 7:"LCD_DISP", 8:None,          # v3q: IO15 free (panel always on)
    9:"VBUS_SENSE", 10:"CHG_STAT", 11:"CAM_SIOD", 12:"CAM_SIOC",
    13:"USB_DM", 14:"USB_DP", 15:"VBAT_ADC", 16:"BUZZ",
    17:"CAM_RESET", 18:"CAM_VSYNC", 19:"CAM_PWDN", 20:"CAM_HREF",
    21:"CAM_D7", 22:"CAM_XCLK", 23:"CAM_D6", 24:"CAM_D5", 25:"CAM_PCLK",
    26:"CAM_EN", 27:"BOOT",
    28:None, 29:None, 30:None,                       # IO35/36/37 = octal PSRAM
    31:"CAM_D4", 32:"CAM_D0", 33:"CAM_D3", 34:"CAM_D1", 35:"CAM_D2",
    36:"SHUTTER", 37:"SPARE_1",
    38:"SCL_SYS", 39:"SDA_SYS", 40:"GND", 41:"GND"},
    note="IO35-37 consumed by octal PSRAM. IO45=CAM_EN and IO46=BUZZ are strapping "
         "pins that must read LOW at reset - both have external pulldowns. "
         "IO3=VBAT_ADC (ADC1_CH2) - it selects the JTAG source only if the "
         "STRAP_JTAG_SEL eFuse is burnt, which it is not by default.")

part("C20", C_, "100nF", C0402, "MCU", 178, 96, conn={1:"+3V3", 2:"GND"},
     note="v3q: moved from U10 to the module supply entry (U1 pin 2), just outside the antenna keep-out")
part("C21", C_, "10uF",  C0603, "MCU", 184, 96, conn={1:"+3V3", 2:"GND"},
     note="v3q: bulk at U1 pin 2 (was 23 mm away next to U10); U10 keeps C5 10uF + C22 1uF")
part("C22", C_, "1uF",   C0402, "MCU", 190, 96, conn={1:"+3V3", 2:"GND"})
part("R20", R_, "10k",   R0402, "MCU", 118, 88, conn={1:"+3V3", 2:"MCU_EN"})
part("C23", C_, "1uF",   C0402, "MCU", 124, 96, conn={1:"MCU_EN", 2:"GND"},
     note="EN RC delay, ~10ms with R20")
part("SW3", "Switch:SW_Push", "RESET", "lc:SW_Pads_Short",
     "MCU", 112, 96, conn={1:"MCU_EN", 2:"GND"})
part("SW2", "Switch:SW_Push", "BOOT", "lc:SW_Pads_Short",
     "MCU", 112, 108, conn={1:"BOOT", 2:"GND"},
     note="v3: BOOT and RESET are tweezer pads instead of TS-1088 buttons (space). The ESP32-S3 flashes over "
          "native USB without BOOT; the pads are only for recovery.")
part("R21", R_, "10k", R0402, "MCU", 118, 100, conn={1:"+3V3", 2:"BOOT"})

# ============================================================ USB ==========
part("J1", "Connector:USB_C_Receptacle_USB2.0_16P", "USB-C",
     "lc:USB_C_ShouHan_TYPE-C-16P-CB0.8-073", "USB", 60, 40, conn={
    "A1":"GND","A4":"VBUS","A5":"CC1","A6":"USB_DP","A7":"USB_DM","A9":"VBUS","A12":"GND",
    "B1":"GND","B4":"VBUS","B5":"CC2","B6":"USB_DP","B7":"USB_DM","B9":"VBUS","B12":"GND",
    "A8":None, "B8":None, "SH":"GND"},
    note="USB2.0-only 16P, SHOU HAN TYPE-C 16P CB0.8 073 (C2906289): mid-mount (sink) receptacle for 0.8 mm "
         "boards, body sits in a 9.24 mm board cutout so it stands only 2.36 mm above the board. The shell front is "
         "flush with the outside of the housing (lens y -2.1); the board has a small tab under the front slots. "
         "The four shell legs are plated slots; solder them from the top only and keep the bottom flush "
         "(the display glass is 0.1 mm below). D+/D- of both orientations are tied, correct for a 2.0 device. Shield to GND.")
part("R1", R_, "5.1k", R0402, "USB", 78, 46, conn={1:"CC1", 2:"GND"})
part("D5", "Device:D", "ESD9L5.0ST5G-N", "lc:D_DFN1006-2", "USB", 96, 46, conn={1:"USB_DP", 2:"GND"},
     note="v3q: USB ESD, BORN ESD9L5.0ST5G-N (LCSC C316041), DFN1006-2, 0.6 pF, Vrwm 5 V, unidirectional. "
          "pin1=K on the data line, pin2=A to GND (checked against JLC's symbol/footprint: pad 1 = C).")
part("D6", "Device:D", "ESD9L5.0ST5G-N", "lc:D_DFN1006-2", "USB", 102, 46, conn={1:"USB_DM", 2:"GND"})
part("R2", R_, "5.1k", R0402, "USB", 84, 46, conn={1:"CC2", 2:"GND"})
part("C1", C_, "4.7uF", C0603, "USB", 90, 46, conn={1:"VBUS", 2:"GND"})

# ==================================================== POWER PATH ===========
part("D1", "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123", "PWR", 100, 34,
     conn={1:"VSYS", 2:"VBUS"},
     note="KiCad Device:D_Schottky is pin1=K, pin2=A. Cathode to VSYS so it "
          "feeds the system from USB and blocks the cell from back-feeding VBUS.")
part("Q1", "Transistor_FET:Q_PMOS_GSD", "AO3401A", "Package_TO_SOT_SMD:SOT-23",
     "PWR", 100, 50, conn={1:"PFET_GATE", 2:"VSYS", 3:"VBAT"},
     note="G=1 S=2 D=3. SOURCE on VSYS, DRAIN on the cell: a P-FET's body diode "
          "runs drain->source, so it points VBAT->VSYS and the cell still feeds "
          "the system during the gate transition. (Fixed in v2d - it was wired "
          "S=VBAT/D=VSYS, which let VSYS charge the cell through the body diode "
          "whenever USB was present, bypassing the charger.) USB present: "
          "Vg = 5V*1M/1.1M = 4.55V vs Vs = VSYS ~4.6V -> off. USB absent: Vg = 0 -> on. AO3401A (Basic C15127): -30V, "
          "Vgs(th) -0.9V, 60mOhm at -4.5V; pinout 1=G 2=S 3=D checked against JLC's symbol.")
part("R3", R_, "100k", R0402, "PWR", 92, 56, conn={1:"VBUS", 2:"PFET_GATE"})
part("R4", R_, "1M",   R0402, "PWR", 92, 62, conn={1:"PFET_GATE", 2:"GND"})

part("U2", "Battery_Management:MCP73831-2-OT", "TP4054", SOT235,
     "PWR", 70, 70, conn={1:"CHG_STAT_OD", 2:"GND", 3:"VBAT", 4:"VBUS", 5:"CHG_PROG"},
     note="TP4054 (UMW, C668215) on the MCP73831 symbol: identical pinout, checked "
          "against JLC's TP4054 symbol - 1=CHRG 2=GND 3=BAT 4=VCC 5=PROG. CHRG is "
          "open-drain (low while charging, off when done / no cell), which is exactly "
          "what the R7 pull-up, D2 and the R31/R33 divider already expect.")
part("R5", R_, "5.1k", R0402, "PWR", 60, 78, conn={1:"CHG_PROG", 2:"GND"},
     note="v3q: Ibat = 1000V/Rprog = 196mA (was 3.3k = 303mA): about 0.65C on a 300mAh 403030 cell, and it keeps "
          "charger + system inside a 500mA USB budget")
part("R6", R_, "1k",   R0402, "PWR", 84, 76, conn={1:"VBUS", 2:"LED_CHG"})
part("D2", "Device:LED", "CHG", "LED_SMD:LED_0603_1608Metric", "PWR", 90, 76,
     conn={1:"CHG_STAT_OD", 2:"LED_CHG"},
     note="pin1=K pin2=A. Anode to the VBUS side, cathode to STAT, so the LED "
          "lights only while STAT is sinking - i.e. only while charging.")
part("R7", R_, "47k", R0402, "PWR", 84, 68, conn={1:"VBUS", 2:"CHG_STAT_OD"},
     note="STAT pull-up referenced to VBUS, NOT to +3V3. A 3V3 pull-up would "
          "forward-bias the charger's ESD diode whenever USB is unplugged, "
          "back-feeding the VBUS net and corrupting VBUS_SENSE.")
part("R31", R_, "100k", R0402, "PWR", 78, 62, conn={1:"CHG_STAT_OD", 2:"CHG_STAT"},
     note="divider top. STAT idles near 4.5V so it must be divided before it "
          "reaches a 3V3 pin.")
part("R33", R_, "200k", R0402, "PWR", 78, 56, conn={1:"CHG_STAT", 2:"GND"},
     note="divider bottom (Basic 100k/200k): done/idle = 5V*300k/347k*2/3 = 2.88V "
          "(above VIH 2.48V, 3.17V at 5.5V VBUS), charging ~0.1V, unplugged 0V. "
          "USB present + low = charging, USB present + high = done.")
part("C2", C_, "4.7uF", C0603, "PWR", 60, 62, conn={1:"VBAT", 2:"GND"})
part("C3", C_, "22uF",  C0805, "PWR", 108, 44, conn={1:"VSYS", 2:"GND"},
     note="holds VSYS up through WiFi TX bursts when running from USB via D1")

part("J5", "Connector_Generic:Conn_01x02", "BAT",
     "lc:BAT_Pads_Small", "PWR", 50, 62, conn={1:"VBAT", 2:"GND"},
     note="solder pads, cell wires go straight on - no connector by request")

# 3V3 rail
part("U10", "Regulator_Linear:AP2112K-3.3", "AP2112K-3.3", SOT235,
     "PWR", 118, 40, conn={1:"VSYS", 2:"GND", 3:"VSYS", 4:None, 5:"+3V3"},
     note="EN tied to VIN = always on. 1=VIN 2=GND 3=EN 4=NC 5=VOUT")
part("C4", C_, "1uF",  C0402, "PWR", 112, 48, conn={1:"VSYS", 2:"GND"})
part("C5", C_, "10uF", C0805, "PWR", 132, 48, conn={1:"+3V3", 2:"GND"})

# VBUS presence
part("R8", R_, "33k", R0402, "PWR", 100, 24, conn={1:"VBUS", 2:"VBUS_SENSE"})
part("R9", R_, "51k", R0402, "PWR", 100, 18, conn={1:"VBUS_SENSE", 2:"GND"},
     note="v3q: 33k/51k (both Basic), ratio 0.607: 2.67V at 4.4V (VIH 2.48V), 3.04V at 5.0V, 3.34V at 5.5V "
          "(3.36V worst case with 1% parts), below VDD+0.3 = 3.6V. The old 100k/200k gave 3.67V at 5.5V. "
          "60uA while USB is present, nothing on battery.")
part("C6", C_, "100nF", C0402, "PWR", 108, 18, conn={1:"VBUS_SENSE", 2:"GND"})

# ======================================================== GAUGE ============
part("R34", R_, "1M", R0402, "GAUGE", 60, 100, conn={1:"VBAT", 2:"VBAT_ADC"},
     note="battery-voltage divider replaces the MAX17048 fuel gauge (cost). 1M/1M "
          "halves VBAT (4.2V -> 2.1V) into IO3/ADC1_CH2; 2.1uA standing drain.")
part("R35", R_, "1M", R0402, "GAUGE", 66, 100, conn={1:"VBAT_ADC", 2:"GND"})
part("C7", C_, "100nF", C0402, "GAUGE", 46, 108, conn={1:"VBAT_ADC", 2:"GND"},
     note="holds the divider node for the ADC's sample capacitor (500k source)")
part("R10", R_, "4.7k", R0402, "GAUGE", 84, 100, conn={1:"+3V3", 2:"SDA_SYS"},
     note="system I2C now only goes to the J4 breakout; pull-ups kept for add-ons")
part("R11", R_, "4.7k", R0402, "GAUGE", 90, 100, conn={1:"+3V3", 2:"SCL_SYS"})

# ======================================================= CAMERA ============
# Standard ESP32-CAM 24-pin DVP order, by CAMERA-FLEX pin number k:
CAM_FLEX = {1:None, 2:None,                      # Y0/Y1 unused in 8-bit mode
    3:"CAM_D2",   4:"CAM_D1",  5:"CAM_D3",  6:"CAM_D0",
    7:"CAM_D4",   8:"CAM_PCLK", 9:"CAM_D5", 10:"GND",
    11:"CAM_D6", 12:"CAM_XCLK", 13:"CAM_D7", 14:"DOVDD",
    15:"DVDD",   16:"CAM_HREF", 17:"CAM_PWDN", 18:"CAM_VSYNC",
    19:"CAM_RESET", 20:"CAM_SIOC", 21:"AVDD", 22:"CAM_SIOD",
    23:"GND",    24:None}
part("J2", "Connector_Generic:Conn_01x24", "OV2640 DVP",
     "lc:FPC_BXCONN_FC-05B24P21H20",
     "CAM", 250, 60, conn={n: CAM_FLEX[25 - n] for n in range(1, 25)},
     note="v3m: J2 is turned 180 deg with its mouth at the top edge. The camera flex runs up from the module, "
          "over J2 and folds back down into the mouth, so the flex arrives turned over and mirrored: connector "
          "pad n meets flex pin 25-n, hence conn = CAM_FLEX[25-n]. Every net keeps the same X position along J2. "
          "BXCONN FC-05B24P21H20 has double-sided contacts, so the flipped flex face still contacts. "
          "Pads numbered in the physical FH12 order, pin 1 at -X.")

part("U4", "lc:TLV755P_DQN", "TLV75533PDQNR", "lc:LDO_X2SON-4_1x1_P0.65", "CAM", 210, 30,
     conn={1:"DOVDD", 2:"GND", 3:"CAM_EN", 4:"VSYS", 5:"GND"},
     note="v3g: TLV75533PDQNR (LCSC C2861882), TI TLV755P in X2SON-4 1x1 mm (DQN), 500 mA. Pin 1=OUT 2=GND 3=EN 4=IN, "
          "thermal pad to GND (TI DQN pin table). Replaces the SOT-23-5 AP2112K-3.3 to clear the top routing corridor. ")
part("C8",  C_, "1uF",   C0402, "CAM", 202, 38, conn={1:"VSYS", 2:"GND"})
part("C9",  C_, "2.2uF", C0603, "CAM", 222, 38, conn={1:"DOVDD", 2:"GND"})

part("U5", "lc:TLV755P_DQN", "TLV75528PDQNR", "lc:LDO_X2SON-4_1x1_P0.65", "CAM", 210, 50,
     conn={1:"AVDD", 2:"GND", 3:"CAM_EN", 4:"VSYS", 5:"GND"},
     note="v3g: TLV75528PDQNR (LCSC C2865384), TI TLV755P in X2SON-4 1x1 mm (DQN), 500 mA. Pin 1=OUT 2=GND 3=EN 4=IN, "
          "thermal pad to GND (TI DQN pin table). Replaces the SOT-23-5 ME6211C28 to clear the top routing corridor. Low TI stock at LCSC (72): pin-compatible TECH PUBLIC TPTLV75528PDQNR C55191439 as second source.")
part("C10", C_, "1uF",   C0402, "CAM", 202, 58, conn={1:"VSYS", 2:"GND"})
part("C11", C_, "2.2uF", C0603, "CAM", 222, 58, conn={1:"AVDD", 2:"GND"})

part("U6", "lc:TLV755P_DQN", "TLV75512PDQNR", "lc:LDO_X2SON-4_1x1_P0.65", "CAM", 210, 70,
     conn={1:"DVDD", 2:"GND", 3:"CAM_EN", 4:"VSYS", 5:"GND"},
     note="v3g: TLV75512PDQNR (LCSC C2867150), TI TLV755P in X2SON-4 1x1 mm (DQN), 500 mA. Pin 1=OUT 2=GND 3=EN 4=IN, "
          "thermal pad to GND (TI DQN pin table). Replaces the SOT-23-5 ME6211C12 to clear the top routing corridor. Low TI stock at LCSC (56): pin-compatible TECH PUBLIC TPTLV75512PDQNR C55191435 as second source. Dropout is no issue at 1.2 V out.")
part("C12", C_, "1uF",   C0402, "CAM", 202, 78, conn={1:"VSYS", 2:"GND"})
part("C13", C_, "2.2uF", C0603, "CAM", 222, 78, conn={1:"DVDD", 2:"GND"})

part("R12", R_, "100k", R0402, "CAM", 196, 90, conn={1:"CAM_EN", 2:"GND"},
     note="holds the whole camera subsystem off at reset - also satisfies the "
          "IO45 strapping requirement")
part("R13", R_, "10k",  R0402, "CAM", 234, 30, conn={1:"DOVDD", 2:"CAM_PWDN"},
     note="camera defaults to powered-down as its rail comes up")
part("R14", R_, "10k",  R0402, "CAM", 234, 40, conn={1:"DOVDD", 2:"CAM_RESET"})
part("C14", C_, "100nF", C0402, "CAM", 240, 40, conn={1:"CAM_RESET", 2:"GND"},
     note="1ms power-on reset with R14; RESETB is active low")
part("R15", R_, "4.7k", R0402, "CAM", 234, 50, conn={1:"DOVDD", 2:"CAM_SIOC"},
     note="SCCB pull-ups go to the SWITCHED rail, so nothing leaks into an "
          "unpowered camera when it is off")
part("R16", R_, "4.7k", R0402, "CAM", 240, 50, conn={1:"DOVDD", 2:"CAM_SIOD"})
part("R17", R_, "2.2k", R0402, "CAM", 196, 100, conn={1:"DOVDD", 2:"LED_CAM"})
part("D3", "Device:LED", "CAM ON", "LED_SMD:LED_0603_1608Metric", "CAM", 202, 100,
     conn={1:"GND", 2:"LED_CAM"},
     note="wired to the switched camera rail, not to a GPIO - it cannot lie "
          "about whether the camera has power. Red KT-0603R (Basic, same part as "
          "D2): (3.3V - 1.9V)/2.2k = 0.6mA.")

# ======================================================== PANEL ============
part("J3", "Connector_Generic:Conn_01x10", "LS027B7DH01",
     "lc:FPC_BXCONN_FC-05B10P21H20",
     "PANEL", 250, 140, conn={
    1:"LCD_SCLK_P", 2:"LCD_SI_P", 3:"LCD_SCS_P", 4:"EXTCOMIN", 5:"LCD_DISP_P",
    6:"VDISP", 7:"VDISP", 8:"VDISP", 9:"GND", 10:"GND"},
    note="Sharp pin order: SCLK SI SCS EXTCOMIN DISP VDDA VDD EXTMODE VSS VSSA. "
         "VDD/VDDA want 4.8-5.5V but the logic inputs want ~3V, so the rail is "
         "boosted and the signals are not. v3q: EXTMODE (pin 8) to VDD as Sharp specifies (EXTMODE high = "
         "VCOM from the EXTCOMIN pin); it was on the 3.3V GPIO rail, below its 4.8V VIH.")

part("U7", "Regulator_Switching:TPS61222DCK", "TPS61222", "Package_TO_SOT_SMD:SOT-363_SC-70-6",
     "PANEL", 200, 140, conn={1:"VBAT", 2:"VDISP", 3:"GND", 4:"VDISP",
                              5:"SW_L", 6:"VBAT"},
     note="1=VIN 2=FB 3=GND 4=VOUT 5=L 6=EN, verified against the SC-70-6 "
          "datasheet. Fixed-output parts require FB tied straight to VOUT. "
          "Fed from VBAT so the input never exceeds the output. v3q: EN tied to VIN, the panel rail is always on. "
          "The TPS61222 has no load disconnect (EN low still leaves VBAT on VOUT through L1 and the rectifier), "
          "so a GPIO 'off' was never a real off. Blank the panel with DISP instead. Iq about 6uA.")
part("L1", L_, "4.7uH", "Inductor_SMD:L_0805_2012Metric", "PANEL", 190, 132,
     conn={1:"VBAT", 2:"SW_L"})
part("C16", C_, "10uF", C0603, "PANEL", 182, 146, conn={1:"VBAT", 2:"GND"})
part("C17", C_, "10uF", C0603, "PANEL", 216, 146, conn={1:"VDISP", 2:"GND"})
# v3q: R18 (PANEL_EN pull-down) removed - the boost is always enabled.

# hardware VCOM oscillator - powered from the PANEL_EN GPIO so it can never
# drive EXTCOMIN into an unpowered panel
part("U8", "74xGxx:SN74LVC1G14DBV", "74LVC1G14GW", "Package_TO_SOT_SMD:SOT-353_SC-70-5", "PANEL", 200, 170,
     conn={1:None, 2:"OSC_RC", 3:"GND", 4:"EXTCOMIN", 5:"+3V3"},
     note="v3h: 74LVC1G14GW in SC-70-5 (TECH PUBLIC, LCSC C19829653), same 1=NC 2=A 3=GND 4=Y 5=VCC pinout as the SOT-23-5 DBV. 1=NC 2=A 3=GND 4=Y 5=VCC, verified against the symbol. v3q: supply "
          "from +3V3 (the panel rail is now always on, so VCOM must always run). EXTCOMIN stays a 3.3V logic level.")
part("R19", R_, "200k", R0402, "PANEL", 210, 178, conn={1:"EXTCOMIN", 2:"OSC_RC"},
     note="v3q: 200k (Basic) instead of 1M: about 6Hz, mid-window instead of 1.25Hz at the 1Hz floor")
part("C18", C_, "1uF", C0603, "PANEL", 192, 178, conn={1:"OSC_RC", 2:"GND"},
     note="f = 1/(0.8*R*C) = 6.25Hz with R19 = 200k (5-7Hz over the 74LVC1G14 hysteresis spread), "
          "inside Sharp's 1-20Hz window. 200k/1uF are Basic parts.")
part("C19", C_, "100nF", C0402, "PANEL", 208, 164, conn={1:"+3V3", 2:"GND"})
# v3q: R29 (0R link from the PANEL_EN GPIO) removed - U8 runs from +3V3.
# v3i: R30 (DNP alternative +3V3 feed for the oscillator) removed: it was the only +3V3 load on the far side of the cell.

# series guards on the panel logic lines
part("R22", R_, "330R", R0402, "PANEL", 230, 120, conn={1:"LCD_SCLK", 2:"LCD_SCLK_P"})
part("R23", R_, "330R", R0402, "PANEL", 230, 126, conn={1:"LCD_SI",   2:"LCD_SI_P"})
part("R24", R_, "330R", R0402, "PANEL", 230, 132, conn={1:"LCD_SCS",  2:"LCD_SCS_P"})
part("R25", R_, "330R", R0402, "PANEL", 230, 138, conn={1:"LCD_DISP", 2:"LCD_DISP_P"})

# ============================================================ UI ===========
part("SW1", "Switch:SW_Push", "D2MQ-1-TR", "lc:SW_Omron_D2MQ-1-TR", "UI", 60, 140,
     conn={1:"SHUTTER", 2:"GND"},
     note="v3: Omron D2MQ-1-TR ultra-slim snap-action switch (LCSC C7156725), right-angled terminals, "
          "lies flat (2.7 mm tall instead of the D2F's 5.8), plunger out of the top edge, top-right in display view. "
          "Pad 1 = middle terminal (NO); both end terminals = pad 2 = GND, so COM/NC order does not matter. "
          "Footprint geometry partly inferred - verify against a real part. Hand-fit (not on the JLC BOM).")
part("R26", R_, "10k", R0402, "UI", 52, 132, conn={1:"+3V3", 2:"SHUTTER"})
part("C24", C_, "100nF", C0402, "UI", 70, 148, conn={1:"SHUTTER", 2:"GND"})

part("BZ1", "Device:Buzzer", "MLT-5020", "lc:Buzzer_SMD_MLT5020", "UI", 100, 140,
     conn={1:"+3V3", 2:"BUZZ_DRV"}, note="pin1 is the + terminal. MLT-5020 (LCSC C94598), 5x5.5 mm passive magnetic. Its third pad is a mechanical anchor with no net.")
part("Q2", "Transistor_FET:Q_NMOS_GSD", "DMG1012UW-7", "Package_TO_SOT_SMD:SOT-323_SC-70",
     "UI", 100, 156, conn={1:"BUZZ_G", 2:"GND", 3:"BUZZ_DRV"},
     note="v3q: DMG1012UW-7 (Diodes, LCSC C156390), SOT-323, Vgs(th) max 1.0V, 0.6 ohm at 2.5V: fully on from a "
          "3.3V GPIO (the 2N7002W was 2+ ohm and near threshold). Pinout 1=G 2=S 3=D checked against JLC's symbol.")
part("R27", R_, "1k",   R0402, "UI", 88, 156, conn={1:"BUZZ", 2:"BUZZ_G"})
part("R28", R_, "100k", R0402, "UI", 94, 164, conn={1:"BUZZ_G", 2:"GND"},
     note="also holds IO46 low at reset, which the strapping needs")
part("D4", "Device:D", "1N4148WS", "Diode_SMD:D_SOD-323", "UI", 112, 140,
     conn={1:"+3V3", 2:"BUZZ_DRV"},
     note="v3g: 1N4148WS in SOD-323 (LCSC C2128, basic part). pin1=K pin2=A: cathode to the rail, anode to the drain, so it clamps "
          "the coil kick on turn-off")

part("J4", "Connector_Generic:Conn_01x06", "SPARE IO", "lc:TestPads_1x06_P1.27",
     "UI", 60, 176, conn={1:"GND", 2:"SPARE_1", 3:None,
                          4:"SDA_SYS", 5:"SCL_SYS", 6:"+3V3"},
     note="v3i: moved to the top edge next to U1 so SDA/SCL/SPARE_1/+3V3 stay local. same idea as v1's J2, now with the system I2C bus on it too. Pad 3 "
          "(was SPARE_2 = IO3) is unconnected now that IO3 reads the battery.")


# ============================================ ERC power-source flags =======
# These tell ERC that a net is legitimately driven even though no symbol on it
# declares a power-output pin. They are not real parts.
part("FLG1", "power:PWR_FLAG", "PWR_FLAG", "", "PWR", 30, 30, conn={1:"GND"})
part("FLG2", "power:PWR_FLAG", "PWR_FLAG", "", "PWR", 36, 30, conn={1:"VBUS"},
     note="VBUS is sourced by the USB host, which no symbol here represents")
part("FLG3", "power:PWR_FLAG", "PWR_FLAG", "", "PWR", 42, 30, conn={1:"VSYS"},
     note="VSYS is fed through D1 and Q1, both of which are passive symbols")
# v3q: FLG4 (PANEL_EN_SW) removed with the net.

part("FLG5", "power:PWR_FLAG", "PWR_FLAG", "", "PANEL", 54, 30, conn={1:"SW_L"},
     note="the boost switching node. KiCad types the TPS61222 L pin as a power "
          "input, so ERC wants a source on it; the inductor is a passive symbol "
          "and cannot be one. Purely an ERC formality.")

# --------------------------------------------------------------------------
if __name__ == "__main__":
    from collections import defaultdict
    nets = defaultdict(list)
    npins = 0
    for ref, pins in CONN.items():
        for p, n in pins.items():
            npins += 1
            if n: nets[n].append(f"{ref}.{p}")
    print(f"{len(PARTS)} parts, {npins} pins, {len(nets)} nets")
    single = {n: v for n, v in nets.items() if len(v) < 2}
    print("single-pin nets (should be empty):", single)
    for n in sorted(nets):
        print(f"  {n:<14} {len(nets[n]):>2}  {' '.join(sorted(nets[n]))}")
