# Verificatie v3q — 3 oktober 2026

De genoemde wijzigingen staan in het opgeslagen schema, de PCB en de actuele BOM. De elektrische verbindingen komen overeen. **De camerakern op 1,2 V blijft niet goedgekeurd tegen de beschikbare OV2640-specificatie.** Verder zijn er plaatsings- en USB-layoutpunten, en zijn opstartgedrag en werkelijke VCOM-frequentie nog niet gemeten. Ontwerp- en fabricagebestanden zijn niet aangepast tijdens deze review.

## Resultaten per wijziging

| Wijziging | Verificatie |
|---|---|
| EXTMODE → VDISP | J3/8 is in schema en PCB verbonden met J3/6, J3/7 en U7/VOUT. Correct volgens Sharp. |
| Boost altijd ingeschakeld | U7/6 EN is verbonden met VBAT, net als U7/1 VIN. |
| U8 op 3,3 V | U8/5 en C19/1 liggen op +3V3; U8/3 is GND. |
| R18 en R29 verwijderd | Afwezig in schema, PCB en BOM. U1/8 (GPIO15) is bewust NC. |
| DISP op IO7 | GPIO7 → R25=330 Ω → J3/5. Laag maakt het beeld wit; geheugen en voeding blijven actief. |
| VBUS-deler 33k/51k | R8=33 kΩ vanaf VBUS; R9=51 kΩ naar GND. 3,339 V nominaal bij 5,5 V; maximaal 3,365 V bij tegengestelde 1%-toleranties. |
| R5=5,1k | In schema/PCB/BOM; nominale laadstroom 196,1 mA. |
| VCOM circa 6 Hz | R19=200 kΩ, C18=1 µF. Met de gebruikte typische K=0,8: 6,25 Hz. Dit is een berekening, geen meting of gegarandeerde band van 5–7 Hz. |
| Q2=DMG1012UW-7 | In schema/PCB/BOM, C156390. G=1, S=2 naar GND, D=3 naar BUZZ_DRV. Diodes specificeert maximaal 0,6 Ω bij VGS=2,5 V: geschikte keuze voor deze 3,3 V-aansturing. |
| C20/C21 dichterbij | Van voedingspad tot voedingspad: C21=6,02 mm, C20=8,09 mm. Beide hangen aan +3V3/GND. |
| USB-ESD toegevoegd | D5/1 op D+, D6/1 op D−; beide pin 2 naar GND. De lokale referentiebibliotheek benoemt pin 1 als C en pin 2 als A. BOM gebruikt **BORN** ESD9L5.0ST5G-N, C316041, geen onsemi-onderdeel. |
| Camerakern 1,2 V | U6 blijft TLV75512PDQNR → J2/10. Ongewijzigd; zie open punt hieronder. |

[Sharp-specificatie](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/1272/LS027B7DH01_Rev_Jun_2010.pdf), [DMG1012UW-datasheet](https://www.diodes.com/datasheet/download/DMG1012UW.pdf), [BORN-datasheet, fabrikantdocument bij ChipDip](https://static.chipdip.ru/lib/496/DOC013496643.pdf).

De BORN-diode heeft 0,6 pF typische en 0,9 pF maximale capaciteit, geschikt als lage-capaciteitskeuze. Zijn opgegeven klemspanning is 13 V bij 3 A; de aanwezigheid van de diode bewijst daarom niet dat de ESP32 elke ESD-test overleeft. Dat hangt ook van de stroomweg en parasitaire inductie af.

## Schema, routing en fabricage-export

- ERC: **0 fouten, 0 waarschuwingen**.
- Geëxporteerde schemanetlijst tegenover opgeslagen PCB: **272 pin/net-toewijzingen, 0 verschillen**. Bewuste NC-netnamen zijn genormaliseerd naar onverbonden PCB-pads.
- Schema tegenover `gen/design.py`: **0 verschillen**.
- BOM tegenover gedeclareerde waarden: **0 verschillen**.
- Bestaande geometrische PCB-connectiviteitschecker: **58/58 nets met meerdere pads verbonden**, 0 onderbrekingen. Deze checker gebruikt benaderde geometrie; KiCad rapporteert daarnaast 0 unconnected items en geen shorts.
- Verse KiCad DRC met in-memory zone-refill: **18 fouten en 89 waarschuwingen**.

De Gerber-zip `fab/jlcpcb/little_camera_v3q_gerbers.zip` is vergeleken met een verse export van de opgeslagen PCB, in een tijdelijke map. **Alle 13 bestanden hebben identieke teken-/boorcommando's**, nadat datumregels en generation-softwaremetadata zijn genegeerd: vier koperlagen, maskers, pasta, silkscreen, contour, PTH en NPTH. Het gaat om een commandotekstvergelijking: ook aperturen en netattributen zitten in die vergelijking.

Dit bevestigt dat de zip de huidige opgeslagen PCB-export vertegenwoordigt. **Aanvullend is nu ook een onafhankelijke reconstructie van koper-eilanden, opens en shorts uit de Gerber-geometrie uitgevoerd.** Alle 58 nets met meerdere elektrische pinnen zijn verbonden via het gereconstrueerde koper en 213 plated gaten; er zijn geen opens, shorts of zwevende koper-eilanden gevonden. Een herhaling met fijnere geometrie geeft dezelfde connectiviteit en bewust geïnjecteerde proefdefecten worden gedetecteerd. Methode, meetwaarden en beperkingen staan in [de aanvullende Gerber-review](gerber_v3q/review.md).

## Resterende bevindingen

### 1. Camerakern: 1,2 V blijft buiten de gevonden specificatie

OmniVision OV2640 v2.2 Table 6 geeft DVDD **1,24–1,36 V**, typisch 1,3 V. De revisiegeschiedenis vermeldt expliciet dat de oudere 1,2 V-specificatie is gewijzigd. U6 op 1,2 V voldoet daar niet aan, ook niet met zijn positieve uitgangstolerantie. Het kan praktisch werken op een bepaalde module, maar zonder exacte module-/sensorinformatie is het geen geverifieerde keuze. [OV2640-fabrikantdatasheet](https://www.welectron.com/mediafiles/productimg/arducam/OV2640_DS.pdf).

De fysieke camera-flexrichting en batterijtype/protectie zijn eveneens nog niet bevestigd. De verlaagde laadstroom is correct berekend, maar niet automatisch toegestaan voor iedere cel.

### 2. Twee nieuwe courtyard-overlaps rond U1

DRC meldt **U1–C20 en U1–C21**. De component-body-bounding-boxen overlappen niet: C20 ligt ongeveer 1,31 mm en C21 ongeveer 1,43 mm naast de modulebody. Hun pads liggen ook buiten de bestaande antennekeepout. Het betreft dus gereserveerde plaatsingsruimte, niet een aangetoonde fysieke botsing of kortsluiting. Maak de vrijgave expliciet: plaatsing/assemblageruimte controleren en de courtyard correct modelleren of de overlap bewust beoordelen. De condensatoren staan dicht naast het antennegedeelte; RF-prestaties in de complete behuizing blijven een meetpunt.

### 3. USB-ESD: elektrisch aanwezig, routing nog verbeteren

D5 en D6 liggen respectievelijk ongeveer **11,4 en 9,4 mm** van de dichtstbijzijnde connector-datapad. Een spoorgraph op basis van segment-eindpunten schat aftakkingen naar de diodes van ongeveer **12,7 mm voor D+ en 4,8 mm voor D−**. De connector→MCU-routes zijn ongeveer **94,7 en 62,1 mm**; dat zijn benaderde tracklengtes met padinterne lengtes verwaarloosd. Deze ongelijke, afgetakte routing is geen bewijs van USB-uitval, maar beperkt de overtuigingskracht van de beschermingswijziging.

Plaats de beveiliging zo dicht mogelijk bij J1, voer de datalijn langs de beveiligingspad en geef de diode een korte retour naar het grondvlak. Controleer de paarimpedantie en return paths. [Espressif USB-layoutaanwijzingen](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/pcb-layout-design.html).

### 4. De 16 bestaande USB-cutout-clearancefouten blijven

J1-pads hebben 0,210 mm tot de contour tegen de globale 0,300 mm-regel. De huidige 0,200 mm-exceptie heft dat niet op. Dit zijn dezelfde 16 fouten als eerder, geen nieuwe verbindingproblemen. De tijdelijke regeltest uit de eerste audit liet zien hoe een globale 0,200 mm-minimumwaarde plus een algemene 0,300 mm-customregel en specifieke J1-exceptie ze kan oplossen. De productieprojectregels zijn tijdens deze verificatie niet aangepast.

### 5. Voedingsgrenzen en sequencing blijven relevant

De nieuwe VBUS-deler heeft voldoende bovenmarge wanneer +3V3 normaal gereguleerd is. De ESP32-grens is echter **VDD+0,3 V**, geen vaste 3,6 V bij afwezige of instortende voeding. Bij inschakelen verschijnt VBUS_SENSE vóórdat U10 klaar is; de lagere 33 kΩ-bovenweerstand beperkt injectiestroom minder dan voorheen. Beoordeel die toestand apart. [Espressif module-inputspecificatie](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf).

U7 draait rechtstreeks op VBAT, U8 op de LDO-uitgang. Daarmee is 'VCOM altijd loopt' alleen geldig zolang +3V3 voldoende hoog blijft. Bij lege batterij kan de boost nog 5 V leveren terwijl oscillatorvoeding en VCOM-logicaniveau niet meer geldig zijn. Bij USB zonder batterij kan de charger-BAT-uitgang ook ander gedrag vertonen. Controleer normale opstart, brownout en ondersteunde batterijloze toestand; voorkom een zichtbaar display zonder geldige inversie. DISP laag blankt het beeld maar verwijdert de displayvoeding niet.

De eerdere aandachtspunten voor AP2112-verwarming, USB-totaalstroom/inrush, batterijprotectie en cameravoedingsvolgorde zijn niet door deze wijzigingslijst volledig bewezen opgelost. Er is geen firmware- of hardwaremeting uitgevoerd. Meet ook VCOM-frequentie en signaalniveaus met de werkelijk bestelde TECH PUBLIC U8; een Nexperia-typische formule is geen gegarandeerde specificatie voor die fabrikant.

## Bewijsbestanden

- `verify_v3q_summary.json`: meetbare bestandsresultaten en SHA-256-hashes.
- `verify_v3q_netlist.xml`: verse schemanetlijst.
- `verify_v3q_drc.json`: volledige DRC en paritymeldingen.
- `verify_v3q_fab_comparison.json`: vergelijking van alle zip-leden met de verse export.

De 96 paritymeldingen zijn 80 Description-verschillen, 4 PWR_FLAG-footprints en 12 bewuste NC-netnamen. De onafhankelijke functionele pin/netvergelijking vindt geen verschil. De 89 DRC-waarschuwingen zijn 63 silk-over-copper, 7 silk-edge, 12 text-height, 2 silk-overlap en 5 library-footprint-mismatch.
