#!/usr/bin/env python3
"""Exercise native Kokoro INT8 inference with explicit phonemes and a selected voice.

Python/ONNX/NumPy prepare test inputs and WAV output only. Inference runs in the
native probe, without ONNX Runtime. Default phonemes say Chinese 'ni hao'.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import wave

import numpy as np
import onnx


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model', type=Path, required=True)
    parser.add_argument('--voices', type=Path, required=True)
    parser.add_argument('--tokens', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--gpu', action='store_true')
    parser.add_argument('--repeat', type=int, choices=[1, 2], default=1)
    parser.add_argument('--speaker', type=int, default=3)
    parser.add_argument('--phonemes', default='ㄋ ㄧ 2 ㄏ ㄠ 3 .')
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    model = onnx.load(args.model, load_external_data=False)
    metadata = {entry.key: entry.value for entry in model.metadata_props}
    dims = [int(d) for d in metadata['style_dim'].split(',')]
    speakers = int(metadata['n_speakers'])
    if dims[1:] != [1, 256] or not 0 <= args.speaker < speakers:
        raise ValueError('Unsupported voice shape or speaker index')
    vocab = {line.rsplit(' ', 1)[0]: int(line.rsplit(' ', 1)[1])
             for line in args.tokens.read_text(encoding='utf-8').splitlines()}
    phones = args.phonemes.split()
    if not 0 < len(phones) < dims[0]:
        raise ValueError('Phoneme count exceeds the voice table')
    ids = [0] + [vocab[p] for p in phones] + [0]
    voices = np.memmap(args.voices, mode='r', dtype='<f4')
    if voices.size != speakers * dims[0] * dims[2]:
        raise ValueError('Voice table size does not match model metadata')
    # sherpa-onnx indexes style by phoneme length, excluding the two boundary zeros.
    style = voices.reshape(speakers, dims[0], dims[2])[args.speaker, len(phones)]
    style_path = args.work / 'style.f32'
    style.tofile(style_path)
    raw = args.work / 'audio.f32'
    env = os.environ.copy()
    env['EVE_ONNX_GPU'] = '1' if args.gpu else '0'
    env.pop('EVE_ONNX_CHECK_FINITE', None)
    env['EVE_ONNX_REPEAT'] = str(args.repeat)
    started = time.perf_counter()
    run = subprocess.run([str(args.probe.resolve()), str(args.model.resolve()), 'audio',
                          str(raw.resolve()), ','.join(map(str, ids)), str(style_path.resolve())],
                         env=env, capture_output=True, text=True, encoding='utf-8', errors='replace')
    seconds = time.perf_counter() - started
    (args.work / 'native.log').write_text(run.stdout + run.stderr, encoding='utf-8')
    if run.returncode or 'ONNX_RUN_PASS' not in run.stdout:
        raise RuntimeError('Native inference failed; see ' + str(args.work / 'native.log'))
    dispatches = int(re.search(r'dispatches=(\d+)', run.stdout).group(1))
    if args.gpu and dispatches == 0:
        raise RuntimeError('GPU run completed without any GPU dispatch')
    audio = np.fromfile(raw, '<f4')
    if not audio.size or not np.isfinite(audio).all() or np.max(np.abs(audio)) == 0:
        raise RuntimeError('Empty, nonfinite or silent audio output')
    sample_rate = int(metadata['sample_rate'])
    with wave.open(str(args.work / 'audio.wav'), 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes((np.clip(audio, -1, 1) * 32767).astype('<i2').tobytes())
    report = dict(schema='eve.tensor.kokoro-probe/1', model_sha256=hashlib.sha256(args.model.read_bytes()).hexdigest(),
                  model_bytes=args.model.stat().st_size, speaker=args.speaker, phonemes=phones,
                  token_ids=ids, backend='gpu' if args.gpu else 'cpu', gpu_dispatches=dispatches,
                  samples=int(audio.size), sample_rate=sample_rate, peak=float(np.max(np.abs(audio))),
                  inference_wall_seconds=seconds, audio_seconds=audio.size / sample_rate)
    report['iterations'] = [{key: (float(value) if key == 'ms' else int(value))
                             for key, value in re.findall(r'(\w+)=([\d.]+)', line)}
                            for line in re.findall(r'ONNX_ITERATION ([^\r\n]+)', run.stdout)]
    if args.repeat == 2 and ('ONNX_REPEAT_IDENTICAL_PASS' not in run.stdout or len(report['iterations']) != 2):
        raise RuntimeError('Missing repeated-inference equivalence proof')
    transfers = re.search(r'ONNX_TRANSFERS ([^\r\n]+)', run.stdout)
    if transfers:
        report['transfers'] = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', transfers.group(1))}
    (args.work / 'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('KOKORO_ONNX_PASS', args.work / 'audio.wav', f'{dispatches} GPU dispatches, {seconds:.3f}s wall time')


if __name__ == '__main__':
    main()
