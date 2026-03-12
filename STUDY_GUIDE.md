# Voortgangstoets – Antwoorden per vraag

---

# 1. Project Architectuur

## Leg de volledige hardware-keten uit

De hardware werkt in drie lagen die op elkaar verder bouwen:

1. **4×4 knoppenmatrix** – 16 mechanische knoppen fysiek bedraad in 4 rijen en 4 kolommen. Bij indrukken sluit een rij- en kolomlijn samen.
2. **MCP23S17 (I/O-expander)** – verbonden met de matrix. Stuurt via GPIOA één kolom tegelijk laag en leest via GPIOB welke rijen laag zijn. Communiceert met de STM32 via SPI.
3. **STM32H533 (microcontroller)** – ontvangt de rij/kolom data van de MCP via SPI, verwerkt dit met debouncing, bepaalt welke knop ingedrukt is en stuurt een MIDI-bericht via USB.
4. **Computer / DAW** – ontvangt het MIDI-bericht via USB. De STM32 meldt zich aan als USB Audio Device, geen driver nodig.

## Welke softwarelaag heeft welke verantwoordelijkheid?

| Laag | Verantwoordelijkheid |
|------|----------------------|
| **Applicatielaag** | Bepaalt welke knop overeenkomt met welke MIDI-noot, genereert Note On/Off berichten, beheert debounce logica |
| **HAL-laag** | Abstractie van hardware: biedt functies voor SPI-transmissie, USB-communicatie, GPIO-beheer zonder directe registeraccess |
| **Driverlaag** | Laag-niveau aansturing van de periferie, registers instellen |
| **Opstartcode** | Klokinitialisatie, systeemconfiguratie vóór de main loop |

## Leg de dataflow stap voor stap uit van knopdruk tot MIDI-bericht

1. Knop wordt ingedrukt → rij- en kolomlijn in de matrix sluiten samen
2. STM32 stuurt via SPI een schrijfopdracht naar MCP23S17: kolom 0 laag (GPIOA = `0xFE`)
3. STM32 leest via SPI de rijwaarden van GPIOB
4. Als een rij laag is → die knop in kolom 0 is ingedrukt
5. Herhaal voor kolommen 1, 2, 3
6. Vergelijk met de vorige toestand → is er een verandering?
7. Debounce: wacht totdat de toestand stabiel is (bijv. 20 ms)
8. Toestand veranderd van losgelaten naar ingedrukt → stuur Note On (`0x90`, nootnummer, velocity)
9. Toestand veranderd van ingedrukt naar losgelaten → stuur Note Off (`0x80`, nootnummer, velocity)
10. USB stuurt het MIDI-bericht naar de computer

## Waarom is de initialisatievolgorde belangrijk?

De hardware moet in de juiste volgorde worden opgezet:
1. Eerst **klok en systeem** instellen (HAL_Init, SystemClock_Config)
2. Dan **SPI initialiseren** – zonder werkende SPI kan de MCP23S17 niet worden aangesproken
3. Dan **MCP23S17 initialiseren** – IODIRA/B instellen (richting), GPPUA/B (pull-ups) — zonder dit weet de chip niet wat input of output is
4. Dan pas de **hoofdlus starten** – anders worden knoppen uitgelezen op niet-geconfigureerde hardware

Als je de MCP23S17 probeert te lezen vóór SPI klaar is, of vóór IODIR is ingesteld, krijg je onbetrouwbare of geen data.

## Wat is de functie en werking van de I/O-expander (MCP23S17)?

De MCP23S17 breidt het aantal GPIO-pinnen van de STM32 uit. Met slechts 4 SPI-draden (SCK, MOSI, MISO, CS) krijg je toegang tot 16 extra I/O-pinnen (PORTA en PORTB, elk 8-bit).

In dit project:
- **GPIOA** = uitgang: stuurt kolommen van de matrix aan (één kolom laag tegelijk)
- **GPIOB** = ingang: leest de rijen van de matrix (welke rij is laag = knop ingedrukt)
- Pull-ups op GPIOB zorgen dat niet-ingedrukte rijen standaard hoog zijn

## Wat is het verschil tussen de HAL-laag en de applicatielaag?

| | HAL-laag | Applicatielaag |
|---|---|---|
| **Doel** | Hardware abstractie | Applicatielogica |
| **Wat doet het** | Beheert SPI-registers, USB-periferie, DMA | Bepaalt MIDI-noten, debounce, matrix scan logica |
| **Afhankelijkheid** | Rechtstreeks gekoppeld aan hardware | Gebruikt HAL-functies, onafhankelijk van hardware |
| **Voorbeeld** | `HAL_SPI_Transmit()` | `scan_matrix()`, `send_midi_note()` |

De HAL-laag zorgt dat de applicatielaag niet weet welke registers er achter schuilen — je kan de hardware wisselen zonder de applicatielogica te herschrijven.

---

# 2. MIDI USB Class

## Wat is MIDI en hoe zit de berichtstructuur in elkaar?

**MIDI** = Musical Instrument Digital Interface. Een standaard voor digitale communicatie tussen muziekinstrumenten en computers.
- Vaste datasnelheid: **31.250 bps**
- Elk bericht: **1 statusbyte + 1 of 2 databytes**
- 16 kanalen beschikbaar (0–15)

Structuur:

| Byte | Inhoud |
|------|--------|
| **Statusbyte** | Berichttype + kanaalnummer (hoge nibble = type, lage nibble = kanaal) |
| **Data byte 1** | Nootnummer (0–127) |
| **Data byte 2** | Velocity (0–127) |

Voorbeeld: `0x90 0x3C 0x7F` = Note On, kanaal 0, noot 60 (C4), velocity 127

## Wat is het verschil tussen Note On en Note Off? (inclusief velocity = 0)

- **Note On** (`0x9n`): knop ingedrukt, noot begint te klinken. Velocity geeft de aanslagsterkte aan.
- **Note Off** (`0x8n`): knop losgelaten, noot stopt.
- **Speciale regel:** Een Note On met velocity = 0 wordt ook beschouwd als een Note Off. Sommige apparaten gebruiken dit om bandwidth te sparen.

## Hoe werkt een USB MIDI class? (het apparaat als USB Audio device)

De STM32 meldt zich bij de computer aan via de **USB Audio Device class**. Dit werkt zonder dat de gebruiker een driver hoeft te installeren — het is een standaard USB klasse die door Windows, macOS en Linux rechtstreeks ondersteund wordt.

De **USB descriptor** vertelt de computer:
- Wat voor apparaat het is (Audio/MIDI)
- Hoeveel endpoints er zijn
- Hoe data verstuurd wordt

De firmware regelt het inpakken van MIDI-data in USB-pakketten en het versturen via de USB-endpoint.

## Hoe worden Note On/Off berichten gegenereerd vanuit code?

Wanneer `scan_matrix()` een toestandsverandering detecteert:
- Losgelaten → Ingedrukt: roep de MIDI-verzend functie aan met statusbyte `0x90`, het nootnummer voor die knop, en een velocity (bijv. 127)
- Ingedrukt → Losgelaten: roep de MIDI-verzend functie aan met statusbyte `0x80`, hetzelfde nootnummer, velocity 0 of 64

De USB-laag verpakt vervolgens deze 3 bytes in een USB MIDI-pakket en stuurt het naar de computer.

## Wat is de rol van kanalen, nootnummers en velocity?

- **Kanaal** (0–15): scheidt verschillende instrumenten of spelers. De lage nibble van de statusbyte. Bijv. `0x90` = Note On op kanaal 0, `0x91` = Note On op kanaal 1.
- **Nootnummer** (0–127): welke noot klinkt. Elke knop in de matrix krijgt een vast nootnummer toegewezen. Noot 60 = C4 (middel-C).
- **Velocity** (0–127): hoe hard de knop wordt ingedrukt. Geeft dynamiek aan het geluid. Bij digitale knoppen (aan/uit) wordt vaak een vaste waarde gebruikt (bijv. 127 = maximaal).

---

# 3. SPI Communicatie met de MCP23S17

## Hoe werkt SPI? De 4 lijnen en hun rol

SPI (Serial Peripheral Interface) is een synchrone seriële communicatieprotocol met 4 lijnen:

| Lijn | Naam | Richting | Functie |
|------|------|----------|---------|
| **SCK** | Serial Clock | Master → Slave | Kloksignaal, stuurt timing van databits |
| **MOSI** | Master Out Slave In | Master → Slave | Data van STM32 naar MCP23S17 |
| **MISO** | Master In Slave Out | Slave → Master | Data van MCP23S17 naar STM32 |
| **CS** | Chip Select | Master → Slave | Selecteert de slave (actief laag) |

Data wordt bit per bit verstuurd, gesynchroniseerd met elke klokpuls. Full-duplex: tegelijk zenden en ontvangen.

## Hoe werkt CS (chip select)?

CS is actief laag: de master trekt CS omlaag om communicatie te starten met die specifieke slave. Zolang CS hoog is, negeert de slave alle data op de bus. Na de transactie trekt de master CS weer omhoog.

Dit maakt het mogelijk meerdere slaves op dezelfde SPI-bus te hebben (elk met eigen CS-lijn), zonder dat ze elkaar storen. De MCP23S17 heeft ook hardware-adressering (A0–A2), zodat tot 8 chips tegelijk op één bus kunnen.

## Hoe is een MCP23S17 SPI-transactie opgebouwd? (opcode, register, data)

Elke transactie bestaat altijd uit **3 bytes**:

```
CS laag trekken
Byte 1 → Opcode:          0100 AAA R
Byte 2 → Register adres:  bijv. 0x00, 0x12, 0x13
Byte 3 → Data:            te schrijven waarde / ontvangen waarde
CS hoog trekken
```

**Opcode uitleg:**
- `0100` = vast patroon voor MCP23S17
- `AAA` = hardware adres (A2, A1, A0 pinnen van de chip)
- `R` = 0 voor schrijven, 1 voor lezen

Standaard (A2=A1=A0=0):
- **Schrijven:** `0x40` (0100 0000)
- **Lezen:** `0x41` (0100 0001)

## Hoe configureer je een register? (bijv. IODIRA voor richting instellen)

Voorbeeld: GPIOA instellen als uitgang (kolommen aansturen):

```
CS laag
Stuur 0x40   → schrijf-opcode
Stuur 0x00   → adres van IODIRA register
Stuur 0x00   → 0 = alle pinnen van PORTA zijn output
CS hoog
```

Voorbeeld: GPIOB instellen als ingang (rijen lezen):

```
CS laag
Stuur 0x40   → schrijf-opcode
Stuur 0x01   → adres van IODIRB register
Stuur 0xFF   → 1 = alle pinnen van PORTB zijn input
CS hoog
```

## Hoe schrijf je data naar GPIOA en lees je van GPIOB?

**Schrijven naar GPIOA** (kolom 0 laag sturen):
```
CS laag
Stuur 0x40   → schrijf-opcode
Stuur 0x12   → adres van GPIOA
Stuur 0xFE   → 1111 1110, alleen bit 0 is laag = kolom 0 actief
CS hoog
```

**Lezen van GPIOB** (rijen inlezen):
```
CS laag
Stuur 0x41   → lees-opcode
Stuur 0x13   → adres van GPIOB
Ontvang byte → elke laag bit = die rij heeft een ingedrukte knop
CS hoog
```

## Wat is de functie van de registers in dit project?

| Register | Adres | Gebruik in dit project |
|----------|-------|------------------------|
| **IODIRA** | 0x00 | PORTA als uitgang instellen (kolommen aansturen) |
| **IODIRB** | 0x01 | PORTB als ingang instellen (rijen lezen) |
| **GPPUA** | 0x0C | Pull-ups op PORTA (niet nodig voor uitgangen) |
| **GPPUB** | 0x0D | Pull-ups op PORTB aanzetten: rijen staan standaard hoog, laag = knop ingedrukt |
| **GPIOA** | 0x12 | Kolom selecteren: schrijf 0xFE/0xFD/0xFB/0xF7 om kolom 0/1/2/3 laag te maken |
| **GPIOB** | 0x13 | Rijen inlezen: welke bits laag zijn = ingedrukte knoppen |
| **OLATA** | 0x14 | Output latch PORTA (alternatief voor GPIOA bij schrijven) |
| **OLATB** | 0x15 | Output latch PORTB |

---

# 4. Button Debouncing

## Wat is contact dender en waarom treedt het op?

Contact dender (bouncing) treedt op omdat mechanische knoppen een **metalen verend contact** hebben. Bij indrukken of loslaten stuitert dit contact enkele keren voordat het stabiel is. Dit duurt typisch **5 tot 50 milliseconden**.

In die tijd schakelt het signaal tientallen tot honderden keren snel tussen aan en uit. Elektronisch gezien ziet de microcontroller dit als tientallen afzonderlijke knopdrukken.

## Waarom is debouncing noodzakelijk voor betrouwbare MIDI-uitvoer?

Zonder debouncing registreert één knopdruk als tientallen events. Dit resulteert in:
- Meerdere Note On berichten voor één druk
- Onmiddellijk gevolgd door meerdere Note Off berichten
- Het geluid klinkt foutief, hapert of speelt meerdere noten tegelijk

Met debouncing wordt alleen de **stabiele, definitieve toestand** doorgegeven aan de MIDI-logica.

## Hoe werkt een timer-gebaseerde debounce implementatie?

```c
uint32_t lastDebounceTime = 0;
uint8_t lastButtonState = 0;
uint8_t buttonState = 0;

if (rawReading != lastButtonState) {
    lastDebounceTime = currentTime;  // toestand veranderd, reset timer
}

if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) {
    buttonState = rawReading;  // stabiele toestand: accepteer de waarde
}

lastButtonState = rawReading;
```

**Werking stap voor stap:**
1. Lees de ruwe toestand van de knop
2. Als de ruwe toestand verschilt van de vorige → reset de timer
3. Als de toestand langer dan `DEBOUNCE_DELAY` (bijv. 20 ms) stabiel is gebleven → accepteer als echte toestand
4. Sla de ruwe toestand op voor de volgende vergelijking

## Hoe wordt een toestandsverandering gedetecteerd? (vorige vs. huidige staat)

Bij elke scandonde wordt de **huidige debounced toestand** vergeleken met de **vorige opgeslagen toestand**:

| Vorige toestand | Huidige toestand | Actie | MIDI |
|---------|---------|--------|------|
| Losgelaten (0) | Losgelaten (0) | Geen actie | — |
| **Losgelaten (0)** | **Ingedrukt (1)** | **Knop ingedrukt** | **Note On** |
| Ingedrukt (1) | Ingedrukt (1) | Geen actie | — |
| **Ingedrukt (1)** | **Losgelaten (0)** | **Knop losgelaten** | **Note Off** |

Na de vergelijking wordt de huidige toestand opgeslagen als de nieuwe "vorige toestand".

## Wat is het verschil tussen 'toestand opvragen' en 'verandering detecteren'?

- **Toestand opvragen:** Je leest gewoon of de knop op dit moment ingedrukt is (0 of 1). Dit vertelt niets over wat er voordien was.
- **Verandering detecteren:** Je vergelijkt de huidige toestand met de vorige. Pas als die twee **verschillen** is er iets gebeurd — dan stuur je een MIDI-bericht.

Voor MIDI heb je verandering nodig: je wil exact één Note On op het moment van indrukken, en één Note Off op het moment van loslaten. Niet continu berichten zolang de knop ingedrukt is.

---

# 5. Keyboard Matrix Scanning via SPI met de MCP23S17

## Hoe maakt een 4×4 matrix 16 knoppen mogelijk met slechts 8 I/O-lijnen?

De 16 knoppen zijn gerangschikt in een raster van 4 rijen × 4 kolommen. Elke knop zit op het kruispunt van één rij en één kolom.

- 4 kolommen → 4 I/O-lijnen (uitgangen, via GPIOA)
- 4 rijen → 4 I/O-lijnen (ingangen, via GPIOB)
- Totaal: **8 lijnen** voor 16 knoppen (i.p.v. 16 lijnen bij directe aansluiting)

Door telkens één kolom laag te sturen en dan de rijen te lezen, achterhaal je welk kruispunt actief is = welke knop ingedrukt is.

## Welke poort is voor rijen en welke voor kolommen, en waarom?

- **GPIOA = kolommen (uitgang):** De STM32 moet actief één kolom laag kunnen sturen → output richting (IODIRA = 0x00)
- **GPIOB = rijen (ingang):** De STM32 moet passief lezen welke rijen laag zijn → input richting (IODIRB = 0xFF), met pull-ups aan (GPPUB = 0xFF) zodat niet-ingedrukte rijen standaard hoog zijn

## Leg de volledige scanprocedure stap voor stap uit

1. **Schrijf `0xFE` naar GPIOA** via SPI → kolom 0 laag (bit 0 = 0), kolommen 1–3 hoog
2. **Lees GPIOB** via SPI → elke bit die laag is = rij met ingedrukte knop in kolom 0
3. **Schrijf `0xFD` naar GPIOA** → kolom 1 laag, lees weer GPIOB
4. **Schrijf `0xFB` naar GPIOA** → kolom 2 laag, lees weer GPIOB
5. **Schrijf `0xF7` naar GPIOA** → kolom 3 laag, lees weer GPIOB
6. **Vergelijk** alle gelezen rij-waarden met de vorige toestand
7. **Genereer MIDI** bij verandering (Note On of Note Off)
8. **Sla huidige toestand op** als "vorige toestand" voor de volgende scanronde

## Hoe wordt de MCP23S17 geïnitialiseerd voor matrix scanning?

Bij opstart, via SPI:

```
1. Schrijf 0x00 naar IODIRA (0x00) → GPIOA = alle pins output (kolommen)
2. Schrijf 0xFF naar IODIRB (0x01) → GPIOB = alle pins input (rijen)
3. Schrijf 0xFF naar GPPUB  (0x0D) → pull-ups op GPIOB = rijen standaard hoog
```

Pull-ups zijn essentieel: zonder pull-ups "zweven" de rij-ingangen en lees je willekeurige waarden wanneer geen knop is ingedrukt.

## Hoe sturen SPI-transacties de kolommen aan en lezen ze de rijen in?

**Kolom aansturen (schrijven naar GPIOA):**
```
CS laag → stuur 0x40 (write) → stuur 0x12 (GPIOA) → stuur kolomwaarde → CS hoog
```
Kolomwaarden: `0xFE` (col 0), `0xFD` (col 1), `0xFB` (col 2), `0xF7` (col 3)

**Rijen inlezen (lezen van GPIOB):**
```
CS laag → stuur 0x41 (read) → stuur 0x13 (GPIOB) → ontvang rijbyte → CS hoog
```
In de ontvangen byte: bit = 0 betekent dat die rij laag is = knop ingedrukt (dankzij pull-ups).

## Hoe worden toestandswijzigingen opgeslagen en gebruikt voor MIDI?

Er zijn twee arrays bijgehouden:
```c
uint8_t vorigeToestand[16];   // toestand van vorige scan
uint8_t huidigeToestand[16];  // toestand van huidige scan
```

Na elke volledige scan (alle 4 kolommen):
- Vergelijk `huidigeToestand[i]` met `vorigeToestand[i]` voor elke knop i
- `0 → 1` (losgelaten → ingedrukt): stuur **Note On**
- `1 → 0` (ingedrukt → losgelaten): stuur **Note Off**
- Geen verandering: niets doen
- Kopieer `huidigeToestand` naar `vorigeToestand` voor de volgende ronde

## Wat is ghosting en hoe kan het optreden bij matrix scanning?

**Ghosting** is het fenomeen waarbij de scanner denkt dat een knop ingedrukt is, terwijl dat niet zo is. Dit treedt op wanneer **3 of meer knoppen tegelijk ingedrukt zijn** in een bepaald patroon:

Voorbeeld: knoppen op (rij1,col1), (rij1,col2) en (rij2,col1) zijn ingedrukt. Wanneer col2 laag gestuurd wordt, loopt de stroom via (rij1,col2) → door knop (rij1,col1) → via de connectie terug naar rij2, waardoor het lijkt alsof knop (rij2,col2) ook ingedrukt is — terwijl dat niet zo is.

Oplossing: diodes in serie met elke knop plaatsen om terugstroom te voorkomen. In software is het moeilijk volledig te vermijden.

---

# Snelle Referentie

## MCP23S17 Registers

| Register | Adres | Functie |
|----------|-------|---------|
| IODIRA | 0x00 | Richting PORTA (1=input, 0=output) |
| IODIRB | 0x01 | Richting PORTB |
| GPPUA | 0x0C | Pull-ups PORTA |
| GPPUB | 0x0D | Pull-ups PORTB |
| GPIOA | 0x12 | Lezen/schrijven PORTA |
| GPIOB | 0x13 | Lezen/schrijven PORTB |
| OLATA | 0x14 | Output latch PORTA |
| OLATB | 0x15 | Output latch PORTB |

## MIDI Statusbytes

| Bericht | Statusbyte | Data 1 | Data 2 |
|---------|-----------|--------|--------|
| Note On | `0x9n` | Nootnummer (0–127) | Velocity (0–127) |
| Note Off | `0x8n` | Nootnummer (0–127) | Velocity (0–127) |
| Control Change | `0xBn` | Controller nr. | Waarde |

*n = kanaalnummer (0–15)*

## Kolomselectie matrix

| Kolom | GPIOA waarde | Binair |
|-------|-------------|--------|
| 0 | `0xFE` | 1111 1110 |
| 1 | `0xFD` | 1111 1101 |
| 2 | `0xFB` | 1111 1011 |
| 3 | `0xF7` | 1111 0111 |

---

*Tip: focus op het WAAROM achter elke keuze, niet alleen het WAT. De toets toetst begrip en uitleg, niet het uit het hoofd kennen van code.*

---

## Quick Reference

### MIDI Status Codes
- Note On: `0x90 + channel` (channels 0-15)
- Note Off: `0x80 + channel`
- Control Change: `0xB0 + channel`

### SPI Operations with MCP23S17
- **Write:** Opcode (0x40) → Register Address → Data
- **Read:** Opcode (0x41) → Register Address → (receive Data)

### Matrix Column Selection
- Column 0: `0xFE` (1111 1110)
- Column 1: `0xFD` (1111 1101)
- Column 2: `0xFB` (1111 1011)
- Column 3: `0xF7` (1111 0111)

---

Good luck with your test! Remember: focus on **understanding WHY** things work, not just memorizing code.
