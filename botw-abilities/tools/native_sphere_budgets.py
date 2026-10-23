# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
"""Preserve native allocation budgets when importing or renaming sphere resources."""
from pathlib import Path
import sys

import oead

ROOT = Path(__file__).resolve().parents[4]
sys.path.append(str(ROOT/'mods/code/totk-cryonis/tools'))
from build_pillar import cloned_resource_floor
from build_actors import budgets

STOCK_ACTORS = ('SpObj_TimerBomb_A_01', 'TimerBarrelBomb', 'BarrelBomb', 'BombFruit',
                'ExpandField_GoronSage', 'Weapon_Sword_070', 'SpObj_Cannon_A_01', 'Enemy_Bokoblin_Junior')


def resource_kind(path):
    parent, filename = path.rsplit('/', 1)
    return parent, filename.split('.', 1)[1]


class NativeBudgets:
    def __init__(self, romfs, z, accepted_floors):
        self.stock = budgets(z.decompress(romfs/'System/Resource/ResourceSizeTable.Product.121.rsizetable.zs'))
        self.accepted = {name:int(size) for name,size in accepted_floors.items()}
        self.resources = {}
        self.packs = {}
        for actor in STOCK_ACTORS:
            name=f'Pack/Actor/{actor}.pack'
            raw=z.decompress(romfs/(name+'.zs'))
            self.packs[name]=len(raw)
            for entry in oead.Sarc(raw).get_files():
                if self.stock(entry.name):
                    self.resources[entry.name] = len(entry.data)

    def parameter(self, original, clone, original_bytes, cloned_bytes):
        if self.stock(original):
            source, size, count, basis = original, self.stock(original), self.resources.get(original,0), 'stock'
        elif self.accepted.get(original):
            source, size, count, basis = original, self.accepted[original], original_bytes, 'accepted'
        else:
            candidates = [(path, self.stock(path), count) for path,count in self.resources.items()
                          if resource_kind(path)==resource_kind(original)]
            if not candidates:
                raise ValueError('No verified native allocation analogue: '+original)
            source,size,count = max(candidates, key=lambda item:
                (cloned_resource_floor(item[1],item[2],cloned_bytes),item[0]))
            basis = 'same native resource type'
        floor=max(self.accepted.get(clone,0),cloned_resource_floor(size,count,cloned_bytes))
        return dict(original=original,clone=clone,source=source,basis=basis,
                    source_budget=size,source_bytes=count,cloned_bytes=cloned_bytes,floor=floor)

    def actor_pack(self, donor, cloned_bytes):
        source=f'Pack/Actor/{donor}.pack'
        if not self.stock(source):
            source='Pack/Actor/'+('ExpandField_GoronSage' if donor.startswith('ExpandField') else STOCK_ACTORS[0])+'.pack'
        size=self.stock(source)
        if size<=0:
            raise ValueError('Missing native actor pack budget: '+source)
        return max(cloned_bytes+65536,size+max(0,cloned_bytes-self.packs[source])+65536)
