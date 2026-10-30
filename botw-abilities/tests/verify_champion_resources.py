# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Verify generated Champion resources against owned data and the actual composed output."""
import argparse
import copy
import hashlib
import json
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'mods/code/botw-abilities/tools'),
               str(ROOT / 'mods/code/totk-remote-bomb/tools'), str(ROOT / 'framework/bridgefs/tools')]
import byml
from totk_mod_tools.zstd_helper import TotkZstd
from import_audio import user_blocks, transcode, parser_budget, block, SLINK
from resource_merge import append_msbt, merge_mals, read_msbt, sarc_files, write_sarc
from build_champion_effects import DONOR
from build_champion_state import RESOURCE
from compose_resources import stock_budgets
from bridgefs_tools import audio_bank
from compose_resources import added_sounds, merge_bank, BANK
from build_champion_state import BLESSINGS
from build_champion_gameplay import clone_updraft, tuning
from import_champion_appearances import RECIPE


def verify_appearances(args, z):
    romfs = args.composed / 'romfs'
    recipe = json.loads(RECIPE.read_text(encoding='utf-8'))
    receipt = json.loads((args.champions / 'native-appearances/receipt.json').read_text())
    for path, expected in recipe['files'].items():
        if not path.startswith('RSDB/'):
            assert hashlib.sha256((romfs / path).read_bytes()).hexdigest() == expected, path
    expected_textures = {p for p in recipe['files'] if p.startswith('TexToGo/')}
    assert {p.relative_to(romfs).as_posix() for p in romfs.glob('TexToGo/BotwChampion_*.txtg')} == expected_textures
    expected_actors = {item['actor'] for item in recipe['appearances']}
    for pattern in ('Pack/Actor/Npc_Botw*Spirit.pack.zs', 'Model/Botw*.bfres*'):
        assert not list(romfs.glob(pattern)), pattern
    tables = {}
    for table in ('ActorInfo','GameActorInfo'):
        rows = byml.Byml(z.decompress(romfs / f'RSDB/{table}.Product.121.rstbl.byml.zs')).parse()
        assert not any(row['__RowId'].startswith('Npc_Botw') for row in rows)
        tables[table] = {row['__RowId']: row for row in rows}
        assert {name for name in tables[table] if name.startswith('Npc_ChampionsPoc')} == expected_actors
    budgets = stock_budgets(z.decompress(args.composed / 'release-restbl' / 'System/Resource/ResourceSizeTable.Product.121.rsizetable.zs'))
    assert all(budgets(path) >= size for path, size in receipt['floors'].items())
    for item in recipe['appearances']:
        name = item['actor']
        pack = sarc_files(z.decompress(romfs / f'Pack/Actor/{name}.pack.zs'))
        root = byml.Byml(pack[f'Actor/{name}.engine__actor__ActorParam.bgyml']).parse()
        def component(key):
            return byml.Byml(pack[root['Components'][key].removeprefix('?')]).parse()
        model, animation, info = component('ModelInfoRef'), component('AnimationRef'), component('ASInfoRef')
        assert model == {'FmdbName': item['model'], 'ModelProjectName': item['model']}
        assert animation['AnimationResources'] == [{'ModelProjectName': item['animation']}]
        assert info['AutoPlaySkeletals'] == [{'FileName': item['clip']}]
        assert info['AutoPlayMaterials'] == [{'FileName': item['material']}]
        actor = tables['ActorInfo'][name]
        assert actor['AnimationResources'] == animation['AnimationResources']
        assert actor['ASInfoAutoPlaySkeletals'] == info['AutoPlaySkeletals']
        assert actor['ASInfoAutoPlayMaterials'] == info['AutoPlayMaterials']
        assert actor['ModelProjectName'] == item['model']
    bolt = sarc_files(z.decompress(romfs / 'Pack/Actor/EftLoc_BotwUrbosaBolt.pack.zs'))
    actor = byml.Byml(bolt['Actor/EftLoc_BotwUrbosaBolt.engine__actor__ActorParam.bgyml']).parse()
    assert 'ChemicalRef' not in actor['Components']
    assert not (romfs / 'Pack/Actor/LightningStrike_BotwUrbosa.pack.zs').exists()
    return {'accepted_actor_chains': 4, 'exact_assets': len(recipe['files'])-2,
            'verified_floors': len(receipt['floors']), 'fury_bolt_has_no_chemical_damage': True}


def verify(args):
    z = TotkZstd(args.romfs)
    report = {}
    sound_work = args.composed / 'slink-merge'
    sound_binary, sound_text = sound_work / 'final.bslnk', sound_work / 'final.txt'
    sound_binary.write_bytes(z.decompress(args.composed / 'romfs' / SLINK))
    transcode(args.xlink,sound_binary,sound_text,'EXKing',args.champions / 'effect-users.txt')
    actual_sounds = user_blocks(sound_text.read_text(encoding='utf-8'))
    expected_sounds = user_blocks((sound_work / 'restored.txt').read_text(encoding='utf-8'))
    assert actual_sounds == expected_sounds, 'final sound routing differs from composed output'
    original_sounds = user_blocks((sound_work / '0.txt').read_text(encoding='utf-8'))
    for name,user in original_sounds.items():
        if name != 'UI_GlobalSound':
            assert actual_sounds[name] == user, name
    before_ui, after_ui = original_sounds['UI_GlobalSound'],actual_sounds['UI_GlobalSound']
    before_calls = block(before_ui,r'^    AssetCallTables \{')
    after_calls = block(after_ui,r'^    AssetCallTables \{')
    assert after_ui.replace(after_calls,before_calls,1) == before_ui
    for match in re.finditer(r'^      ([^\n{}]+) \{',before_calls,re.MULTILINE):
        pattern = r'^      '+re.escape(match[1])+r' \{'
        assert block(before_calls,pattern) == block(after_calls,pattern)
    sounds = json.loads((args.champions / 'audio-receipt.json').read_text())['sounds']
    for cue,entry in sounds.items():
        call = block(after_ui,r'^      '+entry['asset']+r'\[0x[0-9a-f]+\] \{')
        assert f'RuntimeAssetName = "{entry["asset"]}"' in call
        group = 'NpcVoice' if ':NV_Custom_NoActor_' in entry['source'] and cue not in ('MiphaUsed', 'DarukUsed') else 'SystemInsideWorld'
        assert f'GroupName = "{group}"' in call, cue
    assert 'TotkRemoteBomb' in actual_sounds
    report['slink'] = {'stock_users_preserved':len(original_sounds), 'champion_calls':len(sounds),
                       'native_ui_calls_preserved':True, 'remote_bomb_preserved':True}
    effects = json.loads((args.champions / 'effects-receipt.json').read_text())
    clean_users = user_blocks((args.composed / 'elink-merge/0.txt').read_text(encoding='utf-8'))
    all_users = user_blocks((args.composed / 'elink-merge/restored.txt').read_text(encoding='utf-8'))
    for path in (args.composed / 'elink-merge').glob('[1-9].txt'):
        for name, value in user_blocks(path.read_text(encoding='utf-8')).items():
            assert all_users[name] == value, f'changed member effect {name}'
    assert all(all_users[name] == value for name, value in clean_users.items())
    final_work = args.receipt.parent / 'champion-final-elink'
    final_work.mkdir(parents=True,exist_ok=True)
    names = final_work / 'names.txt'
    known_names = (args.champions / 'effect-users.txt').read_text(encoding='utf-8')
    native_names = args.composed / 'native-sphere/users.txt'
    if native_names.is_file():
        known_names += native_names.read_text(encoding='utf-8')
    names.write_text(known_names,encoding='utf-8')
    final_binary = final_work / 'final.belnk'
    assert not (args.composed / 'romfs/ELink2/elink2.Product.110.belnk.zs').exists(), 'shared ELink override must not ship'
    final_binary.write_bytes((args.composed / 'private-effects/combined.belnk').read_bytes())
    before_text, final_text = final_work / 'before.txt', final_work / 'final.txt'
    transcode(args.xlink,args.composed / 'elink-merge/merged.belnk',before_text,'EXKing',names)
    transcode(args.xlink,final_binary,final_text,'EXKing',names)
    expected_text, actual_text = before_text.read_text(encoding='utf-8'), final_text.read_text(encoding='utf-8')
    expected_users = user_blocks(expected_text)
    actual_users = user_blocks(actual_text)
    assert all(actual_users.get(name) == value for name,value in expected_users.items()), 'native postprocessing changed a composed effect user'
    assert expected_text.split('Users {',1)[0] == actual_text.split('Users {',1)[0]
    private_text = final_work / 'private.txt'
    private_binary = args.composed / 'romfs/BotwAbilities/Effects/Private.belnk'
    transcode(args.xlink,private_binary,private_text,'EXKing',names)
    private_users = user_blocks(private_text.read_text(encoding='utf-8'))
    assert private_users == {name: value for name,value in actual_users.items() if name not in clean_users}
    assert private_users.keys().isdisjoint(clean_users)
    effect_banks = byml.Byml(z.decompress(args.composed / 'romfs/Effect/EffectFileInfo.Product.110.Nin_NX_NVN.byml.zs')).parse()['BinaryDict']
    for effect in effects['effects'].values():
        actor, bank = effect['actor'], effect['bank']
        assert actor in actual_users
        assert effect_banks.get(actor) == (None if bank == 'static' else bank)
    field_user = actual_users[effects['effects']['UrbosaRange']['actor']]
    assert 'AlwaysTriggers' not in field_user
    assert 'Scale = 102.800003' in field_user and 'Matrix = 0x0' in field_user
    assert 'RuntimeAssetName = "Npc_Rouge_LightningAttack_Area"' in field_user
    report['elink'] = {'stock_preserved': len(clean_users), 'merged': len(all_users),
                       'champion_users': len(effects['effects']), 'final_users':len(actual_users), 'final_users_preserved':True,
                       'private_users': len(private_users), 'private_bytes': private_binary.stat().st_size,
                       'shared_override_removed': True}
    budgets = stock_budgets(z.decompress(args.romfs / 'System/Resource/ResourceSizeTable.Product.121.rsizetable.zs'))
    report['donor_budgets'] = {}
    donor = sarc_files(z.decompress(args.romfs / f'Pack/Actor/{DONOR}.pack.zs'))
    for path, raw in donor.items():
        report['donor_budgets'][path] = {'bytes': len(raw), 'budget': budgets(path)}
    for effect in effects['effects'].values():
        actor = effect['actor']
        path = f'Pack/Actor/{actor}.pack.zs'
        raw = z.decompress(args.composed / 'romfs' / path)
        assert raw == z.decompress(args.champions / 'romfs' / path)
        files = sarc_files(raw)
        node = byml.Byml(files[f'Actor/{actor}.engine__actor__ActorParam.bgyml']).parse()
        link = node['Components']['ELinkRef'].removeprefix('?')
        assert byml.Byml(files[link]).parse()['UserName'] == actor
        assert all(DONOR not in name for name in files)
    base = byml.Byml(z.decompress(args.romfs / RESOURCE)).parse()
    combined = byml.Byml(z.decompress(args.composed / 'romfs' / RESOURCE)).parse()
    for kind, rows in base['Data'].items():
        current = {row['Hash']: row for row in combined['Data'][kind]}
        for row in rows:
            after = current[row['Hash']]
            if kind == 'Struct':
                expected = copy.deepcopy(row)
                expected['DefaultValue'] = after['DefaultValue']
                assert expected == after
                entries = {value['Hash']: value for value in after['DefaultValue']}
                assert all(entries[value['Hash']] == value for value in row['DefaultValue'])
            else:
                assert row == after
    count = sum(len(combined['Data'][kind]) - len(rows) for kind, rows in base['Data'].items())
    delta = combined['MetaData']['SaveDataSize'][0] - base['MetaData']['SaveDataSize'][0]
    assert count == 28 and delta == 224, (count, delta)
    report['gamedata'] = {'native_records_preserved': sum(map(len, base['Data'].values())),
                          'added_flags': count, 'save_growth_bytes': delta}
    path = 'Mals/USen.Product.121.sarc.zs'
    clean = z.decompress(args.romfs / path)
    member = z.decompress(args.composed / 'romfs' / path)
    entries = sarc_files(clean)
    key = 'ActorMsg/PouchContent.msbt'
    entries[key] = append_msbt(entries[key], {'ChampionTest_Name': 'Test\0'.encode('utf-16le')})
    merged = sarc_files(merge_mals(clean, {'bomb': member, 'champion': write_sarc(entries)}))
    original = sarc_files(member)
    assert all(raw == merged[name] for name, raw in original.items() if name != key)
    messages = read_msbt(merged[key])[2]
    assert all(messages[name] == raw for name, raw in read_msbt(original[key])[2].items())
    assert messages['ChampionTest_Name'] == 'Test\0'.encode('utf-16le')
    report['mals_fixture'] = {'native_member_messages_preserved': len(read_msbt(original[key])[2]),
                              'other_files_byte_identical': len(original) - 1}
    item_receipt = json.loads((args.champions / 'items/items-receipt.json').read_text())
    actual_text = read_msbt(original[key])[2]
    for name, text in item_receipt['messages'].items():
        assert actual_text[name].decode('utf-16le').rstrip('\0') == text, name
    item_ids = {f'Obj_BotwHeroSoul_{region}' for _, region, _ in BLESSINGS}
    for table in ('ActorInfo', 'GameActorInfo', 'PouchActorInfo'):
        relative = f'RSDB/{table}.Product.121.rstbl.byml.zs'
        native_rows = byml.Byml(z.decompress(args.romfs / relative)).parse()
        actual_rows = {r['__RowId']: r for r in byml.Byml(z.decompress(args.composed / 'romfs' / relative)).parse()}
        assert item_ids <= actual_rows.keys()
        assert all(actual_rows[row['__RowId']] == row for row in native_rows)
    for actor in item_ids:
        relative = f'UI/Tex/Icon/{actor}.bntx.zs'
        assert (args.champions / 'items/romfs' / relative).read_bytes() == (args.composed / 'romfs' / relative).read_bytes()
    report['items'] = {'private_items': 4, 'original_messages': len(item_receipt['messages']),
                       'native_rows_preserved': True, 'hud_source_bytes': sum(i['hud_bytes'] for i in item_receipt['items'].values())}
    settings = tuning()
    path = f'Pack/Actor/{settings["donor"]}.pack.zs'
    native_wind = sarc_files(z.decompress(args.romfs / path))
    expected_wind, origins = clone_updraft(native_wind, settings)
    path = f'Pack/Actor/{settings["actor"]}.pack.zs'
    actual_wind = sarc_files(z.decompress(args.composed / 'romfs' / path))
    assert actual_wind == expected_wind
    unchanged = sum(name == old for name, old in origins.items())
    assert unchanged == 8
    for table in ('ActorInfo', 'GameActorInfo'):
        path = f'RSDB/{table}.Product.121.rstbl.byml.zs'
        native_rows = byml.Byml(z.decompress(args.romfs / path)).parse()
        actual_rows = {r['__RowId']: r for r in byml.Byml(z.decompress(args.composed / 'romfs' / path)).parse()}
        expected = copy.deepcopy(next(r for r in native_rows if r['__RowId'] == settings['donor']))
        expected['__RowId'] = settings['actor']
        assert actual_rows[settings['actor']] == expected
    def floors(path):
        return {key: int(size) for line in path.read_text().splitlines()
                if line.strip() and not line.lstrip().startswith('#') for key, size in [line.split('\t')]}
    wind_floors = floors(args.champions / 'gameplay/restbl-forces.tsv')
    combined_floors = floors(args.composed / 'restbl-forces.tsv')
    assert all(combined_floors[key] >= size > 0 for key, size in wind_floors.items())
    for name, old in origins.items():
        if name != old:
            assert wind_floors[name] >= budgets(old) > 0
    report['gale'] = {'actor': settings['actor'], 'unchanged_pack_members': unchanged,
                       'private_resources': len(origins) - unchanged, 'parser_floors_preserved': len(wind_floors),
                       'native_wind_speed': settings['wind_speed']}
    report['appearances'] = verify_appearances(args,z)
    clean_raw = z.decompress(args.romfs / BANK)
    native_audio = audio_bank.read_bank(clean_raw)
    combined_audio = audio_bank.read_bank(z.decompress(args.composed / 'romfs' / BANK))
    audio_bank.verify_preserved(native_audio, combined_audio)
    combined_entries = {e.name: e for e in combined_audio.entries}
    supplied = {}
    audio_sources = {'champions': args.champions / 'romfs' / BANK}
    for name in ('totk-stasis', 'totk-remote-bomb', 'totk-cryonis'):
        folder = ROOT / 'mods/code' / name
        manifest = tomllib.loads((folder / 'mod.toml').read_text())
        audio_sources[name] = folder / next(f['source'] for f in manifest['payload']['file'] if f['dest'] == 'romfs/' + BANK)
    for name, path in audio_sources.items():
        raw = z.decompress(path)
        sounds = added_sounds(audio_bank, native_audio, audio_bank.read_bank(raw))
        for entry, sound in sounds:
            target = combined_entries[entry.name]
            assert target.metadata == entry.metadata
            assert combined_audio.data[target.audio_offset:target.audio_offset + len(sound.wave)] == sound.wave
        supplied[name] = len(sounds)
    assert supplied['champions'] == 21
    parry = combined_entries['BotwChampions_ParryImpact']
    native_parry = (args.romfs / 'Sound/Resource/Stream/Shield_JustGird.bwav').read_bytes()
    assert combined_audio.data[parry.audio_offset:parry.audio_offset + len(native_parry)] == native_parry
    champion_raw = z.decompress(args.champions / 'romfs' / BANK)
    try:
        merge_bank(audio_bank, clean_raw, {'first': champion_raw, 'duplicate': champion_raw})
    except ValueError:
        pass
    else:
        raise AssertionError('duplicate audio ownership was accepted')
    report['audio'] = {'stock_preserved': len(native_audio.entries), 'merged': len(combined_audio.entries),
                       'additions_preserved': supplied, 'duplicate_refused': True}
    args.receipt.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('romfs', 'champions', 'composed', 'receipt'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--xlink',type=Path,required=True)
    verify(parser.parse_args())
