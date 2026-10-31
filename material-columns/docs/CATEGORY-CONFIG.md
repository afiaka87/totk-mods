# Material Columns: guide for mod authors

## Overview

If you want to make your own compatibility file, add `romfs/MaterialColumns/Categories/your-mod.ini` to your mod. You can choose a category for each material, or add a category of your own. For a different language, add the translation into your category's section. English is required as a fallback.

Include the INI with your download; Material Columns v0.1.4 or later is required too. For game versions 1.2.1 and 1.4.2. Your material mod must work on the player's version of the game. You still need that mod to add the actors and inventory data. No rebuild of Material Columns is needed.

## Contents

- [Quick start: use an existing category](#quick-start-use-an-existing-category)
- [Find your actor's ID](#find-your-actors-id)
- [Create a new category](#create-a-new-category)
- [Add translations](#add-translations)
- [Examples and companion files](#examples-and-companion-files)
- [Built-in category IDs](#built-in-category-ids)
- [File rules](#file-rules)
- [Troubleshooting](#troubleshooting)

## Quick start: use an existing category

1. Create `romfs/MaterialColumns/Categories/your-mod.ini` inside your mod's folder. Give it a filename unique to your mod and save it as UTF-8.
2. Add `[General]` with `Version = 1`, then a `[Materials]` section with one assignment per line:

```ini
[General]
Version = 1

[Materials]
Obj_RemoteBombSphere = special-materials
```

3. Replace `Obj_RemoteBombSphere` with your material's exact actor ID. Replace `special-materials` with an ID from the [built-in category list](#built-in-category-ids), if needed.
4. Include the INI with your mod. The player needs both your mod and Material Columns installed and enabled.
5. Close and restart the game after changing the file. Open Material Columns while carrying the item to check its category.

The file is read once, on the first entry into Material Columns. Only categories with eligible items in the player's inventory appear. This example assigns an existing Remote Bombs actor; it does not add a bomb to the game or to a save.

## Find your actor's ID

You will need to write the exact internal game name for the material (you can find it in your `RSDB/PouchActorInfo` entry, under `__RowId`). This is case-sensitive, so leave the capitalization as it is.

For the Remote Bombs example above, the actor name is `Obj_RemoteBombSphere` and the category is `special-materials`. Put the actor on the left of the equals sign and the category on the right. The name you see in the inventory, such as "Remote bomb", won't work in place of the actor name.

You can include a config for another mod without having it installed. If you don't have its items, those assignments just go unused. Please double-check the actor name if an item ends up in the wrong column: a misspelled name is accepted too, but won't match your item.

## Create a new category

Add a `[Category.<id>]` section and an English caption, then assign actors to that ID:

```ini
[General]
Version = 1

[Category.my-mod.crystals]
Name.en-US = Crystals

[Materials]
MyMod_Crystal_Blue = my-mod.crystals
MyMod_Crystal_Red = my-mod.crystals
```

`MyMod_Crystal_Blue` and `MyMod_Crystal_Red` are fictional examples. Replace them with actors your mod registers.

Use an ID that includes your mod's name and a dot, such as `my-mod.crystals`, to avoid collisions. Every custom category definition must include a valid `Name.en-US`. Built-in categories cannot be redefined or renamed through an INI.

Keep the category ID stable between updates. Remembered selections use the ID, so changing its caption is safe; changing the ID gives it a new identity.

Custom categories use the same navigation and item actions as built-in categories. Categories remain ordered by their combined usage counts.

## Add translations

Add `Name.<locale>` entries to the category section. The active language is selected automatically from the system language:

```ini
[General]
Version = 1

[Category.my-mod.crystals]
Name.en-US = Crystals
Name.fr = Cristaux
Name.de = Kristalle

[Materials]
MyMod_Crystal_Blue = my-mod.crystals
```

English is required even when you supply another language. Keep captions short enough to fit above a column. Built-in category translations are already provided by Material Columns.

| Locale key | Language |
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

The loader first tries the exact locale. If it is missing, `fr-CA` tries `fr`, `es-419` tries `es`, and `pt-BR` tries `pt`. All remaining missing translations fall back to `en-US`, including a missing `en-GB` caption.

See the [complete 16-locale example](examples/fictional-materials.ini) or the [translated Remote Bombs category](../config/botw-abilities.ini).

## Examples and companion files

| File | What it demonstrates |
| --- | --- |
| [Fictional materials](examples/fictional-materials.ini) | Five actors, two new categories and all 16 locales |
| [Remote Bombs](../config/botw-abilities.ini) | Four actors in a new category with all 16 translations |
| [FuseablesEX](../config/fuseables-ex.ini) | Two actors assigned to `special-materials` |
| [Invisible Attachments Shop](../config/invisible-attachments.ini) | 35 actors assigned to one new category |
| [The Second Quest](../config/second-quest.ini) | 32 material/device records, including bundles, assigned to built-in categories |

The four companion INIs ship with Material Columns. They do not include the corresponding mods' assets. Unused mappings are harmless when the corresponding material mod is not installed.

## Built-in category IDs

Copy the ID from the left column into your assignment. The English captions are shown here for reference; the game displays the selected language's caption.

| Category ID | English caption |
| --- | --- |
| `fruit` | Fruit |
| `elemental-fruit` | Elemental fruit |
| `utility-plants` | Utility plants |
| `mushrooms` | Mushrooms |
| `plants` | Plants |
| `meat` | Meat |
| `cooking` | Cooking |
| `fish` | Fish |
| `shellfish` | Shellfish |
| `fairies` | Fairies |
| `butterflies` | Butterflies |
| `darners` | Darners |
| `beetles` | Beetles |
| `small-insects` | Small insects |
| `frogs` | Frogs |
| `lizards` | Lizards |
| `ores` | Ore and gems |
| `guts` | Guts |
| `jellies` | Jellies |
| `eyeballs` | Eyeballs |
| `wings` | Wings |
| `tails` | Tails |
| `dragon-spikes` | Dragon spikes |
| `dragon-horns` | Dragon horns |
| `dragon-scales` | Dragon scales |
| `dragon-claws` | Dragon claws |
| `dragon-fangs` | Dragon fangs |
| `octorok-parts` | Octorok parts |
| `bokoblin-horns` | Bokoblin horns |
| `boss-bokoblin-horns` | Boss horns |
| `moblin-horns` | Moblin horns |
| `horriblin-horns` | Horriblin horns |
| `lizalfos-horns` | Lizalfos horns |
| `lynel-saber-horns` | Lynel saber horns |
| `lynel-mace-horns` | Lynel mace horns |
| `gleeok-horns` | Gleeok horns |
| `soldier-horns` | Soldier horns |
| `captain-horns` | Captain horns |
| `hinox-horns` | Hinox horns |
| `like-stones` | Like stones |
| `heavy-parts` | Heavy parts |
| `claws-and-bones` | Claws and bones |
| `other-monster-parts` | Monster parts |
| `ancient-blades` | Ancient blades |
| `brightblooms` | Brightblooms |
| `fuel` | Fuel |
| `special-materials` | Special materials |
| `star-fragments` | Star fragments |
| `zonaite` | Zonaite |
| `zonai-charges` | Zonai charges |
| `zonai-devices` | Zonai devices |
| `other-materials` | Other materials |

## File rules

- Use one `key = value` per line, without surrounding quotes. Put comments on their own line, starting with `;` or `#`.
- Actor IDs are case-sensitive and allow ASCII letters, digits and underscores. Category IDs allow lowercase ASCII letters, digits, hyphens and dots. Both have a 63-character limit. Custom category IDs need a dot, such as `my-mod.crystals`; do not start or end an ID with a dot or use consecutive dots.
- Give each INI a unique filename. If more than one file assigns the same actor, the last valid assignment wins; files are read in case-sensitive filename order.

| Limit | Maximum |
| --- | --- |
| Files in the Categories folder | 32 INIs, each up to 64 KiB; no subfolders |
| New categories across all INIs | 44 |
| Distinct actor assignments across all INIs | 256 |

## Troubleshooting

If an item appears in the wrong column, check:

- The path is `romfs/MaterialColumns/Categories/your-mod.ini`, with a lowercase `.ini` extension and UTF-8 encoding.
- The file contains `[General]` and `Version = 1`. This is the INI format version, not the game version.
- The actor ID and category ID are spelled correctly. A new category also needs `Name.en-US`.
- The player owns the item, and the material mod works on the installed game version.
- The game was restarted after the INI changed.

Missing actors have no effect. Invalid assignments leave an earlier valid assignment or the normal grouping in place. An INI only assigns categories; it does not merge or port another mod's assets.
