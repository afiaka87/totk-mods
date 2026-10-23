# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Validate private actor output against the original data and clean native tables."""
from __future__ import annotations
import argparse
import importlib.util
import json
import hashlib
from pathlib import Path
import sys
import unittest
import byml
import oead

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from totk_mod_tools.zstd_helper import TotkZstd
import build_inventory

def loader(path):
    spec = importlib.util.spec_from_file_location('bomb_actor_recipe', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

recipe = loader(Path(__file__).resolve().parents[1] / 'tools/build_actors.py')
parser = argparse.ArgumentParser()
parser.add_argument('--romfs', required=True, type=Path)
parser.add_argument('--candidate', required=True, type=Path)
parser.add_argument('--shadows', required=True, type=Path)
args, unittest_args = parser.parse_known_args()

class ResourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.z = TotkZstd(args.romfs)
        cls.receipt = json.loads((args.candidate / 'actor-recipe.json').read_text())

    def records(self, actor):
        path = args.candidate / 'romfs/Pack/Actor' / (actor + '.pack.zs')
        return {entry.name: bytes(entry.data) for entry in oead.Sarc(self.z.decompress(path)).get_files()}

    def load(self, records, path):
        return byml.Byml(records[recipe.resource_path(path)]).parse()

    def test_live_physics_and_attack_profiles(self):
        for actor, form, damage in recipe.VARIANTS:
            with self.subTest(actor=actor):
                records = self.records(actor)
                root = self.load(records, f'Actor/{actor}.engine__actor__ActorParam.bgyml')
                sound = self.load(records, root['Components']['SLinkRef'])
                self.assertEqual(sound['UserName'], 'TotkRemoteBomb')
                effect = self.load(records, root['Components']['ELinkRef'])
                self.assertEqual(effect['UserName'], 'TotkRemoteBomb')
                physics = self.load(records, root['Components']['PhysicsRef'])
                controller = self.load(records, physics['ControllerSetPath'])
                body = self.load(records, controller['RigidBodyEntityNamePathAry'][0]['FilePath'])
                shape = self.load(records, next(row['FilePath'] for row in controller['ShapeNamePathAry']
                                               if row['Name'] == body['ShapeName']))
                target = self.load(records, controller['RigidBodySensorNamePathAry'][0]['FilePath'])
                target_shape = self.load(records, next(row['FilePath'] for row in controller['ShapeNamePathAry']
                                                      if row['Name'] == target['ShapeName']))
                self.assertEqual(target['LayerSensor'], 'Object')
                self.assertEqual(target['MotionType'], 'Kinematic')
                target_sphere = target_shape['Sphere'][0]
                self.assertEqual(target_sphere['Center'], recipe.vector(0, .3125, 0))
                self.assertEqual(target_sphere['Radius'], recipe.f32(.349999994))
                self.assertEqual(target_sphere['MaterialPresets'], ['Material_Bomb'])
                self.assertEqual(body['Mass'], 55)
                self.assertEqual(body['MotionType'], 'Dynamic')
                self.assertEqual(body['BuoyancySettingMethod'], 'Auto')
                self.assertNotIn('DensityForBuoyancy', body)
                self.assertAlmostEqual(body['BuoyancyScale'], 1 if form == 'sphere' else .7)
                self.assertEqual(body['WaterFlowScale'], 1)
                expected_inertia = (1.375, 1.375, 1.375) if form == 'sphere' else (1.87918496, 2.2916851, 1.87918496)
                for axis, expected in zip('XYZ', expected_inertia):
                    actual = recipe.f32(shape['AutoCalc']['Tensor'][axis]*body['Mass'])
                    self.assertEqual(actual, recipe.f32(expected))
                self.assertEqual(shape['AutoCalc']['Volume'], recipe.f32(.0654499978 if form == 'sphere' else .1))
                if form == 'sphere':
                    self.assertNotIn('Box', shape)
                    self.assertAlmostEqual(shape['Sphere'][0]['Radius'], .25)
                    self.assertAlmostEqual(shape['Sphere'][0]['Center']['Y'], .25)
                    self.assertEqual(shape['Sphere'][0]['MaterialPresets'], ['Material_Bomb'])
                else:
                    self.assertNotIn('Sphere', shape)
                    polytope = shape['Polytope'][0]
                    self.assertEqual(len(polytope['Vertices']), 8)
                    self.assertAlmostEqual(polytope['ConvexRadius'], .05)
                    self.assertFalse(polytope['ShrinkByRadius'])
                    self.assertEqual(polytope['MaterialPresets'], ['Material_Bomb'])
                    for axis, low, high in [('X', -.25, .25), ('Y', 0, .4), ('Z', -.25, .25)]:
                        values = [vertex[axis] for vertex in polytope['Vertices']]
                        self.assertAlmostEqual(min(values)-polytope['ConvexRadius'], low)
                        self.assertAlmostEqual(max(values)+polytope['ConvexRadius'], high)
                table = self.load(records, root['Components']['GameParameterTableRef'])
                attack = self.load(records, table['Components']['gamebalance_AttackParam'])
                self.assertEqual(attack['BaseAttackPower'], damage)
                lift = self.load(records, root['Components']['LiftableRef'])
                self.assertFalse(lift['IsDeathAtLanding'])
                self.assertEqual(lift['OverrideThrownDamage'], 4)
                self.assertEqual(lift['LiftableType'], 'Pot_S')
                self.assertEqual(lift['RotateCenterPosOffset'], recipe.vector(0, recipe.LIFT_CENTERS[form], 0))
                barrel = self.load(records, lift['$parent'])
                self.assertEqual(barrel['LiftableType'], 'Barrel')
                self.assertEqual(barrel['LiftPurposes'], ['Throw'])
                attention = self.load(records, root['Components']['AttentionSenderRef'])
                self.assertEqual(len(attention['ClientList']), 1)
                self.assertIn('_SimpleCarriable_Grab.', attention['ClientList'][0])
                self.assertEqual(self.load(records, attention['ClientList'][0])['ActionType'], 'Lift')
                self.assertFalse(any('Item_Catch' in path or 'ExplosiveJelly' in path for path in records))
                blast = next(byml.Byml(raw).parse() for path, raw in records.items()
                             if path.endswith('_DefaultExplodeBlast.game__chm__ElementParam.bgyml'))
                self.assertEqual(blast['Size'], 6)
                self.assertEqual(blast['FireLevel'], 'None')
                self.assertEqual(blast['ElementType'], 'Blast')
                self.assertEqual(blast['AffectType'], 'Explosion')

    def test_bomb_keeps_updating_away_from_link(self):
        resident = {entry.name: bytes(entry.data) for entry in oead.Sarc(
            self.z.decompress(args.romfs / 'Pack/ResidentCommon.pack.zs')).get_files()}
        barrel = byml.Byml(resident['Component/LODParam/ForbidPushJobDefault_Default.'
                                    'engine__component__LODParam.bgyml']).parse()
        self.assertEqual(barrel['EnableCalcSkip'], 'Null')
        for actor, _, _ in recipe.VARIANTS:
            with self.subTest(actor=actor):
                records = self.records(actor)
                root = self.load(records, f'Actor/{actor}.engine__actor__ActorParam.bgyml')
                self.assertEqual(self.load(records, root['Components']['LODRef']), dict(barrel, EnableCalcSkip='False'))

    def test_material_presets_exist_in_native_config(self):
        archive = oead.Sarc(self.z.decompress(args.romfs / 'Pack/Bootup.Nin_NX_NVN.pack.zs'))
        config = byml.Byml(bytes(archive.get_file('Phive/Config/PhiveConfig.byml').data)).parse()
        presets = {row['ComponentName'] for row in config['MaterialPresetCollection']}
        for actor, _, _ in recipe.VARIANTS:
            for path, raw in self.records(actor).items():
                if '/ShapeParam/' not in path:
                    continue
                shape = byml.Byml(raw).parse()
                for geometry in ('Sphere', 'Polytope'):
                    for primitive in shape.get(geometry, []):
                        self.assertTrue(set(primitive['MaterialPresets']) <= presets, path)

    def test_only_remote_detonation_or_lava_ends_the_bomb(self):
        stock = {entry.name: bytes(entry.data) for entry in oead.Sarc(
            self.z.decompress(args.romfs/'Pack/Actor/BarrelBomb.pack.zs')).get_files()}
        original = self.load(stock, 'Life/DamageParameters/BarrelBomb.game__life__DamageParameters.bgyml')
        self.assertTrue(original['ExternalDamageParameters']['ImpulseDamageParameters']['IsImpulseDamageEnabled'])
        self.assertTrue(original['InternalDamageParameters']['DrownDamageParameters']['IsDrownDamageEnabled'])
        self.assertEqual(original['ExternalDamageParameters']['DamageRates']['Bomb'], 100)
        expected = recipe.copy.deepcopy(original)
        external, internal = expected['ExternalDamageParameters'], expected['InternalDamageParameters']
        external['ImpulseDamageParameters']['IsImpulseDamageEnabled'] = False
        external['DamageRates'] = dict.fromkeys(external['DamageRates'], 0)
        external['Brittleness'] = 'Normal'
        internal['DrownDamageParameters']['IsDrownDamageEnabled'] = False
        internal['BurnDamageParameters'].update(BoostBurnContinuousDamageRate=0, BoostIgnitionDamageRate=0,
                                                BurnContinuousDamage=0, IgnitionDamageLv1=-1)
        internal['FrozenDamageParameters']['OnTrigFrozenDamage'] = 0
        internal['LightningDamageParameters'].update(Damage=0, SageDamage=0)
        internal['Underwater']['ColdWaterDamage'] = 0
        self.assertEqual(internal['Underwater']['LavaDeepDamage'], 99999)
        for actor, _, _ in recipe.VARIANTS:
            with self.subTest(actor=actor):
                records = self.records(actor)
                root = self.load(records, f'Actor/{actor}.engine__actor__ActorParam.bgyml')
                life = self.load(records, root['Components']['LifeRef'])
                self.assertIn(actor, life['DamageParameters'])
                self.assertEqual(self.load(records, life['DamageParameters']), expected)
                chemical = self.load(records, root['Components']['ChemicalRef'])
                self.assertTrue(chemical['Object'][0]['RequestDeathIfExploded'])
                self.assertTrue(chemical['Object'][0]['RequestDeathIfBurned'])
                material = self.load(records, chemical['Object'][0]['ChemicalMaterial'])
                self.assertTrue(material['IsExplode'])
                self.assertFalse(material['IsBurn'])
                self.assertTrue(material['ExplodeElement'])

    def test_native_shadow_model_routes_and_budgets(self):
        for actor, form, _ in recipe.VARIANTS:
            records = self.records(actor)
            root = self.load(records, f'Actor/{actor}.engine__actor__ActorParam.bgyml')
            model = self.load(records, root['Components']['ModelInfoRef'])
            expected = recipe.SHADOW_MODELS[form]
            self.assertEqual(model['ModelProjectName'], expected)
            self.assertEqual(model['FmdbName'], expected)
            self.assertEqual(model['DebugModelScale'], recipe.vector(1, 1, 1))
            proof = self.receipt['shadows'][form]
            self.assertTrue(proof['topology_preserved'])
            self.assertLessEqual(proof['vertex_max_error'], .001)
            payload = args.shadows/form/f'{expected}.{expected}.bfres.mc'
            self.assertEqual(payload.read_bytes()[:4], b'MCPK')
            self.assertEqual(hashlib.sha256(payload.read_bytes()).hexdigest(), proof['sha256'])
            self.assertGreaterEqual(self.receipt['restbl_floors'][f'Model/{expected}.{expected}.bfres'],
                                    proof['expanded_bytes']*2+65536)
            relative = 'RSDB/ActorInfo.Product.121.rstbl.byml.zs'
            row = next(row for row in byml.Byml(self.z.decompress(args.candidate/'romfs'/relative)).parse()
                       if row['__RowId'] == actor)
            self.assertEqual(row['ModelProjectName'], expected)
            self.assertEqual(row['FmdbName'], expected)
            self.assertEqual(row['ModelAabbMin'], recipe.vector(*proof['minimum']))
            self.assertEqual(row['ModelAabbMax'], recipe.vector(*proof['maximum']))

    def test_every_custom_parameter_has_a_budget(self):
        for actor, _, _ in recipe.VARIANTS:
            for path, data in self.records(actor).items():
                if path.endswith('.bgyml'):
                    self.assertGreaterEqual(self.receipt['restbl_floors'][path], len(data)*10+4096)

    def test_no_same_path_parameter_edits(self):
        vanilla = self.z.decompress(args.romfs / 'Pack/Actor/BarrelBomb.pack.zs')
        original = {entry.name for entry in oead.Sarc(vanilla).get_files() if entry.name.endswith('.bgyml')}
        for actor, _, _ in recipe.VARIANTS:
            self.assertFalse(original.intersection(self.records(actor)))

    def test_stock_registration_rows_are_preserved(self):
        expected = {actor for actor, _, _ in recipe.VARIANTS}
        for family in ('ActorInfo', 'GameActorInfo', 'PouchActorInfo', 'AttachmentActorInfo'):
            relative = f'RSDB/{family}.Product.121.rstbl.byml.zs'
            stock = byml.Byml(self.z.decompress(args.romfs / relative)).parse()
            candidate = byml.Byml(self.z.decompress(args.candidate / 'romfs' / relative)).parse()
            original = {row['__RowId']: row for row in stock}
            current = {row['__RowId']: row for row in candidate}
            self.assertEqual(len(current), len(candidate))
            self.assertEqual(set(current)-set(original), expected)
            for name, row in original.items():
                self.assertEqual(current[name], row)

    def test_inventory_components_and_no_contact_fuse(self):
        for actor, _, _ in recipe.VARIANTS:
            records = self.records(actor)
            root = self.load(records, f'Actor/{actor}.engine__actor__ActorParam.bgyml')
            self.assertEqual(root['Category'], 'ObjectPouchContent')
            pouch = self.load(records, root['Components']['PouchContentRef'])
            self.assertEqual(pouch['Category'], 'Material')
            self.assertTrue(pouch['EnableStock'])
            params = self.load(records, root['Components']['GameParameterTableRef'])
            throw = self.load(records, params['Components']['ThrowBoxParam'])
            self.assertTrue(throw['$parent'].startswith('Work/GameParameter/ThrowBoxParam/'))
            self.assertIn(recipe.resource_path(throw['$parent']), records)
            attachment = self.load(records, root['Components']['AttachmentRef'])
            self.assertNotIn('ChemicalExplosionBomb', attachment['AdditionalSubType'])
            self.assertEqual(attachment['RequestDeathOnHit'], 'Never')
            self.assertEqual(attachment['CommonName'], actor)
            self.assertIn(actor, attachment['ReplacementActorName'])
            self.assertIn('EventPerformerRef', root['Components'])
            self.assertIn('AttentionSenderRef', root['Components'])
            components = root['Components']
            self.assertEqual(components['ActorNameRef'],
                             f'?ActorSystem/ActorName/{actor}.engine__actor__ActorName.bgyml')
            self.assertTrue(components['ModelBindRef'].endswith('/Default.engine__component__ModelBindParam.bgyml'))
            self.assertIn('/MaterialCommon.', components['ActorGameDataSetting'])
            receiver = self.load(records, components['SpecialPowerReceiverRef'])
            self.assertEqual(receiver, {'$parent': 'Work/Component/SpecialPowerReceiverParam/'
                                        'Default.game__component__SpecialPowerReceiverParam.gyml',
                                        'AcceptUltraHand': 'True'})
            self.assertNotIn('EventMemberRef', components)
            self.assertNotIn('NavMeshObjectRef', components)
            self.assertFalse(any('/ModelBindParam/' in path or '/NavMeshObjectParam/' in path for path in records))

    def test_inventory_tags_preserve_every_stock_path(self):
        relative = 'RSDB/Tag.Product.121.rstbl.byml.zs'
        stock = byml.Byml(self.z.decompress(args.romfs/relative)).parse()
        actual = byml.Byml(self.z.decompress(args.candidate/'romfs'/relative)).parse()
        before = {tuple(p):b for p,b in build_inventory.tag_rows(stock)}
        after = {tuple(p):b for p,b in build_inventory.tag_rows(actual)}
        self.assertEqual(actual['TagList'], stock['TagList'])
        for path,bits in before.items(): self.assertEqual(after[path],bits)
        for actor,_,_ in recipe.VARIANTS:
            path = ('Work/Actor/',actor,'.engine__actor__ActorParam.gyml')
            bits = after[path]
            for tag in ('PouchSort','PouchSort_Material','PouchSort_Material_ScrabuildArrow'):
                self.assertTrue(bits & (1 << actual['TagList'].index(tag)))
            self.assertFalse(bits & (1 << actual['TagList'].index('Cook')))

    def test_discovery_flags_preserve_stock_schema(self):
        relative = 'GameData/GameDataList.Product.110.byml.zs'
        stock = byml.Byml(self.z.decompress(args.romfs/relative)).parse()
        actual = byml.Byml(self.z.decompress(args.candidate/'romfs'/relative)).parse()
        self.assertEqual(actual['MetaData'], stock['MetaData'])
        maps = (0xADE0EF78,0x34BF6C2D)
        for kind,rows in stock['Data'].items():
            old = {r['Hash']:r for r in rows}; new = {r['Hash']:r for r in actual['Data'][kind]}
            for h,row in old.items():
                if kind=='Struct' and h in maps:
                    before={r['Hash']:r for r in row['DefaultValue']}
                    after={r['Hash']:r for r in new[h]['DefaultValue']}
                    for key,value in before.items(): self.assertEqual(after[key],value)
                    self.assertEqual(len(after)-len(before),4)
                else: self.assertEqual(new[h],row)
        flags={r['Hash']:r for r in actual['Data']['Bool']}
        for row in self.receipt['inventory']['discovery']:
            self.assertFalse(flags[row['flag']]['DefaultValue'])
        self.assertEqual(build_inventory.hash_name('SpObj_AirPlatform_Capsule_A_01'),4134008575)

    def test_inventory_mals_preserves_stock_messages(self):
        name=self.receipt['inventory']['mals']
        self.assertEqual(name,'USen.Product.121.sarc.zs')  # 1.2.1 loads only the 121 text archive
        stock={e.name:bytes(e.data) for e in oead.Sarc(self.z.decompress(args.romfs/'Mals'/name)).get_files()}
        actual={e.name:bytes(e.data) for e in oead.Sarc(self.z.decompress(args.candidate/'romfs/Mals'/name)).get_files()}
        member='ActorMsg/PouchContent.msbt'
        self.assertEqual(set(stock),set(actual))
        for path,value in stock.items():
            if path!=member: self.assertEqual(actual[path],value)
        old_groups,old_text=build_inventory.msbt_entries(stock[member])
        groups,text=build_inventory.msbt_entries(actual[member])
        self.assertEqual(text[:len(old_text)],old_text)
        entries={name:index for group in groups for name,index in group}
        for group in old_groups:
            for name,index in group: self.assertEqual(entries[name],index)
        for label,value in self.receipt['inventory']['messages'].items():
            self.assertEqual(text[entries[label]].decode('utf-16le').rstrip('\0'),value)

if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *unittest_args])
