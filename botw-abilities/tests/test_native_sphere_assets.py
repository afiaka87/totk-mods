# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Native sphere format regressions and candidate integration contracts."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import unittest

import byml
import oead

ROOT = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(Path(__file__).resolve().parents[1]/'tools'), str(ROOT/'scripts')]
from build_native_sphere import ACTORS, BLAST, EFFECT_BANK, EFFECT_USER, encode, scale_shape
from build_actors import resource_path
from totk_mod_tools.zstd_helper import TotkZstd
from native_sphere_budgets import NativeBudgets, cloned_resource_floor, resource_kind
from build_actors import budgets
from build_release_restbl import read_forces


class FormatTests(unittest.TestCase):
    def test_tiny_or_shrinking_settings_preserve_native_allocations(self):
        self.assertEqual(cloned_resource_floor(4992,56,56),9088)
        self.assertEqual(cloned_resource_floor(9000,512,56),13096)
        self.assertGreaterEqual(cloned_resource_floor(4992,56,80),4992+240+4096)
        with self.assertRaises(ValueError):
            cloned_resource_floor(0,56,56)

    def test_analogue_requires_the_same_native_resource_type(self):
        pool=NativeBudgets.__new__(NativeBudgets)
        known={'Component/SLink/Stock.engine__component__SLinkParam.bgyml':5000,
               'Component/SLink/Wrong.engine__component__ELinkParam.bgyml':500000}
        pool.stock=lambda path:known.get(path,0)
        pool.accepted={}
        pool.resources={path:64 for path in known}
        path='Component/SLink/Custom.engine__component__SLinkParam.bgyml'
        proof=pool.parameter(path,path,64,64)
        self.assertEqual(proof['source_budget'],5000)
        with self.assertRaises(ValueError):
            pool.parameter('Unknown/Custom.engine__component__SLinkParam.bgyml',path,64,64)

    def test_odd_binary_nodes_before_string_arrays_round_trip(self):
        previous_failures=0
        for size in range(1,9):
            data={'BitTable': bytes(range(size)), 'PathList':['Work/Actor/','NativeSphere','.gyml'],
                  'RankTable':b'\0'*8, 'TagList':['Example']}
            self.assertEqual(byml.Byml(encode(data)).parse(),data)
            try:
                old=byml.Byml(byml.Writer(data,version=7).get_bytes()).parse()
                previous_failures += old != data
            except (IndexError,ValueError,AssertionError):
                previous_failures += 1
        self.assertGreater(previous_failures,0)

    def test_resize_changes_geometry_without_changing_mass_or_material(self):
        data={'Sphere':[{'Radius':byml.Float(.55),'Center':dict(X=byml.Float(0),Y=byml.Float(.55),Z=byml.Float(0)),
                         'MaterialPresets':['Material_MetalNoSlip']}], 'Mass':byml.Float(25),
              'Axis':dict(X=byml.Float(0),Y=byml.Float(1),Z=byml.Float(0))}
        result=scale_shape(data,.46)
        self.assertAlmostEqual(result['Sphere'][0]['Radius'],.55*.46,places=6)
        self.assertAlmostEqual(result['Sphere'][0]['Center']['Y'],.55*.46,places=6)
        self.assertEqual(result['Mass'],25)
        self.assertEqual(result['Axis'],data['Axis'])
        self.assertEqual(result['Sphere'][0]['MaterialPresets'],data['Sphere'][0]['MaterialPresets'])


class CandidateTests(unittest.TestCase):
    def test_both_shapes_keep_native_control_graph_animation_and_blast(self):
        z=TotkZstd(ROMFS)
        shapes=('Sphere','Cube') if (BUILD/'native-cube-receipt.json').exists() else ('Sphere',)
        actors=[name.replace('Sphere',shape) for name in ACTORS if name != BLAST for shape in shapes]
        for actor in actors:
            records={e.name:bytes(e.data) for e in oead.Sarc(
                z.decompress(BUILD/f'romfs/Pack/Actor/{actor}.pack.zs')).get_files()}
            def read(link): return byml.Byml(records[resource_path(link)]).parse()
            root=read(f'?Actor/{actor}.engine__actor__ActorParam.bgyml')
            ai=read(root['Components']['AIInfoRef'])
            self.assertEqual(ai['PostCalcAIRef'],'Work/AI/Root/SpObj_TimerBomb.root.ain',actor)
            self.assertEqual(read(root['Components']['SLinkRef'])['UserName'],'SpObj_TimerBomb_A_01',actor)
            self.assertEqual(ai['PostUpdateMatrix'],'Work/AI/Root/BtAb_Effct.root.ain',actor)
            self.assertEqual(read(root['Components']['ASInfoRef'])['AutoPlayASName'],'Wait',actor)
            if actor.endswith('_Attachment'):
                self.assertEqual(ai['PostSensorAIRef'],'Work/AI/Root/ChemicalRod.root.ain',actor)
                self.assertEqual(ai['RootAIRef'],'Work/AI/Root/ChemicalRod_PreCalc.root.ain',actor)
            drop=read(root['Components']['DropRef'])
            table=read(drop['DropTableResourceList'][0])
            element=read(table['DropTableElementResourceList'][0])
            self.assertEqual(element['DropTableElement'][0]['DropActorName'],
                f'Work/Actor/{BLAST}.engine__actor__ActorParam.gyml',actor)

    def test_live_death_drop_creates_the_private_blast(self):
        z=TotkZstd(ROMFS)
        original='Work/Actor/ExpandField_TimeBomb.engine__actor__ActorParam.gyml'
        private=f'Work/Actor/{BLAST}.engine__actor__ActorParam.gyml'
        for actor in ACTORS:
            if actor == BLAST: continue
            records={e.name:bytes(e.data) for e in oead.Sarc(
                z.decompress(BUILD/f'romfs/Pack/Actor/{actor}.pack.zs')).get_files()}
            def read(link): return byml.Byml(records[resource_path(link)]).parse()
            root=read(f'?Actor/{actor}.engine__actor__ActorParam.bgyml')
            drop=read(root['Components']['DropRef'])
            self.assertEqual(len(drop['DropTableResourceList']),1)
            table=read(drop['DropTableResourceList'][0])
            element=read(table['DropTableElementResourceList'][0])
            self.assertEqual(element['DropTableElement'][0]['DropActorName'],private,actor)
            self.assertEqual(element['DropTableElement'][0]['DropProbability'],100)
            self.assertTrue((BUILD/f'romfs/Pack/Actor/{BLAST}.pack.zs').is_file())
            for name,raw in records.items():
                if name.endswith('.bgyml'):
                    self.assertNotIn(original,json.dumps(byml.Byml(raw).parse(),default=str),(actor,name))
            chemical=read(root['Components']['ChemicalRef'])
            material=read(chemical['Object'][0]['ChemicalMaterial'])
            self.assertFalse(material['IsExplode'],actor)

    def test_native_resource_floors_cover_their_recorded_sources(self):
        proof=json.loads((BUILD/'native-sphere-receipt.json').read_text())
        self.assertIn('parameter_budgets',proof)
        floors={path:int(size) for path,size in read_forces(BUILD/'restbl-forces.tsv')}
        z=TotkZstd(ROMFS)
        actual=budgets(z.decompress(BUILD/'release-restbl/System/Resource/ResourceSizeTable.Product.121.rsizetable.zs'))
        self.assertGreaterEqual(len(proof['parameter_budgets']),272)
        for row in proof['parameter_budgets']:
            self.assertEqual(resource_kind(row['source']),resource_kind(row['clone']))
            self.assertGreaterEqual(row['floor'],cloned_resource_floor(
                row['source_budget'],row['source_bytes'],row['cloned_bytes']))
            self.assertGreaterEqual(floors[row['clone']],row['floor'])
            self.assertGreaterEqual(actual(row['clone']),row['floor'])
        stock=budgets(z.decompress(ROMFS/'System/Resource/ResourceSizeTable.Product.121.rsizetable.zs'))
        original='GameParameter/TimerBarrelBomb/SpObj_TimerBomb.game__Object__TimerBarrelBomb.bgyml'
        self.assertGreater(stock(original),56*10+4096)
        for actor in ACTORS:
            if actor==BLAST:continue
            parent,filename=original.rsplit('/',1)
            self.assertGreaterEqual(floors[f'{parent}/{actor}_{filename}'],stock(original)+4096)

    def test_materials_creation_keeps_the_item_identity(self):
        z=TotkZstd(ROMFS)
        for actor in ('Obj_RemoteBombSphere','Obj_RemoteBombSpherePlus'):
            def actor_root(path):
                records={e.name:bytes(e.data) for e in oead.Sarc(z.decompress(path)).get_files()}
                return byml.Byml(records[f'Actor/{actor}.engine__actor__ActorParam.bgyml']).parse()
            accepted=actor_root(ROOT/f'mods/code/totk-remote-bomb/build/assets/romfs/Pack/Actor/{actor}.pack.zs')
            candidate=actor_root(BUILD/f'romfs/Pack/Actor/{actor}.pack.zs')
            expected=f'?ActorSystem/ActorName/{actor}.engine__actor__ActorName.bgyml'
            self.assertEqual(accepted['Components']['ActorNameRef'],expected)
            self.assertEqual(candidate['Components']['ActorNameRef'],expected)
            for key in ('ModelBindRef','ActorGameDataSetting','EventPerformerRef','PlacementMotionRef'):
                self.assertEqual(candidate['Components'][key],accepted['Components'][key],(actor,key))
            for key in ('EventMemberRef','NavMeshObjectRef'):
                self.assertNotIn(key,candidate['Components'])

    def test_explosion_damage_and_animation_match_port_inputs(self):
        z=TotkZstd(ROMFS)
        source=oead.Sarc(z.decompress(BUILD/'native-sphere-input/Pack/Actor/ExpandField_TimeBomb.pack.zs'))
        target={e.name:bytes(e.data) for e in oead.Sarc(
            z.decompress(BUILD/f'romfs/Pack/Actor/{BLAST}.pack.zs')).get_files()}
        checked=0
        for entry in source.get_files():
            if '/AttackParam/' in entry.name or '/ShapeParam/' in entry.name:
                parent,filename=entry.name.rsplit('/',1)
                self.assertEqual(byml.Byml(target[f'{parent}/{BLAST}_{filename}']).parse(),
                                 byml.Byml(bytes(entry.data)).parse())
                checked+=1
        self.assertGreater(checked,0)
        self.assertEqual(z.decompress(BUILD/'romfs/Model/TotkNativeSphere.anim.bfres.zs'),
                         (BUILD/'native-sphere-model/TotkNativeSphere.anim.bfres').read_bytes())

    def test_native_actors_and_cube_isolation(self):
        z=TotkZstd(ROMFS)
        out=BUILD/'romfs'
        for actor in ACTORS:
            records={e.name:bytes(e.data) for e in oead.Sarc(z.decompress(out/f'Pack/Actor/{actor}.pack.zs')).get_files()}
            root=byml.Byml(records[f'Actor/{actor}.engine__actor__ActorParam.bgyml']).parse()
            for key in ('PhysicsRef','GameParameterTableRef','ChemicalRef','ELinkRef'):
                self.assertIn(resource_path(root['Components'][key]),records,(actor,key))
            if actor != BLAST:
                info=byml.Byml(records[resource_path(root['Components']['ModelInfoRef'])]).parse()
                self.assertEqual(info['ModelProjectName'],'TotkNativeSphere')
                self.assertEqual(info['FmdbName'],'SpObj_TimerBomb_A_01')
                bodies=[byml.Byml(raw).parse() for name,raw in records.items() if '/RigidBodyEntityParam/' in name]
                if not actor.endswith('_Attachment'):
                    self.assertTrue(any(float(body.get('Mass',0))==25 for body in bodies),actor)
        if not (BUILD/'native-cube-receipt.json').exists():
            for actor in ('Obj_RemoteBombCube','Obj_RemoteBombCubePlus'):
                expected=ROOT/f'mods/code/totk-remote-bomb/build/assets/romfs/Pack/Actor/{actor}.pack.zs'
                self.assertEqual((out/f'Pack/Actor/{actor}.pack.zs').read_bytes(),expected.read_bytes())
        banks=byml.Byml(z.decompress(out/'Effect/EffectFileInfo.Product.110.Nin_NX_NVN.byml.zs')).parse()
        self.assertEqual(banks['BinaryDict'][EFFECT_USER],EFFECT_BANK)
        self.assertEqual(banks['BinaryDict']['TotkRemoteBomb'],'SpObj_TimerBomb_A_01')
        self.assertEqual(banks['EsetbList'].count(EFFECT_BANK),1)
        self.assertTrue((out/f'Effect/{EFFECT_BANK}.Nin_NX_NVN.esetb.byml.zs').exists())
        self.assertFalse((out/'Model/SpObj_TimerBomb_A.SpObj_TimerBomb_A_01.bfres.mc').exists())


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--build',type=Path)
    parser.add_argument('--romfs',type=Path)
    args=parser.parse_args()
    BUILD,ROMFS=args.build,args.romfs
    suite=unittest.TestLoader().loadTestsFromTestCase(FormatTests)
    if BUILD and ROMFS:
        suite.addTests(unittest.TestLoader().loadTestsFromTestCase(CandidateTests))
    result=unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(not result.wasSuccessful())
