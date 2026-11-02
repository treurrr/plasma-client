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

## Vulkan-fundament

Rauw Vulkan 1.3, geen wrapper-bibliotheek. Dat is geen koppigheid: de claims over
opstarttijd en frame-pacing hangen af van zelf de pipeline-cache, het
submissiepad en de geheugenindeling bezitten.

Twee keuzes die vanaf het begin vastliggen:

**SPIR-V wordt tijdens het bouwen gecompileerd en meegelinkt.** `glslangValidator`
maakt er `.spv` van, `tools/spv_to_header.py` zet dat om in een header met de
woorden erin. De client compileert dus geen shader en leest er geen van schijf.

**De pipeline-cache staat op schijf** en wordt bij het afsluiten weggeschreven.
Gemeten met één triviale pipeline:

| | pipeline bouwen |
|---|---|
| koud, lege cache | 6,7 ms |
| warm, 15 kB cache ingelezen | 0,4 ms |

Zeventien keer sneller, en dat voor één pipeline. Een echte client heeft er
tientallen, dus dit schaalt mee.

Wat niet weg te optimaliseren valt is het aanmaken van het device zelf: **175-215
ms**, vrijwel helemaal driver-initialisatie. Dat is de ondergrens van elke
opstarttijd die we gaan claimen, en vanilla betaalt hem net zo goed.

### Offscreen renderen als controlemiddel

De eerste renderpad is niet een venster maar een afbeelding: renderen naar een
image, terugkopieren naar een host-buffer en wegschrijven als PNG
(`src/image_png.cpp`, zlib die we al linkten). Dat geeft een frame dat je kunt
bekijken in plaats van alleen tellen, het draait zonder een venster over andermans
scherm te gooien, en het werkt ook op `llvmpipe` als er geen gpu is.

Timestamp-queries meten de gpu-tijd in het frame zelf. Een 1280x720 frame met
clear, driehoek en volledige terugkopie kost **0,31 ms** op de 4070, waarvan het
grootste deel de terugkopie is die een echt frame niet doet.

## Meshen

De mesher maakt van een chunk plus de blokkentabel driehoeken. Een vlak met een
`cullface` die naar een buurblok wijst dat een volle kubus is, wordt weggelaten.

Gemeten op 329 chunks echte overworld:

| | |
|---|---|
| quads over | 3.703.149 |
| quads weggesneden | 51.961.629 (**93,3%**) |
| driehoeken | 7.406.298 |
| meshen | 2,08 ms per chunk |

Die 2,08 ms was eerst **5,97 ms**. Het verschil zat vrijwel helemaal in
`rotate_face`: die deed per quad drijvendekommawiskunde en daarna een lineaire
zoektocht door zes richtingen, terwijl de rotatie bij verreweg de meeste blokken
nul is. Kortsluiten bij rotatie nul, plus de zes buurblokken één keer per blok
opzoeken in plaats van één keer per quad, scheelt bijna een factor drie.

Dat 93,3% klopt is niet op gevoel vastgesteld. Een test bouwt een sectie van
16x16x16 volle kubussen en eist exact 1536 overgebleven quads (zes kanten van
16x16) en exact 24.576 - 1536 weggesneden. Een stille fout in de buur-indexering
verandert dat getal meteen.

### Wat hier nog niet goed is

Twee dingen, allebei bewust nog open:

1. **503 MB vertexdata voor 329 chunks.** Een vertex is nu 28 bytes met
   `float`-posities. Dat moet naar 16 bytes met vaste-kommaposities per sectie,
   wat ook per sectie tekenen en frustum-culling mogelijk maakt.
2. **Chunkranden tellen als open.** Elke chunk emitteert daardoor zijn vier
   zijwanden volledig, ook waar de buurchunk er direct tegenaan zit. Dat vraagt
   meshen met kennis van de buren.

## De wereld renderen, en de bug die er bijna doorheen kwam

`plodder-client worldshot <server> <uit.png> [chunks] [zicht]` logt in, verzamelt
chunks, mesht ze met hun buren, uploadt alles naar de gpu en schrijft een frame
weg. Op 200 chunks: 1,9 miljoen driehoeken in **1,14 ms** gpu-tijd.

Buren meegeven aan de mesher scheelde direct de helft: zonder kennis van de
buurchunk emitteert elke chunk zijn vier zijwanden volledig.

| | driehoeken | vertexdata | gpu-tijd |
|---|---|---|---|
| zonder buren | 2.675.158 | 182 MB | 1,75 ms |
| met buren | 1.162.650 | 79 MB | 0,86 ms |

### Waarom dit zonder plaatjes niet was gevonden

Het eerste beeld toonde doorsnedes van het terrein in plaats van het oppervlak.
Drie verklaringen leken plausibel en waren alle drie fout: verkeerde
winding-volgorde (uitgesloten door culling uit te zetten: beeld bleef gelijk),
chunkranden (uitgesloten: de fix halveerde de driehoeken maar veranderde het
beeld niet), en een camera onder de grond.

De echte oorzaak zat in de projectiematrix. Die leverde **standaard** diepte,
near=0 en far=1, terwijl de depth buffer op 0 werd gewist en de test
`VK_COMPARE_OP_GREATER` was. Daarmee wint elk fragment dat verder weg ligt:
de client tekende de achterkant van de wereld.

De matrix is nu echt omgekeerd - near=1, far=0 - wat bij die clear en die test
hoort en bovendien de float-precisie legt waar je hem nodig hebt.

Dit is de reden dat de eerste renderpad een PNG schrijft in plaats van een
venster te openen. De test slaagde, er kwam een frame uit, de timings waren
goed, en het was hartstikke fout. Alleen kijken vond het.

## Texturen

De atlas wordt net als de blokkentabel vooraf gebakken, uit een installatie die
de speler zelf heeft. `tools/find_minecraft.py` zoekt die op - Modrinth App,
Prism, MultiMC, `~/.minecraft` - en leest de versie uit `version.json` in de jar
zelf in plaats van af te gaan op de mapnaam.

`tools/bake_atlas.py` leest de textuurlijst **uit de blokkentabel**, zodat de
indices van beide bestanden per constructie gelijk lopen. Tegels zijn uniform
(de grootste native maat in de pack, kleinere met nearest opgeschaald), wat ook
is wat het spel zelf doet, en wat betekent dat de plek van een tegel een
vermenigvuldiging is in plaats van een opzoeking. Van een animatiestrip wordt
het eerste frame genomen.

Resultaat voor 1.21.11: **2048x576, tegels van 32px, 1096 texturen, 4,73 MB**,
in 0,2 seconden gebakken. De client mapt dat bestand en kopieert het naar de gpu.

Geen enkele asset van Mojang staat in deze repo of hoort in een download van de
client mee te gaan. Elke speler bakt uit zijn eigen jar.

### Twee fouten die hier boven water kwamen

**`heavy_core.json` verwijst naar `"texture": "all"` zonder de `#`.** Dat is een
eigenaardigheid in Mojangs eigen asset; hun lader accepteert het, de mijne niet,
en dat leverde precies een ontbrekende textuur op. De resolver volgt nu ook een
naam zonder hekje als die een sleutel in de textuurmap is.

**De eerste getextureerde frames waren veel te donker.** De atlas staat als sRGB
op de gpu, dus sampelen geeft lineaire waarden, en die werden naar een
UNORM-doel geschreven dat een kijker weer als sRGB leest. Het renderdoel is nu
ook sRGB, zodat de conversie bij het schrijven gebeurt.

### Wat er nog niet is

- **Water wordt niet getekend.** Vloeistoffen hebben geen blokmodel en vragen een
  eigen renderpad.
- **Biomekleuren liggen vast.** Gras, bladeren en stengels worden vermenigvuldigd
  met een vaste vlaktekleur. De biomen worden al uit de chunk gelezen maar nog
  niet omgezet naar kleuren.
- **Geen mipmaps en geen ambient occlusion.** Het eerste geeft flikkering in de
  verte, het tweede maakt het beeld vlakker dan vanilla.

## Gaten in de grond

Het eerste getextureerde beeld had gaten. Twee oorzaken, allebei echt.

**Bladeren, glas en ijs sneden hun buren weg.** De `full cube`-vlag betekende
"zes vlakken die het hele blok vullen", en dat is waar voor die drie - maar je
kijkt er doorheen, dus achter een boom bleef niets over. Geometrie zegt niets
over ondoorzichtigheid. Er is nu een aparte vlag: een staat mag pas wegsnijden
als al zijn zes texturen volledig ondoorzichtig zijn, gecontroleerd op het
alfakanaal tijdens het bakken. Van de 2457 volle kubussen blijven er **2134**
over die dat mogen.

**Water werd helemaal niet getekend.** Vloeistoffen hebben geen blokmodel, dus
stonden ze bij lucht in de lijst "geen geometrie". Ze krijgen nu gemaakte quads:
een kubus waarvan alleen het bovenvlak lager ligt, met de hoogte uit de
`level`-eigenschap. Een vloeistof verbergt zich ook tegen zijn eigen soort,
anders zit een oceaan vol vlakken tussen aangrenzende waterblokken.

Dat laatste is nog niet vanilla-exact: echte vloeistofhoogtes interpoleren
tussen buren, en water hoort doorzichtig te zijn in plaats van dekkend.

## Mipmaps, en waarom de atlas een array-textuur werd

Zonder mipmaps korrelde alles op afstand. Met mipmaps en lineair verkleinen
verscheen er iets anders: een **raster van lichte lijnen** op elke blokgrens.

Dat is atlas-bleeding. Bij verkleinen leest de sampler aan de rand van een tegel
over de tegelgrens heen en mengt de buurtextuur erin. Padding verzacht dat maar
lost het niet op, want elk extra mipniveau vraagt twee keer zoveel rand.

De atlas is daarom geen raster meer maar een **array-textuur**: een laag per
textuur, de laag-index is gewoon de textuur-id uit de blokkentabel. Lagen kunnen
per constructie niet in elkaar lekken, mipniveaus worden per textuur gemaakt, en
uv's buiten 0..1 mogen nu herhalen zoals sommige modellen verwachten.

1098 lagen van 32px met vier mipniveaus is 5,97 MB. De spec garandeert maar 256
lagen; de kaarten hier doen 2048 en 8192, en de client weigert netjes met een
leesbare fout als een gpu te weinig lagen aankan. Een terugval op een
raster-atlas voor zulke kaarten staat nog open.

De sampler vergroot met nearest - dat is de blokkenlook - en verkleint met
lineair plus anisotropie.

## Water in lagen

Na dat raster bleven er horizontale lijnen over, een per bloklaag, dwars door
elke watermassa. Dat was geen filterprobleem maar de geometrie: ik verlaagde het
bovenvlak van **elk** waterblok naar 14/16, ook blokken die onder water zitten.
Tussen twee lagen zat dus een spleet van 2/16 waar je doorheen keek.

Alleen het bovenste blok van een waterkolom heeft een verlaagd oppervlak. Dat is
geen eigenschap van de blokstaat maar van zijn buur, dus het hoort in de mesher,
die de buren wel kent: ligt er water boven, dan vult het blok zijn hele hoogte.

## De bug die alle bovenvlakken wegnam

Elk bovenvlak in de hele renderer ontbrak. In een landschap valt dat niet op -
je ziet zijkanten, planten en water en het *lijkt* terrein - maar een losse
blokkentoets maakte het meteen duidelijk: recht van boven keek je door het blok
heen naar de vloer, met alleen een randje van de zijvlakken.

De oorzaak zat in `frontFace`. De winding van de zes vlakken is consistent, en
dat is precies wat me op het verkeerde been zette: een narekening in
schermruimte gaf keurig per camera exact één zichtbaar vlak. Maar **Vulkan
bepaalt de windingrichting in framebuffer-coordinaten, waar Y omlaag wijst**.
Wat in gewone wiskundige orientatie tegen de klok in loopt, loopt daar dus met
de klok mee. Met `VK_FRONT_FACE_COUNTER_CLOCKWISE` werden de vlakken die naar de
camera wijzen weggesneden en de afgewende getekend.

### Hoe dit boven water kwam

Niet door naar landschappen te kijken. `plodder-client showcase` bouwt een
sectie met opgegeven blokstaten zonder server en rendert die van voren, van
opzij, van boven of schuin. Op blokken waarvan je de textuurrichting kent -
een boomstam, een grasblok, een oven - zie je in een oogopslag wat er mis is.

Dat gereedschap corrigeerde ook een verkeerde diagnose van mij. De stam leek
twee fouten te hebben, bast in de verkeerde richting en ringen op een zijvlak,
tot bleek dat staat 136 `axis=x` is: een liggende stam, waar dat precies klopt.

## Volle kubussen met extra vlakken

`is_full_cube` eiste precies zes quads. Een grasblok heeft er tien - zes zijden
plus vier getinte overlays - en viel daardoor buiten de boot, terwijl het net zo
massief is als steen. Gewoon gras sneed dus nergens iets weg. De test kijkt nu
per richting of er een quad is die het hele blok bedekt, in plaats van quads te
tellen.

## Per sectie meshen en frustum culling

De mesher gaf eerst één buffer per chunk. Nu geeft hij er een per **sectie** van
16x16x16, met posities ten opzichte van de hoek van die sectie en een eigen
bounding box. Dat is de eenheid die de renderer kan overslaan, apart kan
uploaden en los kan weggooien - een chunk is daar te grof voor, want die is 384
blokken hoog en staat bijna nooit helemaal in beeld.

De renderer pakt alle secties in één vertex- en één indexbuffer en tekent er per
sectie uit met `vkCmdDrawIndexed(firstIndex, vertexOffset)`. De hoek van de
sectie gaat als push constant mee, zodat dezelfde geometrie overal kan staan.

Frustum culling test de bounding box tegen de vier zijvlakken van het
beeldvolume, gehaald uit de gecombineerde matrix (Gribb-Hartmann). Een doos
valt pas af als hij volledig achter een vlak ligt, dus de hoek die het verst
voor dat vlak uit steekt beslist.

Gemeten op 1920x1080, 200 chunks, 2,04 miljoen driehoeken:

| | zonder | met |
|---|---|---|
| getekende driehoeken | 2.039.102 | 843.650 (58% minder) |
| gemiddelde frametijd | 0,629 ms | **0,331 ms** |
| fps | 1589 | **3017** |
| 1% low | 1453 fps | **2414 fps** |

### Waarom twee renders van dezelfde scene toch verschilden

Het eerste plaatje na de omzetting verschilde op 0,134% van de pixels, in een
wig aan de horizon. Dat zag eruit als culling die te veel wegnam.

Het was het niet. Culling uitzetten gaf **exact hetzelfde verschil**, en twee
runs van dezelfde binary ook: de server stuurt niet elke keer dezelfde eerste
200 chunks, want de wereld is nog aan het genereren. Met en zonder culling is
het beeld pixel-identiek.

De les zit in het gereedschap: beelden uit verschillende runs vergelijken zegt
niets zolang de invoer niet vastligt. Een vaste chunk-set op schijf hoort bij
het benchmark-harnas.

## De vertex van 28 naar 16 bytes

Zodra posities per sectie zijn, hoeven ze geen floats meer te zijn: een sectie
is 16 blokken breed en een model steekt er hooguit een blok uit. Dus vaste komma
in 1/2048 blok, verschoven met een blok, in `uint16`. Dat is grover dan de
gebakken geometrie (1/4096 blok), maar de fout blijft onder 0,0005 blok - ver
onder een beeldpixel.

```
uint16 position[3]   xyz, vaste komma
uint16 packed        vlak | tintsoort | hoekschaduw
uint16 uv[2]         genormaliseerd, het formaat doet de deling
uint16 texture       laag in de array-textuur
uint16 light         gereserveerd voor sky- en blocklight
```

De uv's staan als `R16G16_UNORM`, zodat de GPU ze zelf naar 0..1 schaalt en er
geen deling in de shader nodig is. Positie en vlaggen delen één `R16G16B16A16`,
want een driecomponents 16-bits vertexformaat wordt niet overal ondersteund.

## De som van drie stappen

Zelfde scene, 1920x1080, 200 chunks, 2,04 miljoen driehoeken, RTX 4070 Laptop:

| stap | frametijd | fps | 1% low |
|------|-----------|-----|--------|
| alles tekenen, 28-byte vertex | 0,629 ms | 1589 | 1453 |
| per sectie + frustum culling | 0,331 ms | 3017 | 2414 |
| vertex naar 16 bytes | **0,247 ms** | **4050** | **3126** |

Twee en een half keer sneller, en de vertexdata ging van 139 naar 90 MB.

Dit meet het tekenpad en niets anders: geen spellogica, geen entities, geen UI,
en de wereld staat als één statische buffer op de kaart. Het is dus geen cijfer
om mee te adverteren. Wat het wel zegt is waar het werk zit: **meshen kost 4 ms
per chunk, zestien keer een heel frame.** Daar ligt de volgende winst, niet in de
renderer.


## Obfuscatie verschilt per versie, en dat bepaalt onze testroute

Horizon verifieert hun gedrag met **orakels**: ze compileren een paar regels Java
tegen de officiele jar, draaien dat naast hun eigen implementatie en eisen dat de
uitvoer identiek is. Dat kan bij hun doelversie 26.3 omdat die **onobfusceerd**
wordt uitgeleverd: de klassen heten gewoon zoals ze heten.

Bij onze doelversie ligt dat anders. Gemeten op 2 november:

| | geobfusceerd | officiele mappings van Mojang |
|---|---|---|
| 1.21.11 | ja, 10.291 klassen (`fzu$a.class`) | client 11,8 MB, server 8,7 MB |
| 26.2 | nee | niet aanwezig, niet nodig |

Mojang is op de 26-reeks gestopt met obfusceren en publiceert daar geen mappings
meer. Voor 1.21.11 bestaan ze wel, en dat is precies een keuze die niet impliciet
mag blijven: mappings gebruiken om een orakel aan te roepen valt in een grijze
zone van onze clean-room-regel.

Die keuze is nu niet urgent. **Het hele netwerkdeel heeft geen orakel nodig** -
daar is de server zelf de scheidsrechter, en die zegt het meteen als we iets fout
sturen. Hij wordt pas urgent bij worldgen, waar niets je corrigeert.

Wat wel meteen bruikbaar is, en volledig clean-room: Horizon test online-mode met
een **nagemaakte Mojang-dienst**. Een lokale mock van het discovery-document en
`join`/`hasJoined`, een echte vanilla server in online mode die via authlib's
eigen schakelaar naar die mock wijst, en hun client die inlogt met een
nep-token. Vanilla's eigen code controleert dan de server-hash, de RSA-wrapping
en de AES/CFB8-cipher. Geen Mojang-account nodig, geen regel van hun code gelezen.
