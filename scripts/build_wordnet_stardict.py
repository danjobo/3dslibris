#!/usr/bin/env python3
"""Builds the bundled English dictionary from Princeton WordNet 3.0.

Writes a StarDict dictionary (wordnet.ifo, wordnet.idx, wordnet.dict.dz)
that the reader's word lookup reads (source/dictionary/stardict.cpp).

Input: a WordNet "dict" directory holding index.{noun,verb,adj,adv},
data.{noun,verb,adj,adv} and the {noun,verb,adj,adv}.exc irregular-form
lists, e.g. from https://wordnetcode.princeton.edu/3.0/WordNet-3.0.tar.gz.

    python3 scripts/build_wordnet_stardict.py <wordnet>/dict \
        sdmc/3ds/3dslibris/dict/wordnet

Each article starts with the headword on its own line, then one block per
part of speech with numbered senses. Irregular forms ("mice", "went") get
their own index entries pointing at the base word's article. Regular
inflections are handled at lookup time (dictionary/word_lookup_utils).
"""

import os
import struct
import sys
import zlib

POS = [("noun", "n", "noun"), ("verb", "v", "verb"), ("adj", "a", "adjective"),
       ("adv", "r", "adverb")]
DICTZIP_CHUNK = 58315  # dictzip's default chunk length


def read_index(path):
    """lemma -> list of synset offsets, most frequent sense first."""
    out = {}
    with open(path, "r", encoding="latin-1") as f:
        for line in f:
            if line.startswith(" "):
                continue  # license header
            parts = line.split()
            lemma = parts[0]
            p_cnt = int(parts[3])
            synset_cnt = int(parts[2])
            offsets = parts[6 + p_cnt:6 + p_cnt + synset_cnt]
            out[lemma] = offsets
    return out


def clean_word(word):
    # Adjective position markers: "big(a)", "galore(ip)".
    if word.endswith(")") and "(" in word:
        word = word[:word.rindex("(")]
    return word.replace("_", " ")


def read_data(path):
    """synset offset -> (words, gloss)."""
    out = {}
    with open(path, "r", encoding="latin-1") as f:
        for line in f:
            if line.startswith(" "):
                continue
            head, _, gloss = line.partition("|")
            parts = head.split()
            w_cnt = int(parts[3], 16)
            words = [clean_word(parts[4 + 2 * i]) for i in range(w_cnt)]
            out[parts[0]] = (words, gloss.strip())
    return out


def read_exceptions(path):
    """inflected form -> base forms."""
    out = {}
    with open(path, "r", encoding="latin-1") as f:
        for line in f:
            parts = line.split()
            if len(parts) >= 2:
                out.setdefault(parts[0], []).extend(parts[1:])
    return out


def article(lemma, by_pos, data):
    display = lemma.replace("_", " ")
    lines = [display]
    for name, _, label in POS:
        offsets = by_pos.get(name)
        if not offsets:
            continue
        lines.append(label)
        for n, offset in enumerate(offsets, 1):
            words, gloss = data[name][offset]
            others = []
            for w in words:
                if w.lower() != display.lower() and w not in others:
                    others.append(w)
            text = "%d. %s" % (n, gloss)
            if others:
                text += " (also: %s)" % ", ".join(others[:6])
            lines.append(text)
    return "\n".join(lines)


def stardict_key(word_bytes):
    # StarDict's order: g_ascii_strcasecmp, then strcmp.
    lower = bytes(b + 32 if 65 <= b <= 90 else b for b in word_bytes)
    return (lower, word_bytes)


def write_dictzip(path, data):
    chunks = [data[i:i + DICTZIP_CHUNK]
              for i in range(0, len(data), DICTZIP_CHUNK)]
    comp = zlib.compressobj(9, zlib.DEFLATED, -15)
    parts = []
    for i, chunk in enumerate(chunks):
        flush = zlib.Z_FINISH if i == len(chunks) - 1 else zlib.Z_FULL_FLUSH
        parts.append(comp.compress(chunk) + comp.flush(flush))
    sizes = [len(p) for p in parts]
    assert max(sizes) < 65536
    ra = struct.pack("<HHH", 1, DICTZIP_CHUNK, len(chunks)) + \
        b"".join(struct.pack("<H", s) for s in sizes)
    extra = b"RA" + struct.pack("<H", len(ra)) + ra
    header = b"\x1f\x8b\x08\x04" + struct.pack("<I", 0) + b"\x02\x03" + \
        struct.pack("<H", len(extra)) + extra
    trailer = struct.pack("<II", zlib.crc32(data) & 0xFFFFFFFF,
                          len(data) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(header)
        for p in parts:
            f.write(p)
        f.write(trailer)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, out_dir = sys.argv[1], sys.argv[2]
    index = {}
    data = {}
    exceptions = {}
    for name, _, _ in POS:
        data[name] = read_data(os.path.join(src, "data." + name))
        for lemma, offsets in read_index(
                os.path.join(src, "index." + name)).items():
            index.setdefault(lemma, {})[name] = offsets
        for form, bases in read_exceptions(
                os.path.join(src, name + ".exc")).items():
            for base in bases:
                if base not in exceptions.setdefault(form, []):
                    exceptions[form].append(base)

    body = bytearray()
    location = {}
    for lemma in sorted(index):
        text = article(lemma, index[lemma], data).encode("utf-8")
        location[lemma] = (len(body), len(text))
        body += text

    entries = []
    for lemma, loc in location.items():
        entries.append((lemma.replace("_", " ").encode("utf-8"), loc))
    for form, bases in exceptions.items():
        key = form.replace("_", " ").encode("utf-8")
        for base in bases:
            if base in location and base != form:
                entries.append((key, location[base]))
    def own_article(entry):
        key, loc = entry
        return location.get(key.decode("utf-8").replace(" ", "_")) == loc

    # Same key: the word's own article before the ones it is a form of.
    entries = sorted(set(entries),
                     key=lambda e: (stardict_key(e[0]), not own_article(e),
                                    e[1][0]))

    idx = bytearray()
    for key, (offset, size) in entries:
        idx += key + b"\0" + struct.pack(">II", offset, size)

    os.makedirs(out_dir, exist_ok=True)
    base = os.path.join(out_dir, "wordnet")
    with open(base + ".idx", "wb") as f:
        f.write(idx)
    write_dictzip(base + ".dict.dz", bytes(body))
    with open(base + ".ifo", "w", encoding="utf-8", newline="\n") as f:
        f.write("StarDict's dict ifo file\n")
        f.write("version=2.4.2\n")
        f.write("wordcount=%d\n" % len(entries))
        f.write("idxfilesize=%d\n" % len(idx))
        f.write("bookname=WordNet\n")
        f.write("author=Princeton University\n")
        f.write("website=https://wordnet.princeton.edu/\n")
        f.write("description=WordNet 3.0 (c) 2006 Princeton University. "
                "See LICENSE.txt.\n")
        f.write("sametypesequence=m\n")
    print("%d entries, idx %d bytes, articles %d bytes, dict.dz %d bytes" %
          (len(entries), len(idx), len(body),
           os.path.getsize(base + ".dict.dz")))


if __name__ == "__main__":
    main()
