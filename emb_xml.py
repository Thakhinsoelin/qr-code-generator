

from pathlib import Path

SOURCE = Path("assets/helloO.xml")
OUTPUT = Path("src/hello_xml.h")


def escape_c(data: bytes) -> str:
    result = []

    for b in data:
        if b == 34:          # "
            result.append(r'\"')
        elif b == 92:        # \
            result.append(r'\\')
        elif b == 10:        # newline
            result.append(r'\n')
        elif b == 13:        # carriage return
            result.append(r'\r')
        elif b == 9:         # tab
            result.append(r'\t')
        elif 32 <= b <= 126:
            result.append(chr(b))
        else:
            # Fixed-width octal escape preserves UTF-8 bytes
            result.append(f'\\{b:03o}')

    return ''.join(result)


def main():
    if not SOURCE.exists():
        raise FileNotFoundError(f"XML file not found: {SOURCE}")

    data = SOURCE.read_bytes()
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)

    lines = data.splitlines(keepends=True)

    with OUTPUT.open("w", encoding="ascii", newline="\n") as f:
        f.write("#pragma once\n")
        f.write("#include <stddef.h>\n\n")

        f.write("static const char hello_xml[] =\n")

        if not lines:
            f.write('    ""\n')
        else:
            for line in lines:
                f.write(f'    "{escape_c(line)}"\n')

        f.write(";\n\n")
        f.write("static const size_t hello_xml_size = ")
        f.write("sizeof(hello_xml) - 1;\n")

    print(f"Successfully embedded {len(data)} bytes.")
    print(f"Output: {OUTPUT}")


if __name__ == "__main__":
    main()

