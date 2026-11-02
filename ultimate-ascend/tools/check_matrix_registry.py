"""Check every composed matrix payload against the current known-writes registry."""
import argparse
import hashlib
import json
import sys
from pathlib import Path

from native_profile_data import VERSIONS
from patch_registry import owned_sources


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True)
    p.add_argument('--matrix',type=Path,required=True)
    p.add_argument('--community',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    args=p.parse_args();sys.path.insert(0,str(args.root/'scripts'))
    from patch_gaps import registry
    from patch_gaps.formats import parse_pchtxt
    from totk_version_port.images import BUILD_IDS
    from community_port import SOURCE_SHA256
    if hashlib.sha256(args.community.read_bytes()).hexdigest()!=SOURCE_SHA256:
        raise ValueError('Unsupported compatibility baseline')
    original=parse_pchtxt(args.community.read_text())[1]
    def hits(writes,ranges):
        return {(sid,label) for w in writes if w.enabled for a,b,sid,label in ranges
                if a < w.off+len(w.data) and w.off < b}
    results=[]
    for version in VERSIONS:
        bid=BUILD_IDS[version]
        reg=registry.load(bid)
        ranges=registry.all_ranges(reg,skip=owned_sources(reg))
        digest=hashlib.sha256(registry.path_for(bid).read_bytes()).hexdigest()
        inherited=hits(original,ranges) if version=='1.2.1' else set()
        for variant in ('standard','restricted'):
            folder=args.matrix/version/variant
            report=json.loads((folder/'base/report.json').read_text())
            if report['registry_sha256']!=digest:
                raise ValueError(f'Registry changed after build: {version}')
            first=parse_pchtxt((folder/'base/ultimate-ascend.pchtxt').read_text())[1]
            combined=parse_pchtxt((folder/'combined/ultimate-ascend.pchtxt').read_text())[1]
            if hits(first,ranges) or hits(combined,ranges)!=inherited:
                raise ValueError(f'New registry conflict: {version}/{variant}')
            if version=='1.2.1':
                def tail(writes):
                    return {w.off+i:b for w in writes if w.enabled for i,b in enumerate(w.data)
                            if report['native_profile']['text_end'] <= w.off+i <
                            (report['native_profile']['text_end']+0xfff)&~0xfff}
                if tail(combined)!=tail(original):
                    raise ValueError('Inherited community tail changed')
            results.append(dict(version=version,variant=variant,new_overlaps=0,
                                inherited_labels=len(inherited),registry_sha256=digest,
                                registry_sources=len(registry.load(bid)['sources'])))
    args.out.write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps(dict(payloads=len(results),new_overlaps=0,
                         inherited_121_labels=len(inherited) if version=='1.2.1' else
                         next(r['inherited_labels'] for r in results if r['version']=='1.2.1'))))


if __name__=='__main__':
    main()
