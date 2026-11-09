from hashlib import sha256
from pathlib import Path
from struct import pack
from subprocess import run
from sys import argv
from tempfile import TemporaryDirectory


def name(value):
    value = value.encode()
    return pack("<H", len(value)) + value


def tag(kind, key, payload):
    return bytes([kind]) + name(key) + payload


def compound(*tags):
    return b"".join(tags) + b"\x00"


def nbt_list(key, *entries, kind=10):
    return tag(9, key, pack("<Bi", kind, len(entries)) + b"".join(entries))


def variant(value, kind=3):
    return tag(kind, "Variant", pack({2: "<h", 3: "<i", 4: "<q"}[kind], value))


def metadata(*chunks):
    data = compound(nbt_list("LimboEntities", *chunks), tag(10, "DragonFight", compound(variant(4399))))
    return tag(10, "", compound(tag(10, "data", data)))


def chunk(*entities):
    return compound(variant(4288), nbt_list("EntityTagList", *entities))


def color_lines(first, second):
    return b"".join(
        f"\x1b[{32 if present else 31}m{value}\x1b[0m\n".encode()
        for value, present in zip((4288, 4399), (first, second))
    )


def check(result, expected, code):
    assert (result.returncode, result.stdout, result.stderr) == (code, color_lines(*expected), b""), result


def fingerprint(directory):
    return {item.name: sha256(item.read_bytes()).digest() for item in directory.iterdir() if item.is_file()}


def main():
    binary, writer = (Path(path).resolve() for path in argv[1:])
    decoy = compound(tag(10, "Other", compound(variant(4288), variant(4399))), tag(8, "Variant", name("4399")))
    unrelated = compound(variant(6059), tag(12, "Array", pack("<iq", 1, 4288)))
    match_first = compound(variant(4288))
    match_second = compound(variant(4399, kind=4))
    all_matches = metadata(chunk(match_first), chunk(match_second))
    cases = [
        ("neither", metadata(chunk(decoy, unrelated, compound(variant(3127)))), "TheEnd", (False, False), 0),
        ("first", metadata(chunk(unrelated), chunk(match_first)), "TheEnd", (True, False), 0),
        ("second", metadata(chunk(match_second, unrelated)), "TheEnd", (False, True), 0),
        ("both", all_matches, "TheEnd", (True, True), 0),
        ("short", metadata(chunk(compound(variant(4288, kind=2)))), "TheEnd", (True, False), 0),
        ("empty limbo", metadata(), "TheEnd", (False, False), 0),
        ("empty End-typed list", tag(10, "", compound(tag(10, "data", compound(nbt_list("LimboEntities", kind=0))))), "TheEnd", (False, False), 0),
        ("no limbo tag", tag(10, "", compound(tag(10, "data", compound(variant(4288))))), "TheEnd", (False, False), 0),
        ("wrong key", all_matches, "Overworld", (False, False), 0),
        ("truncated", all_matches[:-1], "TheEnd", (False, False), 1),
        ("invalid list length", tag(10, "", compound(tag(10, "data", compound(tag(9, "LimboEntities", pack("<Bi", 10, -1)))))), "TheEnd", (False, False), 1),
    ]

    with TemporaryDirectory() as temp:
        root = Path(temp)
        nbt = root / "metadata.nbt"
        for label, payload, key, expected, code in cases:
            world = root / f"测试存档 {label}"
            world.mkdir()
            db = world / "db"
            nbt.write_bytes(payload)
            run([writer, db, key, nbt], check=True)
            assert list(db.glob("*.ldb")), label
            before = fingerprint(db)
            check(run([binary, world], capture_output=True), expected, code)
            assert fingerprint(db) == before, label
        check(run([binary, root / "missing"], capture_output=True), (False, False), 1)
        check(run([binary, root, root], capture_output=True), (False, False), 2)
    print(f"{len(cases) + 2} checks passed")


if __name__ == "__main__":
    main()
