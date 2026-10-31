# Material Columns: custom materials

## For mod authors

The mappings of custom materials can be adjusted through Material Columns by adding lines to an INI according to the categories you use. Included are configs for Remote Bombs, FuseablesEX and Invisible Attachments Shop.

If you want to make your own compatibility patch, the file to add is `romfs/MaterialColumns/Categories/your-mod.ini`. Use your actor's internal name for the material. You can choose its category: `fruit`, `mushrooms`, `special-materials`, or a category of your own. The full list is below.

For a different language, add the translation into your category's section. English is required as a fallback. You can find a complete example with all 16 locales below, along with the Remote Bombs config.

To install, drag your INI into that folder in your mod. Include it with your download; Material Columns is required too. For game versions 1.2.1 and 1.4.2. Close the game before editing the files and restart afterward. No rebuild needed.

## Examples

- [All 16 locales, two new categories and five fictional materials](examples/fictional-materials.ini)
- [Remote Bombs: four actors and all 16 category translations](../config/botw-abilities.ini)
- [FuseablesEX: two added materials assigned to special-materials](../config/fuseables-ex.ini)
- [Invisible Attachments Shop: 35 added materials in one new category](../config/invisible-attachments.ini)
- [The Second Quest: 32 additional material/device records, including bundles](../config/second-quest.ini)

```ini
[General]
Version = 1

[Materials]
Obj_RemoteBombSphere = special-materials
```

## Reference

| Setting | Limit or rule |
| --- | --- |
| Actor name source | Exact `__RowId` in the material's `RSDB/PouchActorInfo` entry; preserve capitalization |
| Assignment | `[Materials]`, then `ActorName = friendly-category-id` |
| New definition | `[Category.your-mod.category]`, followed by `Name.<locale> = Caption` |
| Existing categories | Translations stay unchanged; custom definitions cannot replace them |
| Identity | Category ID persists remembered selection; changing the caption does not change the ID |
| Appearance | Only categories containing eligible owned items appear |
| Menu behavior | Existing navigation, previews, fades, actions and remembered selections retained |
| Prerequisite | Material mod must register its actors and inventory data for the installed game version |
| Encoding | UTF-8, optional BOM; Windows CRLF or Unix LF |
| Values | One setting per line; no quotes, escapes, multiline values or inline comments |
| Comments | A line beginning with `;` or `#`, after optional whitespace |
| Read timing | Once at the first entry into Material Columns; restart to reload |
| File order | Filename byte order, case-sensitive; later assignments win |
| Category replacement | Later complete definition wins; English caption required again |
| Cross-file references | All category definitions read before assignments |
| User override | Optional `zz-user-overrides.ini` in the same folder |
| Same file path in two mods | Only the mod manager's selected or last installed file is available |
| Files | 32 `.ini` files; no subdirectories; lowercase `.ini` extension |
| Filename | 63 UTF-8 bytes maximum; ordinary ASCII filenames recommended |
| File size | 65,536 bytes maximum |
| Line | 512 bytes maximum, excluding the newline |
| Actor ID | 1â€“63 ASCII letters, digits or underscores; case-sensitive |
| Category ID | 1â€“63 lowercase ASCII letters, digits, hyphens or dots; no leading, trailing or consecutive dots |
| Custom category ID | At least one dot, for example `botw-abilities.remote-bombs` |
| New categories | 44 definitions in total, alongside the 52 built-in categories |
| Material mappings | 256 distinct actor IDs; replacing an assignment does not consume another entry |
| Caption | Nonempty UTF-8; at most 63 UTF-16 units and 191 UTF-8 bytes; no control characters |
| Display fit | Native caption width unchanged; keep labels short even below the encoding limit |
| English | `Name.en-US` required in every custom category definition |
| Same-language fallback | `en-GB` â†’ `en-US`; `fr-CA` â†’ `fr`; `es-419` â†’ `es`; `pt-BR` â†’ `pt` |
| Final fallback | `en-US` |
| Equal usage | Built-ins retain their type-order rank; custom categories follow definition order |
| Custom item order | Existing catalog rank when known; otherwise stable actor identity order |
| Missing folder | No overrides; existing behavior retained |
| Missing/unsupported Version | Entire file skipped |
| Directory read failure, too many files or overlong INI filename | All INI loading skipped; existing behavior retained |
| Category/mapping capacity reached | Additional entries skipped; existing entries retained |
| Invalid assignment | Prior assignment or original classification retained |
| Unmapped custom actor | Original unknown-material/device fallback retained |
| Actor absent or misspelled | Mapping accepted if the ID syntax is valid; unused until a matching actor appears in inventory. Actor existence is not checked. |
| Malformed content | Invalid entries are skipped; valid entries can still apply. A broken section is ignored until a valid header. |
| Partial loading | No per-file rollback; a later read failure can leave earlier accepted entries in place. |
| Diagnostics | Diagnostic builds only: 0 syntax; 1 version; 2 name/caption; 3 locale; 4 category; 5 capacity; 6 assignment override. Release builds write no diagnostic logs. |
| Registry memory | 12,904 bytes reserved; only the selected caption retained per custom category |

| Locale | Language |
| --- | --- |
| `ja` | Japanese |
| `en-US` | American English |
| `en-GB` | British English |
| `fr` | French |
| `fr-CA` | Canadian French |
| `de` | German |
| `it` | Italian |
| `es` | Spanish |
| `es-419` | Latin American Spanish |
| `ko` | Korean |
| `nl` | Dutch |
| `pt` | Portuguese |
| `pt-BR` | Brazilian Portuguese |
| `ru` | Russian |
| `zh-Hans` | Simplified Chinese |
| `zh-Hant` | Traditional Chinese |

## Community fixtures

| Download inspected | Evidence | Scope |
| --- | --- | --- |
| [FuseablesEX v1.5](https://gamebanana.com/mods/454956), file 1022941 | Product.120 pouch table: `Animal_Kokko_A`, `KorokCarryPassenger_Pair` become materials | Classification checked offline; visibility/grouping accepted in the merged Citron 1.2.1 setup |
| [Invisible Attachments Shop](https://gamebanana.com/mods/450826), file 1026021 | Product.120 pouch table: 35 additional material entries | Classification checked offline; visibility/grouping accepted in the merged Citron 1.2.1 setup |
| Both original downloads | Older game-data overlays | Used in a local merge with The Second Quest for the 1.2.1 test; original downloads are not automatically compatible together. No 1.4.2 gameplay claim; assets not redistributed |
| [The Second Quest 1.2.1](https://gamebanana.com/mods/479009), file 1859970 | Product.121 pouch table and English names: 8 additional material records and 24 device records, including bundles | Existing translated categories; visibility/grouping accepted in the merged Citron 1.2.1 setup. Install the material mod separately for its matching game version |

## Built-in category IDs

| ID | English caption |
| --- | --- |
| fruit | Fruit |
| elemental-fruit | Elemental fruit |
| utility-plants | Utility plants |
| mushrooms | Mushrooms |
| plants | Plants |
| meat | Meat |
| cooking | Cooking |
| fish | Fish |
| shellfish | Shellfish |
| fairies | Fairies |
| butterflies | Butterflies |
| darners | Darners |
| beetles | Beetles |
| small-insects | Small insects |
| frogs | Frogs |
| lizards | Lizards |
| ores | Ore and gems |
| guts | Guts |
| jellies | Jellies |
| eyeballs | Eyeballs |
| wings | Wings |
| tails | Tails |
| dragon-spikes | Dragon spikes |
| dragon-horns | Dragon horns |
| dragon-scales | Dragon scales |
| dragon-claws | Dragon claws |
| dragon-fangs | Dragon fangs |
| octorok-parts | Octorok parts |
| bokoblin-horns | Bokoblin horns |
| boss-bokoblin-horns | Boss horns |
| moblin-horns | Moblin horns |
| horriblin-horns | Horriblin horns |
| lizalfos-horns | Lizalfos horns |
| lynel-saber-horns | Lynel saber horns |
| lynel-mace-horns | Lynel mace horns |
| gleeok-horns | Gleeok horns |
| soldier-horns | Soldier horns |
| captain-horns | Captain horns |
| hinox-horns | Hinox horns |
| like-stones | Like stones |
| heavy-parts | Heavy parts |
| claws-and-bones | Claws and bones |
| other-monster-parts | Monster parts |
| ancient-blades | Ancient blades |
| brightblooms | Brightblooms |
| fuel | Fuel |
| special-materials | Special materials |
| star-fragments | Star fragments |
| zonaite | Zonaite |
| zonai-charges | Zonai charges |
| zonai-devices | Zonai devices |
| other-materials | Other materials |
