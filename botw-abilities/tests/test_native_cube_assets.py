# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Check live cube references, inherited allocations and accepted sphere isolation."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build_native_cube import ACTORS, CUBES, PROJECT, MODEL
from build_native_sphere import pack, resource_path, TotkZstd, byml, BLAST
from build_actors import budgets
from native_sphere_budgets import cloned_resource_floor


class CubeTests(unittest.TestCase):
    def test_live_cube_identity_collision_and_death_drop(self):
        z=TotkZstd(ARGS.romfs)
        for actor in ACTORS:
            data=pack(z.decompress(ARGS.build/f'romfs/Pack/Actor/{actor}.pack.zs'))
            def read(link):return byml.Byml(data[resource_path(link)]).parse()
            root=read(f'?Actor/{actor}.engine__actor__ActorParam.bgyml')
            info=read(root['Components']['ModelInfoRef'])
            self.assertEqual((info['ModelProjectName'],info['FmdbName']),(PROJECT,MODEL))
            if actor in CUBES:
                self.assertEqual(root['Components']['ActorNameRef'],f'?ActorSystem/ActorName/{actor}.engine__actor__ActorName.bgyml')
                self.assertEqual(root['Category'],'ObjectPouchContent')
            drop=read(root['Components']['DropRef'])
            table=read(drop['DropTableResourceList'][0])
            element=read(table['DropTableElementResourceList'][0])['DropTableElement'][0]
            self.assertEqual(element['DropActorName'],f'Work/Actor/{BLAST}.engine__actor__ActorParam.gyml')
            self.assertEqual(element['DropProbability'],100)
            controllers=[byml.Byml(raw).parse() for name,raw in data.items() if '/ControllerSetParam/' in name]
            self.assertEqual(len(controllers),1)
            physical=next(row['FilePath'] for row in controllers[0]['ShapeNamePathAry'] if row['Name']=='Physical')
            geometry=read(physical)
            accepted=pack(z.decompress(ROOT/f'mods/code/totk-remote-bomb/build/assets/romfs/Pack/Actor/{actor.removesuffix("_Attachment")}.pack.zs'))
            expected=next(byml.Byml(raw).parse() for name,raw in accepted.items() if name.endswith('_BarrelBomb_Body_0.phive__ShapeParam.bgyml'))
            self.assertEqual(geometry,expected,actor)
            self.assertIn('Polytope',geometry);self.assertNotIn('Sphere',geometry)
            chemical=read(root['Components']['ChemicalRef'])
            self.assertFalse(read(chemical['Object'][0]['ChemicalMaterial'])['IsExplode'])

    def test_native_cube_budgets_reach_the_finished_table(self):
        z=TotkZstd(ARGS.romfs)
        actual=budgets(z.decompress(ARGS.build/'release-restbl/System/Resource/ResourceSizeTable.Product.121.rsizetable.zs'))
        self.assertGreaterEqual(actual(f'Model/{PROJECT}.{MODEL}.bfres'),524288)
        proof=json.loads((ARGS.build/'native-cube-receipt.json').read_text())
        self.assertGreater(len(proof['parameter_budgets']),200)
        for row in proof['parameter_budgets']:
            self.assertGreaterEqual(actual(row['clone']),cloned_resource_floor(row['source_budget'],row['source_bytes'],row['cloned_bytes']),row['clone'])
        for texture in proof['textures']:
            path=ARGS.build/f'romfs/TexToGo/{texture["name"]}.txtg'
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),texture['sha256'])

    def test_cube_attachments_are_registered_and_shared_sphere_assets_unchanged(self):
        z=TotkZstd(ARGS.romfs)
        for family in ('ActorInfo','GameActorInfo'):
            rows=byml.Byml(z.decompress(ARGS.build/f'romfs/RSDB/{family}.Product.121.rstbl.byml.zs')).parse()
            for actor in ACTORS:self.assertEqual(sum(r['__RowId']==actor for r in rows),1)
        receipt=json.loads(ARGS.sphere_receipt.read_text())
        checked=0
        for entry in receipt['artifact']['files']:
            name=entry['path']
            if any(fragment in name for fragment in ('Pack/Actor/Obj_RemoteBombSphere','Pack/Actor/Obj_NativeSphereBlast',
                    'Model/TotkNativeSphere','Effect/','ELink2/','TexToGo/RemoteBombBall')):
                self.assertEqual(hashlib.sha256((ARGS.build/name).read_bytes()).hexdigest(),entry['sha256'],name)
                checked+=1
        self.assertGreaterEqual(checked,14)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    for key in ('build','romfs','sphere-receipt'):parser.add_argument('--'+key,type=Path,required=True)
    ARGS=parser.parse_args()
    unittest.main(argv=[sys.argv[0]],verbosity=2)
