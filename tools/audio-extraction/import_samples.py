"""Import the 36 extracted SIFT WAV resources, preserving their original bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import wave

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('extracted', type=Path, help='Output directory from Extract.csproj')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
dest = root / 'assets/handpan'
keys = 'BFGHJKTYU'
labels = ['低6', '3', '4', '5', '6', '7', '高1', '高2', '高3']
midi = [45, 52, 53, 55, 57, 59, 60, 62, 64]
resources = json.loads((args.extracted / 'resource-manifest.json').read_text(encoding='utf-8-sig'))
bundle = json.loads((args.extracted / 'bundle-manifest.json').read_text(encoding='utf-8-sig'))
expected = {f'{key}_{variant}.wav' for key in keys for variant in range(1, 5)}
assert len(resources) == 36
assert {Path(r['file']).name for r in resources} == expected
pending, records = [], []
for entry in resources:
    name = Path(entry['file']).name
    # Only known local basenames are read, regardless of paths in the manifest.
    path = args.extracted / 'all_samples' / name
    data = path.read_bytes()
    assert len(data) == entry['length'] and hashlib.sha256(data).hexdigest() == entry['sha256']
    with wave.open(str(path), 'rb') as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getcomptype()) == (2, 2, 44100, 'NONE')
        frames = wav.getnframes()
        assert len(wav.readframes(frames)) == frames * 4
    key, variant = name[0], int(name[2])
    index = keys.index(key)
    records.append(dict(file=name, key=key, label=labels[index], target_midi=midi[index],
                        variant=variant, frames=frames, duration_seconds=frames/44100,
                        resource=entry['resourceName'], assembly=entry['assembly'],
                        bytes=len(data), sha256=entry['sha256']))
    pending.append((name, data))
source = Path(bundle['source'])
manifest = dict(source=source.name, source_version='0.1.4',
                source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                sample_rate=44100, channels=2, bits_per_sample=16,
                processing='Original embedded WAV bytes; no trimming, normalization or resampling.',
                playback_variants='Round-robin per key in timeline order; stable across pause/resume and seek.',
                notes=records)
dest.mkdir(parents=True, exist_ok=True)
for name, data in pending:
    (dest / name).write_bytes(data)
(dest / 'manifest.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
qrc = ['<RCC>', '  <qresource prefix="/handpan">', '    <file alias="manifest.json">handpan/manifest.json</file>']
for key in keys:
    for variant in range(1, 5):
        name = f'{key}_{variant}.wav'
        qrc.append(f'    <file alias="{name}">handpan/{name}</file>')
qrc += ['  </qresource>', '</RCC>']
(root / 'assets/soundbank.qrc').write_text('\n'.join(qrc) + '\n', encoding='utf-8')
print('Imported and verified all 36 WAV resources; generated manifest and Qt resources.')
