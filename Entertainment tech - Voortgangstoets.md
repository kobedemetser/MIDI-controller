![](./Entertainment%20tech%20-%20Voortgangstoets_images/image-001.png)

**Entertainment technology**

**bachelor in de elektronica – ICT - Brugge**

docent: **Van Gaever T.**

academiejaar **2025-2026**

**Leerstofoverzicht - Voortgangstoets**

| **Onderwerp** | Code begrip &amp; uitleg |
| --- | --- |
| **Onderdelen** | MIDI USB, SPI/MCP23S17, Debouncing, Matrix Scanning |
| **Niveau** | Begrijpen en kunnen uitleggen |

# Project Architectuur

Vooraleer je de individuele onderdelen bestudeert, moet je het volledige project als geheel begrijpen: hoe de hardware en software samenwerken van knopdruk tot MIDI-bericht.

## Hardware Architectuur

Het project bestaat uit drie hardware-lagen die met elkaar verbonden zijn:

| **Component** | **Interface** | **Rol in het project** |
| --- | --- | --- |
| 4x4 Knoppenmatrix | Fysieke bedrading | 16 mechanische knoppen in rijen en kolommen |
| MCP23S17 | SPI (naar STM32) | I/O-expander: leest matrix, stuurt kolommen aan |
| STM32H533 | SPI (naar MCP) + USB | Centrale microcontroller: verwerking &amp; MIDI-uitvoer |
| Computer / DAW | USB (MIDI class) | Ontvangt MIDI-berichten als USB Audio Device |

## Software Architectuur – Lagen

De software is opgebouwd in functionele lagen. Elke laag heeft een duidelijke verantwoordelijkheid.

## Wat moet je kunnen uitleggen?

-   De volledige hardware-keten
-   Welke softwarelaag welke verantwoordelijkheid heeft
-   De dataflow stap voor stap van knopdruk tot verzonden MIDI-bericht
-   Waarom de initialisatievolgorde belangrijk is
-   Functie en werking van de I/O expander
-   Het verschil tussen de HAL-laag (hardware abstractie) en de applicatielaag

# 2\. MIDI USB Class

De MIDI USB class laat een microcontroller toe om als USB-MIDI apparaat te communiceren met een computer. De studenten moeten de volgende concepten begrijpen en kunnen uitleggen:

## Wat is MIDI?

-   MIDI = Musical Instrument Digital Interface
-   Technische standaard voor digitale communicatie tussen muziekinstrumenten, computers en hardware
-   8-bit berichten met een vaste datasnelheid van 31.250 bits per seconde (bps)
-   16 kanalen beschikbaar per MIDI-apparaat (kanalen 0–15, vaak gelabeld als 1–16)

## MIDI Berichtstructuur

Elk MIDI-bericht bestaat uit een statusbyte gevolgd door één of twee databytes:

| **Berichttype** | **Status Byte** | **Data Byte 1** | **Data Byte 2** |
| --- | --- | --- | --- |
| Note On | 0x9n (n = kanaal) | Nootnummer (0–127) | Velocity (0–127) |
| Note Off | 0x8n (n = kanaal) | Nootnummer (0–127) | Velocity (0–127) |
| Control Change | 0xBn | Controller nr. (0–127) | Waarde (0–127) |

## USB MIDI Class (Standard MIDI Class)

-   Een microcontroller kan als USB-MIDI apparaat werken zonder speciale driver te installeren
-   Het apparaat meldt zich aan als een USB Audio Device
-   Compatibel met Windows, macOS en Linux
-   Op de STM32 wordt de USB MIDI class geconfigureerd via de HAL/middleware stack
-   De firmware zorgt voor de USB descriptor en de MIDI-berichtenverwerking

## Wat moet je kunnen uitleggen?

-   Wat MIDI is en hoe de berichtstructuur in elkaar zit (status + data bytes)
-   Verschil tussen Note On en Note Off (inclusief velocity = 0 als Note Off)
-   Hoe een USB MIDI class werkt: het apparaat als USB Audio device
-   Hoe Note On/Off berichten gegenereerd worden vanuit code
-   Wat de rol is van kanalen, nootnummers en velocity

# SPI Communicatie met de MCP23S17

## SPI Protocol Basiskennis

-   SPI = Serial Peripheral Interface – serieel communicatieprotocol
-   4 signaallijnen: SCK (klok), MOSI (master out), MISO (master in), CS/SS (chip select)
-   Synchrone communicatie: data wordt gesynchroniseerd met de klok
-   Full-duplex: tegelijkertijd zenden en ontvangen mogelijk
-   CS actief LAAG: de slave wordt geselecteerd door CS laag te trekken

## De MCP23S17 I/O Expander

De MCP23S17 is een 16-bit I/O expander met SPI-interface. Kenmerken:

-   Twee 8-bit poorten: PORTA (GPA0–GPA7) en PORTB (GPB0–GPB7)
-   Slechts 4 SPI-draden nodig voor 16 extra GPIO's
-   Interne pull-up weerstanden (100 kΩ) per pin configureerbaar
-   Hardware adressering via A0–A2 pinnen: tot 8 chips op één SPI-bus

## 3.3 SPI Transactiestructuur met MCP23S17

Elke SPI-transactie naar de MCP23S17 bestaat uit 3 bytes:

-   Byte 1 – Opcode: 0100 AAA R (AAA = adres A2-A0, R = 0 schrijven / 1 lezen)
-   Byte 2 – Register adres: bijv. 0x00 (IODIRA), 0x12 (GPIOA), 0x13 (GPIOB)
-   Byte 3 – Data: te schrijven waarde of ontvangen data bij lezen

Standaard opcodes (A2=A1=A0=0):

Write: 0x40 (0100 0000)

Read: 0x41 (0100 0001)

## 3.4 Belangrijke Registers

| **Register** | **Adres** | **Functie** |
| --- | --- | --- |
| IODIRA | 0x00 | I/O richting PORTA (1 = input, 0 = output) |
| IODIRB | 0x01 | I/O richting PORTB |
| GPPUA | 0x0C | Pull-up activatie PORTA |
| GPPUB | 0x0D | Pull-up activatie PORTB |
| GPIOA | 0x12 | GPIO waarden lezen/schrijven PORTA |
| GPIOB | 0x13 | GPIO waarden lezen/schrijven PORTB |
| OLATA | 0x14 | Output latch PORTA |
| OLATB | 0x15 | Output latch PORTB |

## 3.5 Wat moet je kunnen uitleggen?

-   Hoe SPI werkt: de 4 lijnen en hun rol
-   Hoe CS (chip select) werkt
-   De opbouw van een MCP23S17 SPI-transactie (opcode, register, data)
-   Hoe je een register configureert (bijv. IODIRA voor richting instellen)
-   Hoe je data schrijft naar GPIOA en leest van GPIOB
-   Uitleggen wat de functie is van de registers voor het project (registers niet vanbuiten kennen, je mag de tabel gebruiken)

# Button Debouncing

## Wat is contact dender (bouncing)?

Wanneer een mechanische drukknop wordt ingedrukt of losgelaten, maakt het contact niet onmiddellijk een stabiele verbinding. Door de veerkracht van het metaal ontstaan er meerdere snelle aan/uit schakelingen (bounces) voordat het signaal stabiel wordt. Dit noemen we 'contact dender' of bouncing.

-   Een drukknop kan tientallen tot honderden keer schakelen binnen enkele milliseconden
-   Zonder debouncing worden meerdere events geregistreerd voor één enkele knopdruk
-   Dit veroorzaakt fouten: meerdere MIDI-berichten voor één knopdruk

## 4.2 Software Debouncing Methoden

De meest gebruikte software-aanpak is de tijdsvertraging methode:

-   Meet de tijd tussen twee toestandsveranderingen
-   Accepteer de nieuwe toestand pas na een stabiele periode (typisch 5–50 ms)
-   Gebruik millis() of een timer voor de tijdsmeting
-   Sla de vorige toestand op en vergelijk bij elke scanronde

Pseudocode voor debounce logica:

uint32\_t lastDebounceTime = 0;

uint8\_t lastButtonState = 0;

uint8\_t buttonState = 0;

if (rawReading != lastButtonState) {

lastDebounceTime = currentTime;

}

if ((currentTime - lastDebounceTime) > DEBOUNCE\_DELAY) {

buttonState = rawReading; // stabiele toestand

}

lastButtonState = rawReading;

## Toestandsdetectie voor MIDI

Voor MIDI is het niet de toestand op zich die belangrijk is, maar de verandering van toestand:

| **Vorige Toestand** | **Huidige Toestand** | **Actie** | **MIDI Bericht** |
| --- | --- | --- | --- |
| Losgelaten (0) | Losgelaten (0) | Geen actie | — |
| Losgelaten (0) | Ingedrukt (1) | Knop ingedrukt | Note On (0x90, noot, velocity) |
| Ingedrukt (1) | Ingedrukt (1) | Geen actie | — |
| Ingedrukt (1) | Losgelaten (0) | Knop losgelaten | Note Off (0x80, noot, velocity) |

## 4.4 Wat moet je kunnen uitleggen?

-   Wat contact dender is en waarom het optreedt bij mechanische knoppen
-   Waarom debouncing noodzakelijk is voor betrouwbare MIDI-uitvoer
-   Hoe een timer-gebaseerde debounce implementatie werkt
-   Hoe toestandsverandering wordt gedetecteerd (vorige vs. huidige staat)
-   Het verschil tussen 'toestand opvragen' en 'verandering detecteren'

# Keyboard Matrix Scanning via SPI met de MCP23S17

## 5.1 Werking van een Knoppenmatrix

Een 4x4 knoppenmatrix combineert 16 knoppen met slechts 8 I/O-lijnen (4 rijen + 4 kolommen):

-   Door één kolom tegelijk laag te sturen en dan de rijen te lezen, detecteer je welke knop ingedrukt is
-   Elke combinatie van rij + kolom identificeert een unieke knop (16 knoppen totaal)

## 5.2 Scanprocedure Stap voor Stap

De scanprocedure via SPI:

-   Stap 1: Schrijf 0xFE naar GPIOA via SPI (kolom 0 laag, rest hoog)
-   Stap 2: Lees GPIOB via SPI (welke rijen zijn laag = welke knoppen ingedrukt in kolom 0)
-   Stap 3: Herhaal voor kolommen 1, 2 en 3 (0xFD, 0xFB, 0xF7)
-   Stap 4: Vergelijk met vorige toestand en genereer MIDI bij verandering
-   Stap 5: Sla huidige toestand op als 'vorige toestand' voor de volgende scanronde

## C Implementatie Structuur

De kerncomponenten van de matrixscanner in C:

| **Component** | **Doel** |
| --- | --- |
| SPI Initialisatie | Configureer SPI: mode 0, MSB first, kloksnelheid instellen |
| MCP23S17 Init | Stel IODIR en GPPU registers in via SPI bij opstart |
| mcp_write(reg, data) | Schrijf een byte naar een register via SPI |
| mcp_read(reg) | Lees een byte van een register via SPI |
| scan_matrix() | Loop door kolommen, lees rijen, detecteer veranderingen |
| Toestand Arrays | uint8_t vorige[16], huidige[16] voor change detection |
| MIDI Output | Stuur Note On/Off bij toestandsverandering |

## Wat moet je kunnen uitleggen?

-   Hoe een 4x4 matrix 16 knoppen met 8 I/O-lijnen mogelijk maakt
-   Welke poort voor rijen en welke voor kolommen gebruikt wordt en waarom
-   De volledige scanprocedure stap voor stap
-   Hoe de MCP23S17 geïnitialiseerd wordt voor matrix scanning
-   Hoe SPI-transacties de kolommen aansturen en rijen inlezen
-   Hoe toestandswijzigingen worden opgeslagen en gebruikt voor MIDI-berichten
-   Wat ghosting is en hoe het kan optreden bij matrix-scanning

**Tip voor de toets:**

Zorg dat je niet enkel weet wat iets doet, maar ook waarom het zo werkt. Deze overhoring toetst begrip en het kunnen uitleggen van de geschreven code, niet het uit het hoofd kennen van code.