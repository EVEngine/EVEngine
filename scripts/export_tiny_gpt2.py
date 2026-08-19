#!/usr/bin/env python3
"""Export a tiny GPT-2 (TinyStories-gpt2-3M by default) for the EVEngine tensor demo.

Downloads nothing itself: place the checkpoint + tokenizer files from a HuggingFace
GPT-2 repo under test/assets/mini_llm/ (default TinyStories model, from
calum/tinystories-gpt2-3M):

  tinystories_pytorch_model.bin   PyTorch state dict (torch .bin, parsed without torch)
  tinystories_config.json
  tinystories_vocab.json
  tinystories_merges.txt

Outputs:

  test/assets/mini_llm/tinystories.bin    engine weight bundle (name -> float32 tensor)
  test/assets/mini_llm/tokens.bin         token id -> decoded UTF-8 string table
  test/assets/mini_llm/prompt.txt         BPE-encoded prompt ids (comma separated)
  test/assets/mini_llm/reference_tokens.txt   numpy greedy reference generation ids
  test/assets/mini_llm/probs16.bin        numpy softmax probs [16, vocab] (float32)

Run:  python scripts/export_tiny_gpt2.py
"""

import json
import os
import pickle
import re
import struct
import sys
import zipfile
import collections

import numpy as np

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(ROOT), "test", "assets", "mini_llm")
OUT = SRC

PROMPT = "Once upon a time, there was a little"
GENERATE_TOKENS = 32
EPS = 1e-5
MODEL_PREFIX = "tinystories"


# ---------------------------------------------------------------- safetensors
def load_safetensors(path):
    with open(path, "rb") as f:
        n = struct.unpack("<Q", f.read(8))[0]
        header = json.loads(f.read(n))
        data = f.read()
    tensors = {}
    for name, meta in header.items():
        if name == "__metadata__":
            continue
        # The built-in causal mask / masked-bias are handled by our own kernel
        # mask; skip them to keep the engine bundle small.
        if name.endswith(".attn.bias") or name.endswith("attn.masked_bias"):
            continue
        dtype = meta["dtype"]
        start, end = meta["data_offsets"]
        if dtype == "F32":
            dt = np.float32
        elif dtype == "U8":
            dt = np.uint8
        elif dtype == "I64":
            dt = np.int64
        else:
            raise ValueError(f"unsupported dtype {dtype} for {name}")
        arr = np.frombuffer(data, dtype=dt, count=(end - start) // np.dtype(dt).itemsize,
                            offset=start).reshape(meta["shape"])
        tensors[name] = np.array(arr, dtype=np.float32)
    return tensors


def load_torch_bin(path):
    """Read a PyTorch state_dict .bin (zip + pickle) without importing torch."""
    zf = zipfile.ZipFile(path)
    member_prefix = next(n.split("/")[0] for n in zf.namelist()
                         if n.endswith("/data.pkl"))

    class FloatStorage:
        pass

    class LongStorage:
        pass

    class Storage:
        def __init__(self, data, esize):
            self.data, self.esize = data, esize

    class TensorInfo:
        def __init__(self, storage, offset, size, stride):
            self.storage, self.offset, self.size, self.stride = storage, offset, size, stride

    class TorchUnpickler(pickle.Unpickler):
        def find_class(self, module, name):
            if module == "collections" and name == "OrderedDict":
                return collections.OrderedDict
            if module == "torch._utils":
                if name in ("_rebuild_tensor_v2", "_rebuild_tensor"):
                    return lambda storage, offset, size, stride, *rest: TensorInfo(
                        storage, offset, size, stride)
                if name == "_rebuild_parameter":
                    return lambda tensor, *rest: tensor
            if module == "torch":
                if name == "FloatStorage":
                    return FloatStorage
                if name == "LongStorage":
                    return LongStorage
            raise pickle.UnpicklingError(f"{module}.{name}")

        def persistent_load(self, pid):
            kind, storage_cls, key, _device, _nelems = pid
            esize = 4 if storage_cls is FloatStorage else 8
            return Storage(zf.read(f"{member_prefix}/data/{key}"), esize)

    state = TorchUnpickler(zf.open(f"{member_prefix}/data.pkl")).load()
    tensors = {}
    for name, v in state.items():
        if not isinstance(v, TensorInfo):
            continue
        n = 1
        for d in v.size:
            n *= int(d)
        dtype = np.float32 if v.storage.esize == 4 else np.int64
        arr = np.frombuffer(v.storage.data, dtype=dtype, count=n,
                            offset=v.offset * v.storage.esize).reshape(v.size)
        tensors[name] = np.array(arr, dtype=np.float32)
    return tensors


# ------------------------------------------------------------ engine .bin I/O
def write_engine_bundle(tensors, path):
    with open(path, "wb") as f:
        f.write(b"EVLLM11")  # 8-byte magic (matches test/tensor_llm.cpp)
        f.write(struct.pack("<i", len(tensors)))
        for name in sorted(tensors):
            arr = np.ascontiguousarray(tensors[name], dtype=np.float32)
            nb = name.encode("utf-8")
            f.write(struct.pack("<i", len(nb)))
            f.write(nb)
            f.write(struct.pack("<i", arr.ndim))
            f.write(struct.pack("<%di" % arr.ndim, *arr.shape))
            f.write(struct.pack("<q", arr.nbytes))
            f.write(arr.tobytes())
    print(f"wrote {path} ({os.path.getsize(path)} bytes, {len(tensors)} tensors)")


def write_token_table(vocab, path):
    decode = bytes_to_unicode_reverse()
    with open(path, "wb") as f:
        f.write(struct.pack("<i", len(vocab)))
        for i in range(len(vocab)):
            token = next(k for k, v in vocab.items() if v == i)
            try:
                raw = bytes(decode[c] for c in token)
                text = raw.decode("utf-8", errors="replace")
            except KeyError:
                text = token  # special/added token such as <|endoftext|>
            tb = text.encode("utf-8")
            f.write(struct.pack("<i", len(tb)))
            f.write(tb)
    print(f"wrote {path} ({os.path.getsize(path)} bytes)")


# ---------------------------------------------------------------- tokenizer
def bytes_to_unicode():
    bs = list(range(ord("!"), ord("~") + 1)) + list(range(ord("¡"), ord("¬") + 1)) + \
        list(range(ord("®"), ord("ÿ") + 1))
    cs = bs[:]
    n = 0
    for b in range(256):
        if b not in bs:
            bs.append(b)
            cs.append(256 + n)
            n += 1
    return dict(zip(bs, [chr(c) for c in cs]))


def bytes_to_unicode_reverse():
    return {v: k for k, v in bytes_to_unicode().items()}


def load_bpe(vocab_path, merges_path):
    with open(vocab_path, "r", encoding="utf-8") as f:
        vocab = json.load(f)
    with open(merges_path, "r", encoding="utf-8") as f:
        merges = [tuple(line.rstrip("\n").split()) for line in f if " " in line]
    ranks = {pair: i for i, pair in enumerate(merges)}
    return vocab, ranks


def bpe_encode(text, vocab, ranks):
    pat = re.compile(
        r"""'s|'t|'re|'ve|'m|'ll|'d| ?[A-Za-z]+| ?[0-9]+| ?[^\sA-Za-z0-9]+|\s+""")
    byte_encoder = bytes_to_unicode()
    ids = []
    for word in pat.findall(text):
        chars = [byte_encoder[b] for b in word.encode("utf-8")]
        while len(chars) > 1:
            pair = min(
                ((ranks[(chars[i], chars[i + 1])], i) for i in range(len(chars) - 1)
                 if (chars[i], chars[i + 1]) in ranks),
                default=None,
            )
            if pair is None:
                break
            _, i = pair
            chars = chars[:i] + [chars[i] + chars[i + 1]] + chars[i + 2:]
        for tok in chars:
            ids.append(vocab[tok])
    return ids


# ---------------------------------------------------------------- numpy GPT-2
def gpt2_forward(tokens, w, n_layer, n_head, n_embd, n_inner, vocab_size):
    wte = w["transformer.wte.weight"]
    wpe = w["transformer.wpe.weight"]
    T = len(tokens)
    head = n_embd // n_head
    x = wte[np.array(tokens)] + wpe[np.arange(T)]
    for i in range(n_layer):
        p = f"transformer.h.{i}."
        h = layernorm(x, w[p + "ln_1.weight"], w[p + "ln_1.bias"])
        # GPT-2 Conv1D layers store weight as [in, out]: linear = x @ W + b.
        qkv = h @ w[p + "attn.c_attn.weight"] + w[p + "attn.c_attn.bias"]
        q, k, v = np.split(qkv, 3, axis=1)
        q = q.reshape(T, n_head, head).transpose(1, 0, 2)
        k = k.reshape(T, n_head, head).transpose(1, 0, 2)
        v = v.reshape(T, n_head, head).transpose(1, 0, 2)
        scores = (q @ k.transpose(0, 2, 1)) / np.sqrt(head)
        mask = np.triu(np.full((T, T), -np.inf, dtype=np.float32), k=1)[None]
        scores = scores + mask
        probs = softmax(scores, axis=-1)
        att = probs @ v
        att = att.transpose(1, 0, 2).reshape(T, n_embd)
        x = x + att @ w[p + "attn.c_proj.weight"] + w[p + "attn.c_proj.bias"]
        h = layernorm(x, w[p + "ln_2.weight"], w[p + "ln_2.bias"])
        h = gelu_new(h @ w[p + "mlp.c_fc.weight"] + w[p + "mlp.c_fc.bias"])
        x = x + h @ w[p + "mlp.c_proj.weight"] + w[p + "mlp.c_proj.bias"]
    h = layernorm(x, w["transformer.ln_f.weight"], w["transformer.ln_f.bias"])
    head = w.get("lm_head.weight", wte)  # untied LM head when present
    return h @ head.T


def layernorm(x, weight, bias):
    mu = x.mean(axis=-1, keepdims=True)
    var = x.var(axis=-1, keepdims=True)
    return (x - mu) / np.sqrt(var + EPS) * weight + bias


def gelu_new(x):
    return 0.5 * x * (1.0 + np.tanh(0.7978845608028654 * (x + 0.044715 * x ** 3)))


def softmax(x, axis=-1):
    e = np.exp(x - np.max(x, axis=axis, keepdims=True))
    return e / e.sum(axis=axis, keepdims=True)


def greedy_generate(tokens, w, cfg, max_tokens):
    out = list(tokens)
    for _ in range(max_tokens):
        logits = gpt2_forward(out, w, cfg["n_layer"], cfg["n_head"], cfg["n_embd"],
                              cfg["n_inner"], cfg["vocab_size"])
        nxt = int(np.argmax(logits[-1]))
        out.append(nxt)
        if nxt == cfg["eos_token_id"]:
            break
    return out


def decode_ids(ids, vocab):
    dec = bytes_to_unicode_reverse()
    text = ""
    for i in ids:
        token = next(k for k, v in vocab.items() if v == i)
        try:
            raw = bytes(dec[c] for c in token)
            text += raw.decode("utf-8", errors="replace")
        except KeyError:
            text += token
    return text


def main():
    torch_bin = os.path.join(SRC, f"{MODEL_PREFIX}_pytorch_model.bin")
    safetensors = os.path.join(SRC, "model.safetensors")
    if os.path.exists(torch_bin):
        w = load_torch_bin(torch_bin)
    elif os.path.exists(safetensors):
        w = load_safetensors(safetensors)
    else:
        sys.exit(f"missing {torch_bin} (or model.safetensors): download a tiny GPT-2 first")
    vocab, ranks = load_bpe(os.path.join(SRC, f"{MODEL_PREFIX}_vocab.json"),
                            os.path.join(SRC, f"{MODEL_PREFIX}_merges.txt"))
    with open(os.path.join(SRC, f"{MODEL_PREFIX}_config.json")) as f:
        cfg = json.load(f)
    if cfg.get("n_inner") is None:
        cfg["n_inner"] = 4 * cfg["n_embd"]

    write_engine_bundle(w, os.path.join(OUT, "tinystories.bin"))
    write_token_table(vocab, os.path.join(OUT, "tokens.bin"))

    prompt_ids = bpe_encode(PROMPT, vocab, ranks)
    with open(os.path.join(OUT, "prompt.txt"), "w", encoding="utf-8") as f:
        f.write(",".join(map(str, prompt_ids)))
    print(f"prompt {PROMPT!r} -> ids {prompt_ids}")

    # numpy reference: greedy generation first, then softmax probs at T=16 over
    # the same 16-token context the engine test will use.
    cfg.setdefault("n_inner", 4 * cfg["n_embd"])
    gen = greedy_generate(prompt_ids, w, cfg, GENERATE_TOKENS)
    with open(os.path.join(OUT, "reference_tokens.txt"), "w", encoding="utf-8") as f:
        f.write(",".join(map(str, gen)))
    print(f"numpy greedy generation: {decode_ids(gen, vocab)!r}")

    ref_tokens = gen[:16]
    logits = gpt2_forward(ref_tokens, w, cfg["n_layer"], cfg["n_head"], cfg["n_embd"],
                          cfg["n_inner"], cfg["vocab_size"])
    probs = softmax(logits, axis=-1).astype(np.float32)
    probs.tofile(os.path.join(OUT, "probs16.bin"))
    print(f"reference probs [{probs.shape[0]}, {probs.shape[1]}] -> probs16.bin")


if __name__ == "__main__":
    main()
