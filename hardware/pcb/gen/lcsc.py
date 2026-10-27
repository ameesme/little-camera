# -*- coding: utf-8 -*-
"""LCSC part assignments for JLCPCB assembly, resolved 2026-09-29 with the JLC MCP.

Keyed by the BOM value as it appears on the board. Each entry:
    (LCSC id, manufacturer part number, library type, stock at time of lookup, note)

Passives were picked from the JLCPCB Basic library wherever one exists in the
right package, because every Extended line carries a one-off setup fee.
"""

LCSC = {
    # ---------------------------------------------------------------- resistors (0402)
    "0R":    ("C17168", "0402WGF0000TCE", "basic",    9649103, ""),
    "1k":    ("C11702", "0402WGF1001TCE", "basic",    8133473, ""),
    "2.2k":  ("C25879", "0402WGF2201TCE", "basic",    2354371, ""),
    "3.3k":  ("C25890", "0402WGF3301TCE", "basic",    2168414, "sets TP4054 charge current (303mA)"),
    "4.7k":  ("C25900", "0402WGF4701TCE", "basic",   15730046, ""),
    "5.1k":  ("C25905", "0402WGF5101TCE", "basic",    6263741, "USB-C CC pull-downs; R5 TP4054 charge current (196mA)"),
    "33k":   ("C25779", "0402WGF3302TCE", "basic",    1208545, "v3q: VBUS divider top"),
    "51k":   ("C25794", "0402WGF5102TCE", "basic",    1911293, "v3q: VBUS divider bottom"),
    "10k":   ("C25744", "0402WGF1002TCE", "basic",   22360849, ""),
    "47k":   ("C25792", "0402WGF4702TCE", "basic",    5630304, ""),
    "100k":  ("C25741", "0402WGF1003TCE", "basic",    9158565, ""),
    "200k":  ("C25764", "0402WGF2003TCE", "basic",    2347635, "R33 STAT divider, R19 VCOM oscillator (v3q)"),
    "1M":    ("C26083", "0402WGF1004TCE", "basic",    2548709, ""),
    "330R":  ("C25104", "0402WGF3300TCE", "basic",    1251008, ""),
    # ---------------------------------------------------------------- capacitors
    "100nF": ("C1525",  "CL05B104KO5NNNC", "basic",  23056837, "0402 16V X7R"),
    ("1uF", "0402"): ("C52923", "CL05A105KA5NQNC", "basic", 5951968, "0402 25V X5R"),
    ("1uF", "0603"): ("C15849", "CL10A105KB8NNNC", "basic", 6433783, "0603 50V X5R - VCOM oscillator timing cap"),
    "2.2uF": ("C23630", "CL10A225KO8NNNC", "basic",   2652212, "0603 16V X5R"),
    "4.7uF": ("C19666", "CL10A475KO8NNNC", "basic",   2622319, "0603 16V X5R"),
    ("10uF", "0603"): ("C96446", "CL10A106MA8NRNC", "basic", 3424489,
                       "0603 25V X5R - chosen over the 10V basic part because C16/C17 sit on the 5V boost rail"),
    ("10uF", "0805"): ("C15850", "CL21A106KAYNNNE", "basic", 5193619, "0805 25V X5R"),
    "22uF":  ("C45783", "CL21A226MAQNNNE", "basic",   4115394, "0805 25V X5R"),
    # ---------------------------------------------------------------- magnetics / discretes
    "4.7uH": ("C63166", "MPL2012S4R7MHT",  "extended", 105458, "Sunlord 0805, 800mA, 250mOhm"),
    "B5819W":("C8598",  "B5819W SL",       "basic",    544688, "Changjing; Basic (was MDD Extended)"),
    "1N4148W":("C81598","1N4148W",         "basic",   5050564, ""),
    "CHG":   ("C2286",  "KT-0603R",        "basic",   3789423, "red"),
    "CAM ON":("C2286",  "KT-0603R",        "basic",   3789423, "red, same part as CHG (the yellow-green was Extended)"),
    "2N7002":("C8545",  "2N7002",          "basic",   1684537, ""),
    "AO3401A":("C15127","AO3401A",         "basic",    813323, "Alpha & Omega; Basic P-FET (was SI2301 Extended)"),
    # ---------------------------------------------------------------- ICs
    "ESP32-S3-WROOM-1-N16R8": ("C2913202", "ESP32-S3-WROOM-1-N16R8", "extended", 21303,
                               "STANDARD-ONLY: forces JLCPCB Standard PCBA"),
    "TP4054":        ("C668215",  "TP4054",            "extended",145104, "UMW; pin-compatible with MCP73831 (was $0.63)"),
    "TPS61222":      ("C116461",  "TPS61222DCKR",      "extended",  1082, ""),
    "74LVC1G14":     ("C7835",    "SN74LVC1G14DBVR",   "extended",318690, "TI"),
    "AP2112K-3.3":   ("C51118",   "AP2112K-3.3TRG1",   "extended", 40520, "Diodes Inc."),
    "ME6211C28M5G":  ("C53099",   "ME6211C28M5G-N",    "extended", 33689, "replaces AP2112K-2.8 (not stocked)"),
    "ME6211C12M5G":  ("C236672",  "ME6211C12M5G-N",    "extended", 11035, "replaces AP2112K-1.2 (200 in stock)"),
    # ---------------------------------------------------------------- electromechanical
    "USB-C":       ("C2906289", "TYPE-C 16P CB0.8 073", "extended",  30919,
                    "SHOU HAN; mid-mount for 0.8 mm boards, STANDARD-ONLY; shell legs are plated slots (hand-soldered THT)"),
    "OV2640 DVP":  ("C23398820", "FC-05B24P21H20",   "extended",   4147,
                    "BXCONN, double-sided contacts, gold; FH12-24S drop-in ($0.10 vs $0.51). Footprint pads in physical FH12 order"),
    "LS027B7DH01": ("C23398812", "FC-05B10P21H20",   "extended",   2313,
                    "BXCONN, double-sided contacts, gold; FH12-10S drop-in ($0.14 vs $0.51)"),
    "BOOT":        ("C455280", "TS-1088R-02026",      "extended", 173269, "matches footprint"),
    "RESET":       ("C455280", "TS-1088R-02026",      "extended", 173269, "matches footprint"),
    "MLT-5020":    ("C94598",  "MLT-5020",            "extended",  28936, "footprint replaced to match"),
    "D2F-01-A":    ("C2734474", "D2F-01-A",            "extended",     10,
                    "HAND-FIT: excluded from the JLC BOM/CPL (~$3.10 at LCSC)"),
    # ---------------------------------------------------------------- v3 additions (small board)
    "TLV75533PDQNR": ("C2861882", "TLV75533PDQNR", "extended", 2078, "TI X2SON-4 1x1, DOVDD 3.3V"),
    "TLV75528PDQNR": ("C2865384", "TLV75528PDQNR", "extended",   72, "TI X2SON-4 1x1, AVDD 2.8V; low stock - TECH PUBLIC TPTLV75528PDQNR C55191439 is pin-compatible"),
    "TLV75512PDQNR": ("C2867150", "TLV75512PDQNR", "extended",   56, "TI X2SON-4 1x1, DVDD 1.2V; low stock - TECH PUBLIC TPTLV75512PDQNR C55191435 is pin-compatible"),
    "2N7002W":       ("C5224238", "2N7002W",       "extended", 8502, "ElecSuper SOT-323, Vgs(th) 1.6V"),
    "1N4148WS":      ("C2128",    "1N4148WS",      "basic", 2267959, "Changjing SOD-323"),
    "74LVC1G14GW":   ("C19829653","74LVC1G14GW",   "extended", 3364, "TECH PUBLIC SC-70-5 (SOT-353)"),
    "DMG1012UW-7":   ("C156390",  "DMG1012UW-7",   "extended", 2465, "v3q: Diodes SOT-323 N-FET, Vgs(th) <=1.0V, buzzer driver"),
    "ESD9L5.0ST5G-N":("C316041",  "ESD9L5.0ST5G-N","extended", 405063, "v3q: BORN DFN1006-2 USB ESD, 0.6pF, pad 1 = cathode"),
    "D2MQ-1-TR":     ("C7156725", "D2MQ-1-TR",     "extended",   20, "HAND-FIT: Omron right-angle PCB terminals, excluded from the JLC BOM/CPL (~$7 at LCSC)"),
}


def lookup(value, footprint):
    """Return the LCSC tuple for a BOM line, or None."""
    for pkg in ("0603", "0805", "0402"):
        if pkg in footprint and (value, pkg) in LCSC:
            return LCSC[(value, pkg)]
    return LCSC.get(value)
