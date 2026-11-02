# Plodder Client

Native Minecraft-client in C++23. Multiplayer-only, clean-room: geen
gedecompileerde Minecraft-code, alleen het netwerkprotocol, Mojangs eigen
pakketrapporten en assets uit een eigen legale installatie.

Eerlijk voordeel, geen cheats. Snellere opstart, meer FPS, lagere
input-latency — maar geen informatie of invoer die de vanilla client niet heeft.
Zie `docs/PRODUCT.md` voor de positionering en `docs/ARCHITECTUUR.md` voor hoe het
in elkaar zit.

Ondersteund: **774 (1.21.11)** en **776 (26.2)**. Beide nummers komen uit
`version.json` in de officiële jars.

## Bouwen

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

Debug-builds zetten AddressSanitizer en UBSan aan en hebben daarvoor
`sudo dnf install libasan libubsan` nodig.

## Gebruiken

```sh
./build/plodder-client features                  # wat er aan en uit staat
./build/plodder-client versions                  # ondersteunde protocollen
./build/plodder-client ping <host>[:poort]       # serverinfo
./build/plodder-client join <host>[:poort] [naam] [protocol]
```

`join` logt in en blijft in de play-state staan. Typen stuurt chat, `/iets`
stuurt een commando, `/quit` stopt. `PLODDER_DEBUG=1` logt elk onbehandeld
play-pakket met zijn ID en eerste bytes — zo zijn de layouts hier uitgezocht.

Servermeldingen zijn pas leesbaar met Minecrafts taalbestand. Dat is een
game-asset en zit dus niet in deze repo:

```sh
tools/fetch_lang.sh ~/.minecraft/versions/1.21.11/1.21.11.jar
```

## Een versie toevoegen

ID's worden gegenereerd, nooit met de hand getypt:

```sh
java -DbundlerMainClass=net.minecraft.data.Main -jar server.jar --reports --output out
python3 tools/gen_packet_ids.py 774=out774/reports/packets.json 776=out776/reports/packets.json \
    > src/proto/packet_ids_gen.cpp
```

## Mijlpalen

- [x] **1. Ping** — socket, varints, packet-framing met zlib, JSON, chat-componenten
- [x] **2. Inloggen** — handshake → login → configuratie → play, keep-alive, chat
      in beide richtingen, NBT, taalbestand. Getest op echte 1.21.11- en
      26.2-servers, met twee clients tegelijk.
- [ ] **3. Wereld renderen** — chunks, atlas uit eigen jar, meshing (raw Vulkan,
      bindless, voorgebakken SPIR-V)
- [ ] **4. Speelbaar** — movement, breken/plaatsen, GUI's, entities
- [ ] **5. Online-mode** — Microsoft-sessie of doorgegeven sessie van een launcher

Nog te installeren voor mijlpaal 3:
`sudo dnf install glfw-devel vulkan-headers vulkan-loader-devel glm-devel libcurl-devel glslang spirv-tools`

## Wat dit niet wordt

Geen client die om je Microsoft-wachtwoord vraagt. Alleen OAuth of een sessie die
een launcher doorgeeft.

Voorlopig geen client voor servers met zware anticheat: vanilla-movement 1:1
namaken is het moeilijkste deel van een MC-client, en dat komt na mijlpaal 4.
