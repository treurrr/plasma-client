# Licensing and branding

## Code

Everything in this repository is licensed under the **GNU General Public
License, version 3**. The full text is in [LICENSE](LICENSE).

In short: you may use, study, change and share this, and anything you build on
it has to stay under the same licence. That is deliberate. A client asks people
to trust it with their account and with what it shows them on a server, and the
only honest basis for that trust is that anyone can read what it does. A fork
that closes the source takes that away from its own users.

Running a separate service alongside the client - an account or cosmetics
backend, say - is not covered by this licence, because it is not a derivative
work of the client.

## Branding

The Plodder Client name, the chevron mark and the other identity assets in
[`brand/`](brand/) are **not** covered by the licence above. Using them without
written permission is not allowed, and that includes variations close enough to
be mistaken for them.

**If you fork this repository, remove the branding assets from your fork and
give it your own name.** The code is yours to build on; the name is how players
know whose client they are signing into, which is exactly what must not be
ambiguous.

This split follows [Modrinth's](https://github.com/modrinth/code/blob/main/COPYING.md),
for the same reason.

## Minecraft

Nothing from Mojang is in this repository and nothing from Mojang ships with the
client. Block geometry and textures are read at run time from a Minecraft
installation the player already owns.

NOT AN OFFICIAL MINECRAFT PRODUCT. NOT APPROVED BY OR ASSOCIATED WITH MOJANG OR
MICROSOFT.
