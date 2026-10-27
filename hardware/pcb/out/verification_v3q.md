# little camera v3q — respin na de elektrische review, verificatie vóór bestelling (2026-10-03)

v3q vervangt v3p. Aanleiding is de externe elektrische audit van 3 oktober (`out/electrical_audit_2026-10-03.md`). De keuzes van Mees daarbij:

- display altijd aan;
- camerakern op 1,2 V houden;
- verder alles wat nodig is, zolang het schema niet onnodig complex wordt.

## 1. Wat er veranderd is

| Bevinding uit de audit | Wijziging in v3q |
|---|---|
| 1. EXTMODE (J3/8) op 3,3 V, terwijl die VDD (4,8–5,5 V) wil | J3/8 hangt nu aan **VDISP**, zoals Sharp voorschrijft. EXTMODE hoog = VCOM via de EXTCOMIN-pin; dat is het bestaande hardwarecircuit. |
| 2. PANEL_EN schakelt de display niet echt uit (de TPS61222 heeft geen load disconnect) | **Display altijd aan.** U7 EN hangt aan VBAT. R18 (pull-down) en R29 (0R-link) zijn weg. U8 (VCOM-oscillator) en C19 hangen aan +3V3, dus VCOM loopt altijd. Het netje PANEL_EN / PANEL_EN_SW bestaat niet meer en **IO15 is vrij** (U1 pin 8 NC). Het beeld uitzetten gaat via DISP (IO7). De boost gebruikt in rust ongeveer 6 µA. |
| 3. VBUS_SENSE tot 3,67 V bij 5,5 V | R8/R9 is **33k/51k** (beide Basic), verhouding 0,607. Dat geeft 2,67 V bij 4,4 V (VIH 2,48 V), 3,04 V bij 5,0 V en 3,34 V bij 5,5 V; met 1 %-weerstanden in het slechtste geval 3,36 V. Dat blijft onder VDD + 0,3 = 3,6 V. Het verbruik is 60 µA, alleen als USB erin zit. |
| 4. DVDD 1,2 V vs. OV2640 v2.2 (1,3 V) | **Bewust gehouden op 1,2 V** (keuze Mees), zoals op de gangbare ESP32-CAM-modules. Dit staat als afwijking in `design.py` (U6). |
| 6. Laadstroom 303 mA zonder USB-budget | R5 is **5,1k**: 196 mA, ongeveer 0,65C voor een 403030-cel. Lader plus systeem blijven zo dichter bij een USB-budget van 500 mA. |
| 7. Geen lokale ontkoppeling bij de ESP32-module | **C21 (10 µF) en C20 (100 nF) staan nu bij U1 pin 2** (+3V3-ingang), net buiten de antenne-keep-out, ongeveer 6 mm van de pin (was 23–25 mm). U10 houdt C5 (10 µF) en C22 (1 µF). |
| 8. Geen USB-ESD | **D5/D6 = BORN ESD9L5.0ST5G-N** (C316041), DFN1006-2, 0,6 pF, 5 V Vrwm, één per datalijn. De kathode zit op D+/D−, de anode op GND. Ze staan op de route van J1 naar U1, ongeveer 9 mm van de connector. |
| (VCOM-frequentie 1,25 Hz, tegen de ondergrens van 1 Hz) | R19 is **200k** (Basic) in plaats van 1M: ongeveer 6 Hz, midden in Sharps venster van 1–20 Hz. 470k zou een Extended-onderdeel zijn geweest; 200k zit al op de BOM. |
| (Wens Mees: dock-aansluiting onder de camera) | **J4 is een dock-rij geworden** (`lc:TestPads_Dock3_P2.54`). Pads 1–3 liggen op één rij, Ø1,5 mm, steek 2,54 mm: **GND, IO43 (U0TXD, console) en +3V3**. Pads 4–6 (SDA_SYS, SCL_SYS, GND, 0,9 × 1,6 op 1,27) blijven als testpads onder de achterplaat. Het board is opnieuw gerouteerd; BOM en CPL zijn identiek gebleven. |
| (Buzzerdriver op de grens bij 3,3 V gate-drive) | Q2 is **DMG1012UW-7** (Diodes, C156390), SOT-323, Vgs(th) ≤ 1,0 V en 0,6 Ω bij 2,5 V. De pinout 1 = G, 2 = S, 3 = D is gelijk gebleven. |

**Bewust niet gedaan:** de buzzer op VSYS zetten. De MLT-5020 is gespecificeerd voor 2–4 V (nominaal 3 V), en VSYS haalt 4,2 V op de accu en ongeveer 4,6 V op USB. Hij blijft dus op 3,3 V, en de achterplaat blijft ongewijzigd.

**Netto aan onderdelen:** R18, R29 en FLG4 zijn eruit; D5 en D6 zijn erbij. Dat geeft 80 onderdelen op het board en 58 nets (was 60).

## 2. Fabricagebestanden tegen ontwerp-intentie (onafhankelijk van KiCad)

De gerbers en drillfiles (`little_camera_v3q_gerbers.zip`) zijn opnieuw als exacte vectorgeometrie ingelezen. Er is geen KiCad-data gebruikt voor de verbindingen. De nets komen uit `design.py`. Er zijn 4 koperlagen, 216 doorgemetaliseerde gaten (waarvan 4 sleuven) en 197 vias. *(Bijgewerkt na de dock-respin.)*

| Check | Resultaat |
|---|---|
| Opens: elk net precies één koperstuk | **58/58 nets, 0 opens** |
| Shorts | **0** |
| Zwevend koper | **0** |
| Min. koper–koper tussen verschillende nets (F / In1 / In2 / B) | 0,1286 / 0,1353 / 0,1286 / 0,1286 mm (regel 0,127) |
| Min. koper–boardrand | 0,30 mm. Alleen de padrij van de USB-C zit op 0,21 mm tot de uitsparing; dat is bewust, volgens de fabrikanttekening (JLC-minimum 0,2). Al het andere koper zit op ≥ 0,30 mm. |
| Min. annular ring van de vias | 0,124 mm (0,5/0,25-via) |
| Min. gatwand tot koper van een ander net | 0,253 mm |
| IPC-D-356 (`little_camera_v3q.ipc`) tegen `design.py` | **0 verschillen** (272 pinnen) |
| Mutatietest | Een expres doorgeknipt EXTCOMIN-spoor gaf **OPEN**. Een expres brugje over D5 (USB_DP–GND) gaf **SHORT**. Na de dock-respin: een brugje tussen J4-pad 2 en 3 gaf **SHORT** (+3V3–SPARE_1), en een doorgeknipt spoor bij pad 3 gaf **OPEN** (+3V3). |

**KiCad DRC.** 0 niet-verbonden items en 0 clearance-fouten. Er staan nog 18 fouten open, die allebei bewust zijn:

- 16 meldingen over de J1-pads tot de uitsparing (toegestaan door de projectregel `.kicad_dru`);
- 2 courtyard-overlaps tussen C20/C21 en de antenne-courtyard van U1.

De antenne-courtyard van de Espressif-footprint is ±11 mm breed. C20/C21 liggen buiten de antenne-keep-out-regel (x > 18,1) in een gebied dat al met GND gevuld was, en 1 mm van de modulebehuizing. De 89 waarschuwingen zijn silk en cosmetisch.

Na de autorouter is één GND-fanoutstub bij J2 versmald van 0,25 naar 0,20 mm (er was 0,125 mm naar twee camera-vias). Vier dangling fanout-vias bij J3 en drie dode stubjes zijn verwijderd.

**Dock-respin.** Na de nieuwe routering (Freerouting `-mp 60`, met LCD_SCS handmatig afgemaakt) is dezelfde opruimronde gedaan: de GND-stub bij J2 is naar 0,20 mm gegaan, en 5 dangling vias met 4 dode stubjes zijn verwijderd (AVDD, DOVDD, 2× VDISP, LCD_DISP_P). Daarna gaf de DRC weer exact dezelfde 18 bewuste fouten, 0 niet-verbonden items en 0 clearance-fouten. `conncheck`: 58/58 nets verbonden. `padnets`: 272 pinnen, 0 afwijkingen.

## 3. Pinouts en plaatsing tegen JLC's bibliotheek

- **D5/D6 (C316041):** JLC's symbool heeft pin 1 = C en pin 2 = A. De footprint X1-DFN1006-2 heeft pad 1 op −0,35 mm, net als die van ons. Rotatie-offset 0.
- **Q2 (C156390):** JLC's symbool heeft 1 = G, 2 = S, 3 = D. Rotatie-offset +180°, hetzelfde als bij de vorige SOT-323.
- **Plaatsingssimulatie:** JLC's footprints zijn op de positie en rotatie uit de nieuwe CPL gezet, voor U1, U2, U4–U8, U10, Q1, Q2, D1–D6, BZ1, J1–J3 en L1. Elke pad landt ≤ 0,21 mm van onze pad met hetzelfde nummer; D5/D6 en de IC's landen ≤ 0,16 mm. De EP van U1 bestaat uit een array van subpads en valt in hetzelfde GND-vlak.

## 4. Schema

`mksch.py` heeft het schema opnieuw opgebouwd uit `design.py`: 84 symbolen.

- Audit tegen `design.py`: geen afwijkingen.
- KiCad ERC: 0 fouten, 0 waarschuwingen.
- PDF: `out/little_camera_v3q_schematic.pdf`.

## 5. Displaystaart en behuizing (3D)

De staart van de LS027B7DH01 is uit een productfoto geschat, met de glasbreedte van 62,8 mm als schaal (±0,7 mm):

- ~13,5 mm lang vanaf de glasrand;
- 9,4 mm breed over de eerste ~5 mm, daarna 5,5 mm;
- verstijver ~4,6 mm.

Het oude 3D-model rekende met ~6 mm. J3 kan niet verder naar binnen: de accu-uitsparing begint op y 6,8. De sleuf van J3 is 3,15 mm diep, dus de verstijver steekt ~1,45 mm uit de connector.

**Gekozen oplossing:** een S-vouw in de bestaande onderrand, zonder PCB-wijziging.

- `out/display_tail_section.png` laat de vorm zien. Het rekenmodel staat in `gen/fpc_geom.py`.
  - De staart gaat omhoog langs de wand, maakt een boog (top op z 2,9, achterplaat op 3,55), en vouwt met R0,5 terug in de verstijver.
  - De vouw lukt alleen als de staart minstens 13,4 mm lang is.
- **Displaymodel:** het nieuwe model `lc.3dshapes/MECH_LS027B7DH01_v3q.step` komt uit `gen/mk_disp_v3q.py`.
  - Het TFT-glas loopt over het volle formaat.
  - Het CF-glas begint 2,5 mm hoger, waardoor er een richel ontstaat (zie Sharp fig. 8-2).
  - De FPC is gebonden op de kijkkant van die richel.
- **Behuizing (`gen/fusion/encv5.py`):**
  - Aan de binnenkant van de onderwand zit nu een verdieping van 0,2 mm achter de staart (STEP x 27,2–38,6). Daarmee is de minimale afstand van de staart tot de behuizing 0,25 mm.
  - De drukpennen naast de inkepingen zijn 0,65 mm breed (was 0,85). De linker pennen overlapten 0,1 mm met de rand van de ESP32-module.
- **Interferentiecheck:** volledig in cadquery, met de board-STEP (`out/little_camera_v3q_full.step`) tegen de vier behuizingsdelen die uit Fusion zijn geëxporteerd.
  - Er blijft één aanraking over: de USB-C-schaal tegen de wand van de opening (0,0055 mm³; de connector zit bewust gelijk met de buitenkant).
  - Fusion's eigen check geeft ook 0.
- **Belangrijk:** de eerdere Fusion-checks (v3p, "0 interferenties") waren niet geldig. De cut-features in de behuizingsscripts sneden zonder participant bodies ook dwars door het geïmporteerde board. Dat is nu opgelost in `encv5.py`: cuts raken alleen nog het eigen onderdeel.
- **Nog open:** meet de echte staart (lengte vanaf de glasrand, lengte van de verstijver) voordat je de S-vouw definitief maakt.

### Coverglas verzonken, gelijk met de voorkant (`gen/fusion/encv6.py`)

- **Geen rand meer voor het glas:** de rand van 0,5 mm voor het glas is weg. Het glas ligt nu in een kom, gelijk met de voorkant, en de behuizing is 0,5 mm dunner: 7,98 → 7,48 mm (plus de camerabult).
- **Glas:** 64,4 × 44,0 × 0,55 mm, hoekradius r0,8. Het rust op een richel rond het display: 0,7 mm breed links en rechts, 0,5 mm boven en onder.
  - De kom is 0,6 mm diep (0,55 glas + 0,05 lijm) en laat 0,1 mm speling rondom.
  - Het zichtbare kader van de behuizing is overal 1,4 mm.
- **Zwarte rand op het glas:** het zichtvenster wordt een zwarte rand die op de achterkant van het glas is gedrukt.
  - Het actieve gebied van de LS027 ligt niet in het midden van het glas. Het zit ~2,06 mm van de bovenrand en ~5,48 mm van de FPC-rand (lens y 5,48–40,76, x 2,0–60,8). Dit is gemeten op een productfoto en geschaald naar de spec-hoogte van 35,28 mm; reken op ±0,4 mm.
  - De CF-glasrand ligt 1,7 mm van de TFT-rand.
  - Het gedrukte venster is het actieve gebied plus 0,3 mm rondom. In STEP-coördinaten is dat x 3,2–62,6 en y 4,38–40,26 (`PWIN` in `gen/fusion/encv7.py`).
- **Verticaal gecentreerd op het actieve gebied (`gen/fusion/encv8.py`):** de behuizing is gecentreerd op het actieve gebied (STEP y 22,32), niet op het displayglas.
  - De onderkant blijft waar hij was: daar zitten de USB-C, die gelijk moet blijven met de buitenkant, en de displaystaart. De bovenkant wordt 3,42 mm langer.
  - Behuizing: 67,4 × 50,44 × 7,48 mm. Coverglas: 64,4 × 47,44 × 0,55 mm.
  - Zwarte rand op het glas: 2,5 mm links en rechts, 5,78 mm boven en onder. Vanaf de buitenkant gemeten is dat 7,28 mm boven en onder, dus geen dikkere kin.
  - De knop overbrugt nu ~4,1 mm naar de plunjer van de D2MQ. Het drukpinnetje is daarom 1,5 mm dik gemaakt (was 1,0 mm). De speling tot de plunjer is 0,1 mm.
  - Gecontroleerd in cadquery: geen interferenties, behalve de bekende aanraking van de USB-C (0,0055 mm³). Staart tot behuizing 0,25 mm; de onderdelen van de behuizing overlappen elkaar niet.
- **Definitieve cameramodule (`gen/mk_cam_v3q.py` → `MECH_OV2640_CAM_v3q.step`, behuizing `gen/fusion/encv9.py`):** 8 × 8 mm, 6,0 mm dik inclusief lens.
  - Opbouw van het model: 0,4 verstijver, 2,9 behuizing, 2,7 lensbarrel (Ø6,4).
  - De bovenkant van de lens ligt 0,1 mm hoger dan in het oude model (8,5 × 8,5 × 5,9). De camerabult is daarom 0,1 mm hoger: bovenkant op STEP z 7,55, de bult steekt 2,4 mm uit.
  - Totale dikte met bult: 9,88 mm.
  - Speling lensbarrel tot achterplaat 0,175 mm, camerabehuizing tot achterplaat 0,125 mm, geen interferenties.
- **Gecontroleerd in cadquery:**
  - glas tot display 0,05 mm, glas tot behuizing 0,05 mm;
  - staart tot behuizing 0,25 mm (de achterkant van de richel ligt op 0,25 mm onder de staart);
  - geen interferenties, behalve de bekende aanraking van de USB-C-schaal (0,0055 mm³).

### Dock-contactrij door de achterplaat (`gen/fusion/encv11.py`)

Zie `out/pogo_layout.png`.

- Drie gaten op één rij, op de padposities zoals uit de gerbers teruggelezen (STEP-coördinaten): x 20,82 / 23,36 / 25,90, y 40,6. Dat is 21,62 mm van de linkerrand en 6,94 mm van de bovenrand, in achteraanzicht.
- Gat Ø1,2 mm door de achterplaat (voor P75-pogopinnen, huls Ø1,02), met een lead-in van Ø1,8 × 0,3 mm aan de buitenkant.
- **De buzzergaatjes (3 × Ø0,8, steek 1,2) liggen op dezelfde lijn y 40,6**, dus 2,25 mm boven het midden van de buzzer. Ze zitten nog boven de buzzerbehuizing (y 35,6–41,1), en tussen de bovenkant van de buzzer en de achterplaat zit 1,5 mm lucht.
- Van de buitenkant van de achterplaat tot het pad is het 4,30 mm. Een dock-pin moet dus minstens ~4,5 mm uit de dock steken (bijv. een P75 met een lange plunjer, of een pin die met huls en al in het gat zakt).
- Gecontroleerd in cadquery: geen onderdeel binnen 4,0 mm van het pinpad (cilinder Ø1,1 van pad tot achterplaat). Alle andere spelingen zijn ongewijzigd: lensbarrel 0,175, camerabehuizing 0,125, staart 0,25, knop 0,1 mm. De enige aanraking blijft de bekende van de USB-C.
- **Nog te doen in Fusion:** Fusion reageerde niet op het moment van de respin. De controle is gedaan op `out/enc_v3q_A_back_plate_dock_cq.step`: de encv10-achterplaat met dezelfde gaten in cadquery. Zodra Fusion weer reageert worden `encv11.py` en `dummy12.py` gedraaid en de STEP/STL opnieuw geëxporteerd.

### Dummy om te printen

`out/little_camera_v3q_dummy.stl` en `.3mf` komen uit `gen/fusion/dummy10.py` (op basis van `encv9.py`).

- Het is één massief stuk met de buitenvorm van behuizing A, zonder glas, kom of holtes (dus ook geen overhang).
- Het display is alleen als twee gegraveerde lijnen gemarkeerd (0,3 breed, 0,15 diep): de rand van het coverglas en het venster.
- Maat: 67,4 × 50,9 × 9,88 mm, inclusief knop en camerabult.

## 6. Nog open vóór bestelling

0. **Fusion:** draai `gen/fusion/encv11.py` (behuizing met dock-gaten) en `dummy12.py` (dummy met Ø1,2-putjes op de dockposities), en exporteer de behuizing-STEP's opnieuw.
1. **Camera-flex: orientatie bevestigd (2026-10-04).**
   - Mees heeft gemeten met de lens op tafel en de flex naar zich toe. Vanaf het **rechter** uiteinde gaven contact 2 en 15 doorgang.
   - Omgedraaid naar lens omhoog met de flex richting de bovenrand blijft links/rechts gelijk. Het pin 1-uiteinde ligt dan **rechts**, zoals J2 verwacht. Er is dus geen boardwijziging nodig.
   - De meting is eenduidig: vanaf het andere uiteinde zijn 2 en 15 de contacten 23 (Y1) en 10 (DVDD), en die zijn niet doorverbonden.
   - De contacten zitten aan de kant tegenover de lens. Dat maakt niet uit, want de BXCONN-connector heeft dubbelzijdige contacten.
   - **Flexlengte: 40 mm** (gemeten door Mees). Het gemodelleerde pad vanaf de modulerand gebruikt ~25 mm. De rest wordt boven het board opgeborgen, dus **geen boardwijziging**:
     - de lus kan ~3,4 mm verder richting de bovenwand (+6,8 mm);
     - een grotere lusstraal (r 1,3) geeft +1,5 mm;
     - wat overblijft gaat in een platte Z-vouw boven de run (r 0,4, 1,96 mm stapelhoogte binnen 2,2 mm).
     - Het cameramodel moet nog bijgewerkt worden.

2. **Firmware.**
   - Het display staat altijd onder spanning. Stuur DISP (IO7) hoog om het beeld te tonen en laag om het te blanken.
   - IO15 is vrij.
   - VBUS_SENSE ligt bij USB nu rond 3,0 V. Dat blijft een digitale hoog, dus er verandert niets.
2b. **J2-plaatsing gecorrigeerd (2026-10-04, ná de eerste upload).**
   - JLC's footprint van de FC-05B24P21H20 heeft dezelfde geometrie als de onze (contacten op y −1,8, lipjes op +1,3, hetzelfde nulpunt). Alleen de pinnummering loopt andersom.
   - De oude CPL-correctie (verschuiving 3,6 mm, +180°) legde de pinnummers op elkaar en draaide daarmee de behuizing om: de mond wees naar de camera, de lipjes stonden op kale print en de behuizing op C9. In JLC's preview was dat te zien.
   - Nieuw: J2 op (55,30; 39,02), rotatie 180. Een plaatsingssimulatie met JLC's eigen footprint geeft voor alle 26 pads 0,000 mm. De lipjes vallen op onze MP-pads en de mond zit aan de bovenrand.
   - De pinnummers zijn omgekeerd (JLC 1 = onze 24). Dat is onschuldig, want een FPC-connector verbindt positie op positie.
   - J1, J3, BZ1, U1, L1, D5/D6, U4–U6, Q2 en U8 zijn opnieuw gesimuleerd: allemaal ≤ 0,10 mm, op de juiste pads.
3. **JLC-preview:** controleer bij het bestellen nog de rotatie van D5/D6 en Q2 in JLC's plaatsingspreview.
