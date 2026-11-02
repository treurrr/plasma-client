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
