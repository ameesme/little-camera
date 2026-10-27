# little camera v3p — verificatie vóór bestelling (2026-09-30)

## 1. Fabricagebestanden tegen ontwerp-intentie (onafhankelijk van KiCad)

De gerbers en drillfiles die naar JLC gaan (`little_camera_v3p_gerbers.zip`) zijn met een eigen reader exact als vectorgeometrie ingelezen. Er is geen KiCad-data gebruikt voor de verbindingen:

- 4 koperlagen;
- 202 doorgemetaliseerde gaten, waarvan 4 sleuven;
- de boardcontour.

De methode:

- Het koper op elke laag is opgedeeld in aaneengesloten stukken. Gaten koppelen de lagen alleen waar koper de gatwand raakt.
- Elke pad is als meetpunt gebruikt.
- Het verwachte net per pad komt uit `design.py`, de bron voor het schema.

| Check | Resultaat |
|---|---|
| Opens: elk net precies één koperstuk | **60/60 nets, 0 opens** |
| Shorts: geen koperstuk met twee nets, of met een NC-pad | **0 shorts** |
| Zwevend koper (koper zonder pad) | **0** |
| Min. koper–koper tussen verschillende nets (F / In1 / In2 / B) | 0,1286 / 0,1288 / 0,1286 / 0,1287 mm (regel 0,127) |
| Min. koper–boardrand | 0,30 mm; uitzondering: USB-C-signaalpads 0,21 mm tot de uitsparing (bewust, volgens de fabrikanttekening; JLC-min. 0,2) |
| Min. annular ring van de vias | 0,124 mm (0,5/0,25-via, nominaal 0,125) |
| Min. gatwand tot koper van een ander net (alle lagen) | 0,253 mm |
| IPC-D-356-netlist (`little_camera_v3p.ipc`) tegen het ontwerp | 0 verschillen; alleen de mechanische pads van J2/J3 en BZ1-pad 3 zijn N/C, zoals bedoeld |
| KiCad DRC | 0 fouten, behalve de 16 J1-pad-tot-uitsparing-meldingen die de projectregel `.kicad_dru` toestaat; 0 niet-verbonden pads |

**Controle van de checker zelf.** In een kopie van de geometrie is expres een spoor van CHG_STAT doorgeknipt: dat gaf "OPEN". Daarna is expres een brugje van 0,1 mm tussen twee nets gelegd: dat gaf "SHORT". De checker ziet dus echte fouten.

**Aanpassing na deze check.** De GND-vulling stond op 0,2 mm van de boardrand. Die is teruggezet naar 0,3 mm en de gerbers zijn opnieuw gemaakt. De getallen hierboven gelden voor de nieuwe zip.

## 2. Pinouts en plaatsing tegen JLC/LCSC-bibliotheek en datasheets

Voor elk gepolariseerd onderdeel of onderdeel met meerdere pinnen is JLC's eigen footprint en symbool van exact hetzelfde LCSC-nummer opgehaald. Daarbij geldt: padnummer = datasheetpin.

Per onderdeel is gecheckt:

- **Pinfuncties:** de pinfuncties uit JLC's symbool komen overeen met `design.py`. Dat is gedaan voor ESP32-S3-WROOM-1, TP4054, TLV755P DQN, TPS61222, AP2112K, 74LVC1G14, AO3401A, 2N7002W, B5819W, 1N4148WS, LED KT-0603R, MLT-5020 en USB-C.
- **Footprint-geometrie:** onze pads vallen per padnummer, na rotatie, precies op JLC's pads, dus pinvolgorde en spiegeling kloppen.
- **Plaatsing (CPL):** JLC's footprint is gesimuleerd op de positie en rotatie uit de CPL. Elke pad landt ≤ 0,21 mm van onze pad met dezelfde functie. Bij U1 geldt dat voor alle 40 signaalpads (≤ 0,03 mm); de EP valt in hetzelfde GND-vlak.

**Gevonden en opgelost in de CPL** (`gen/mkjlc.py` heeft nu `ROT_OFFSET` en `CENTROID`):

- **J2, camera-FPC:** JLC's oorsprong ligt 3,6 mm van de onze. De connector zou 3,6 mm verschoven geplaatst worden. Er is een positiecorrectie toegevoegd en de rotatie gaat van 180 naar 0.
- **Rotaties waarin JLC's bibliotheek anders staat dan KiCad:**
  - U2, U7, U8, Q1, Q2: +180°
  - U10: +270°
- **LED's:** JLC nummert ze andersom (1 = A), maar de kathode ligt fysiek aan dezelfde kant als bij ons. Er is geen correctie nodig.

**Overig gecontroleerd tegen de datasheets:**

- **D2MQ-1:** contact form COM – NO – NC, dus de middelste pin is NO. Middelste pin = SHUTTER en beide buitenpinnen = GND is correct: in rust hoog, ingedrukt laag.
- **LS027B7DH01 (Sharp-tekening, fig. 8-1):**
  - Pin 1 ligt rechts in vooraanzicht (displaykant) met de staart naar beneden. Rond de onderrand gevouwen komt pin 1 dus links in componentaanzicht. Dat klopt met J3, waar pad 1 = SCLK links ligt.
  - De contactzone is 5,5 mm breed en gecentreerd op 31,4 mm van de glasrand, precies waar J3 staat.
  - Pinfuncties 1–10: SCLK, SI, SCS, EXTCOMIN, DISP, VDDA, VDD, EXTMODE, VSS, VSSA.
- **Strapping-pinnen ESP32-S3:**
  - IO0: 10 k pull-up.
  - IO45: 100 k pull-down (VDD_SPI 3,3 V, nodig voor N16R8).
  - IO46: ~101 k pull-down via R27/R28.
  - IO3: alleen relevant bij een gebrande eFuse.
  - EN: RC 10 k / 1 µF.
- **USB:** D−/D+ gaan naar IO19/IO20, beide orientaties zijn doorverbonden, en CC1/CC2 hebben 5,1 k naar GND.

## 3. Open punten (niet met documenten te bewijzen of een ontwerpkeuze)

1. **Camera-flex spiegeling (belangrijkste).** J2 gaat ervan uit dat bij de module (lens naar boven, flex richting bovenrand) pin 1 van de ESP32-CAM-volgorde (pin 2 = AGND, 3 = SIO_D, 4 = AVDD) aan de rechterkant ligt in componentaanzicht. De lus over J2 houdt de x-volgorde gelijk. De datasheets van OV2640-modules tonen niet aan welke kant pin 1 fysiek zit, dus dit is alleen met de echte module te bevestigen:
   - Zoek met een multimeter (piepstand) welk uiteinde van de flex contact 2 en contact 15 onderling doorverbonden heeft: AGND en DGND. Tel daarbij de contacten vanaf dat uiteinde.
   - Leg de module met de lens naar boven op zijn plek, met de lus over J2. Dat "pin 1-uiteinde" moet aan de **rechterkant** (richting USB-kant) uitkomen.
   - Komt het links uit, dan moet J2 gespiegeld worden **vóór** bestelling, anders krijgt de sensor spanning op de verkeerde pinnen.
2. **Laadstroom.** Met R5 = 3,3 k laadt de TP4054 met ~300 mA. Dat is ≈ 1C voor een 403030-cel (~300 mAh): toegestaan maar aan de bovenkant. Advies: R5 = 5,1 k (~200 mA, zelfde Basic-part C25905). Dat is alleen een BOM-wijziging, geen routingwijziging.
3. **Laatste controle bij JLC:** kijk bij het bestellen nog in JLC's plaatsingspreview. De rotaties en offsets staan nu goed in de CPL, maar JLC's operator werkt met dezelfde bibliotheek.
