# Architectuur

## Waarom dit document bestaat

Native Minecraft-clients sterven niet aan hun renderer. Ze sterven aan
protocolonderhoud: elke nieuwe versie schuift pakket-ID's en veldindelingen op, en
een codebase die daar niet op gebouwd is moet bij elke release half herschreven
worden. AltCraft staat nog op 1.12.2, Polymer kan alleen kijken. Daarom is
versie-isolatie hier geen latere opschoning maar het eerste ontwerpbesluit.

## Het bewijs dat dat nodig is

Gemeten tussen protocol 774 (1.21.11) en 776 (26.2), uit Mojangs eigen
pakketrapporten:

| Fase                   | Pakketten | Verschillende ID's |
|------------------------|-----------|--------------------|
| login, clientbound     | 6         | 0                  |
| login, serverbound     | 5         | 0                  |
| configuratie, beide    | 30        | 0                  |
| play, clientbound      | 139       | **102**            |
| play, serverbound      | 66        | **68 van 69**      |

Login en configuratie liggen stil; de play-fase schuift bijna volledig op. Een
client die één ID-tabel hardcodeert werkt dus precies één versie.

En het zit niet alleen in de ID's. `accept_teleportation` was in 774 en 776 één
VarInt; in 26.3 kwamen er positie- en rotatievelden bij. Layouts zijn net zo
versiegebonden als nummers.

## Waar de nummers vandaan komen

Niet uit de wiki, en niet met de hand. `tools/gen_packet_ids.py` leest het
`reports/packets.json` dat de officiële server-jar zelf genereert:

```
java -DbundlerMainClass=net.minecraft.data.Main -jar server.jar --reports
python3 tools/gen_packet_ids.py 774=774/packets.json 776=776/packets.json \
    > src/proto/packet_ids_gen.cpp
```

Dat is observeerbare uitvoer van het spel, geen gedecompileerde code. Het is ook
aantoonbaar betrouwbaarder dan documentatie: de wiki beschreef tijdens het bouwen
`select_known_packs` als 0x0F en `code_of_conduct` als 0x14, terwijl 774 en 776
0x0E en 0x13 gebruiken. De wiki liep op een nieuwere snapshot vooruit. Wie die
nummers had overgetypt, had een client gebouwd die nergens inlogt.

Een versie toevoegen is dus: jar pakken, rapport genereren, generator opnieuw
draaien. Geen handwerk, geen vergeten pakket.

Veldindelingen komen wél uit de protocolspec, maar worden tegen echte bytes
gecontroleerd. `player_chat` is zo uitgelezen, en daar kwam een detail uit dat in
geen enkele spec staat: Mojangs NBT-codec wikkelt een los getal in een compound
onder de lege sleutel, dus `{"": 5}` in plaats van `5`. Zonder die vondst staat er
"There are  of a max of  players online".

## Lagen

```
byte_buffer     wire-formaat: big-endian ints, LEB128 varints, strings
tcp_socket      blokkerende socket, TCP_NODELAY, timeouts
packet_io       framing en zlib-compressie
json / nbt      tekstcomponenten in beide vormen
translations    Minecrafts taalbestand, %s en %N$s
proto/          per versie een gegenereerde ID-tabel
session         de toestandsmachine: handshake -> login -> configuratie -> play
features        wat de speler aan en uit kan zetten
```

Alleen `session` kent versies. De lagen eronder zijn versieloos; de laag erboven
(de renderer, mijlpaal 3) wordt dat ook.

## Wat de sessie doet

Handshake met het echte protocolnummer, dan login. Compressie wordt ingesteld
zodra de server er een drempel voor stuurt. Onbekende login-pakketten worden niet
genegeerd maar afgewezen met een leesbare fout, want stil doorgaan op een pakket
dat je niet begrijpt is precies hoe je uren kwijtraakt.

In de configuratiefase melden we nul bekende packs, zodat de server de volledige
registers stuurt in plaats van ervan uit te gaan dat wij ze al hebben.
Resource packs worden geweigerd, gedragsregels worden doorgegeven aan de UI.

In de play-fase antwoorden we op keep-alive, ping en teleport-bevestigingen, en
lezen we chat in beide vormen (`system_chat` en `player_chat`).

## Wat er nog niet is

- **Online-mode.** Een server in online-mode vraagt versleuteling; dat heeft een
  Microsoft-sessie nodig. Zie `docs/PRODUCT.md` voor de strategie daarvoor. De
  client zegt dit nu met zoveel woorden in plaats van te crashen.
- **Beweging.** We sturen geen positie. De vanilla server eist `client_tick_end`
  vóór een positiepakket, dus dat gaat samen met de spelerssimulatie.
- **Wereld.** Chunks, blokken, entiteiten: mijlpaal 3, samen met de renderer.
- **1.8.9.** Protocol 47 heeft geen configuratiefase. De tabel heeft daar al
  ruimte voor: een ontbrekend pakket is -1 in plaats van een gat in de code.

## Chunks

Een chunk komt binnen als `level_chunk_with_light`: coordinaten, heightmaps, een
blob met secties, block entities en lichtdata. Elke sectie bevat het aantal
niet-lucht-blokken en twee paletted containers, een voor blokken en een voor
biomen.

Een paletted container is bit-gepakt in longs, en entries steken **nooit** over
een long-grens: bij 5 bits passen er 12 in een long en blijven de bovenste 4
bits padding. Drie vormen: `bits == 0` is één waarde voor de hele sectie,
`bits <= 8` (blokken) of `<= 3` (biomen) is indirect met een palet, en daarboven
staan de blokstaat-ID's direct in de data.

Twee dingen die de spec niet (duidelijk) zegt en die uit de bytes kwamen:

1. **De data-array van een paletted container heeft geen lengte-prefix.** De
   heightmaps erboven wel. De lengte van de container-data volgt uit het aantal
   entries en de bits per entry. Wie hier een varint leest, leest de eerste long
   als lengte en loopt vast.
2. **Het aantal secties staat niet in het pakket.** Dat volgt uit de hoogte van
   de dimensie, die in een register staat dat wij niet bijhouden. Secties lezen
   tot de blob leeg is heeft dat register niet nodig.

### Hoe dit gecontroleerd is

Dat het parst zonder uitzondering bewijst niets: verschoven bit-unpacking geeft
plausibel uitziende onzin. De controle is de blokverdeling van 329 chunks echte
overworld, met de ID's omgezet naar namen via `blocks.json`:

| blokstaat | blokken | blok |
|-----------|---------|------|
| 0 | 21.939.649 | air |
| 27722 | 4.247.954 | deepslate |
| 1 | 2.773.842 | stone |
| 86 | 1.157.748 | water |
| 23250 | 351.876 | tuff |
| 85 | 253.914 | bedrock |

Deepslate onder stone, de steensoorten in de juiste verhouding, en bedrock op
253.914 blokken = 256 x 3,0 lagen per chunk, wat precies klopt met het
willekeurige patroon van ongeveer drie lagen diep. Die getallen kunnen niet per
ongeluk kloppen.

## Blokmodellen

Dit is het stuk dat ik in de planning onderschatte, dus het is als eerste van
mijlpaal 3 uitgezocht: het heeft geen GPU nodig en is volledig te controleren.

Een blokstaat wordt geometrie in vier stappen:

1. `blocks.json` geeft staat-ID naar bloknaam plus eigenschappen.
2. `assets/minecraft/blockstates/<blok>.json` kiest een model. Twee vormen:
   `variants` matcht op de hele eigenschappenset, `multipart` verzamelt alle
   delen waarvan de `when`-voorwaarde klopt (AND over sleutels, OR over
   `|`-waarden, plus geneste `OR`/`AND`).
3. Het model erft via `parent`. Texturen mergen kind-eerst; `elements` komen van
   de naaste voorouder die ze definieert. Textuurnamen als `#all` worden door de
   gemergede map gevolgd tot een echt pad.
4. De variant levert nog `x`/`y`-rotatie en `uvlock` mee.

`tools/resolve_block_models.py` doet dit voor alle 29.671 staten van 1.21.11 en
leest alleen assets uit een eigen installatie.

### Uitkomst, en waarom "leeg" niet hetzelfde is als "fout"

| | staten |
|---|---|
| met geometrie | 26.706 (356.952 quads, 13,4 gemiddeld) |
| zonder model: lucht, vloeistoffen, barrier, light | 84 |
| legitiem leeg | 2.881 |
| onopgelost | 0 |

Die 2.881 zijn nagelopen in plaats van weggewuifd, want een onzichtbaar blok is
precies het soort fout dat je pas maanden later ziet. Drie groepen:

- **Block entities.** Borden, banieren, bedden, kisten, shulkers, koppen,
  conduit, end_portal, decorated_pot, copper_golem_statue. Die worden in vanilla
  door een eigen renderer getekend; hun model bevat alleen een particle-textuur.
  Dat is een aparte renderpad die nog moet komen.
- **Onbereikbare muurstaten.** `cobblestone_wall` heeft 324 staten met
  geometrie; alleen de combinatie `up=false` met niets verbonden is leeg, en die
  bestaat in het register maar nooit in de wereld, want de server zet `up` dan
  op true.
- **Onvolgroeide gewassen.** `pitcher_crop` half=upper is leeg in stadium 0-2 en
  heeft 4 quads in 3-4. Dat is geen bug maar precies wat vanilla doet: de plant
  is nog niet hoog genoeg om in het bovenste blok te komen.

## De voorgebakken blokkentabel

Vanilla leest bij het opstarten 1168 blockstate-bestanden en 2390 modellen,
lost overerving en textuurverwijzingen op, en bouwt daar objecten van. Dat is
werk dat voor een vaste spelversie elke keer hetzelfde antwoord geeft, dus doen
wij het één keer vooraf.

`tools/bake_block_table.py` schrijft alles naar één bestand dat de client
`mmap`'t en ter plekke indexeert. Geen JSON, geen allocaties, geen objectgraaf.

```
header        magic, versie, protocol, aantallen
quad_offsets  per staat een index in de quad-array (CSR)
quads         28 bytes per quad, vast formaat
texturen      offsets in de stringblob
bloknamen     offsets in de stringblob
state_block   per staat welk blok het is
state_flags   geen model / heeft geometrie / volle kubus
strings       alle namen achter elkaar, nul-afgesloten
```

Gemeten op 774: **10,34 MB, 29.671 staten, 356.952 quads, 1096 texturen**. De
`mmap` kost **20 µs** en de hele tabel doorlopen **60 µs**. Bakken kost 2,2
seconden, maar dat is bouwtijd, niet opstarttijd.

Coordinaten staan als `int16` in 1/256 model-eenheid. Over alle 356.952 quads is
de grootste afrondfout **0,0019 model-eenheid = 0,00012 blok**: bij een blok van
100 pixels op het scherm 0,012 pixel. Het bereik dat echt voorkomt is -8 tot 24
model-eenheden, ruim binnen wat `int16` bij deze schaal aankan (-128 tot 128).

De `full cube`-vlag is wat de mesher nodig heeft: 2457 staten zijn een volledige
dichte kubus en mogen het aanliggende vlak van hun buur wegsnijden.

Het bestand bevat alleen geometrie die uit de eigen installatie is afgeleid.
Geen assets van Mojang in de repo, en `tools/bake_block_table.py` heeft een
client-jar van de speler zelf nodig.
