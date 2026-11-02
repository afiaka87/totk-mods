"""Build and test both patch variants for the selected owned game versions."""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

from native_profile_data import VERSIONS


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tool-root',type=Path,required=True)
    p.add_argument('--community',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--nso-121',type=Path,help='Owned clean 1.2.1 main NSO when that version is selected')
    p.add_argument('--versions',nargs='+',choices=VERSIONS,default=VERSIONS)
    p.add_argument('--variants',nargs='+',choices=('standard','restricted'),default=('standard','restricted'))
    p.add_argument('--reuse',action='append',default=[],metavar='VERSION/VARIANT',
                   help='Explicitly reuse a completed base build after checking its receipt')
    args = p.parse_args()
    if '1.2.1' in args.versions and args.nso_121 is None:
        p.error('--nso-121 is required when building 1.2.1')
    args.tool_root=args.tool_root.resolve();args.out=args.out.resolve()
    sys.path.insert(0,str(args.tool_root/'scripts'))
    from totk_version_port.images import IMAGE_LAYOUT
    tool = Path(__file__).resolve().parent
    jobs = [(v,k) for v in args.versions for k in args.variants]
    if set(args.reuse)-{v+'/'+k for v,k in jobs}:
        p.error('--reuse must name a selected version/variant')

    def build(version,variant):
        folder=args.out/version/variant;folder.mkdir(parents=True,exist_ok=True)
        base,combined=folder/'base',folder/'combined'
        def run(name,script,*options):
            result=subprocess.run([sys.executable,str(tool/script),*map(str,options)],capture_output=True,text=True)
            (folder/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
            if result.returncode:
                raise RuntimeError(f'{version}/{variant} {name} failed: {folder/(name+".log")}')
        if version+'/'+variant not in args.reuse:
            nso=(args.tool_root/IMAGE_LAYOUT[version]['nso'] if version!='1.2.1'
                 else args.nso_121)
            run('build','build_patch.py','--tool-root',args.tool_root,'--nso',nso,
                '--game-version',version,'--community',args.community,'--out',base,
                *(['--shrine-restricted'] if variant=='restricted' else []))
        validation=json.loads((base/'validated.json').read_text())
        report=json.loads((base/'report.json').read_text())
        if validation['research_only'] or validation['game_version']!=version:
            raise ValueError('Matrix base has an unfinished or wrong native profile')
        for name,digest in validation['payload_sha256'].items():
            if hashlib.sha256((base/name).read_bytes()).hexdigest()!=digest:
                raise ValueError('Matrix base payload differs from validation')
        if bool(report.get('shrine_gates'))!=(variant=='restricted'):
            raise ValueError('Matrix variant does not match its gates')
        run('compose','compose_compat_free.py','--root',args.tool_root,'--community',args.community,
            '--descend',base/'ultimate-ascend.pchtxt','--layout',base,'--out',combined,'--candidate')
        run('compatibility','test_compat_free.py','--root',args.tool_root,'--layout',base,
            '--patch',combined/'ultimate-ascend.pchtxt','--out',combined/'compatibility.json')
        composed=json.loads((combined/'composition.json').read_text())
        result=dict(version=version,variant=variant,base=str(base),combined=str(combined),
                    payload_sha256=composed['payload_sha256'],private_data_bytes=report['private_data_bytes'],
                    remaining_gaps=report['remaining_gap_counts'],
                    verdict='Offline build and compatibility pass; observed boot pending')
        (folder/'matrix-entry.json').write_text(json.dumps(result,indent=2)+'\n')
        return result

    results,failures=[],[]
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures={pool.submit(build,*job):job for job in jobs}
        for future in as_completed(futures):
            version,variant=futures[future]
            try:
                results.append(future.result())
                print(f'PASS {version}/{variant}',flush=True)
            except Exception as error:
                failures.append(dict(version=version,variant=variant,error=str(error)))
                print(f'FAIL {version}/{variant}: {error}',flush=True)
    summary=dict(results=sorted(results,key=lambda r:(r['version'],r['variant'])),failures=failures)
    (args.out/'matrix.json').write_text(json.dumps(summary,indent=2)+'\n')
    if failures:
        raise SystemExit(1)


if __name__=='__main__':
    main()
