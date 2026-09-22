import sys
from dataclasses import dataclass


@dataclass
class Range:
    begin: int
    end: int


# Extract ranges from Unicode's DerivedCoreProperties.txt format.
def parse_property_ranges(lines):
    for line in lines:
        if not line.strip() or line.startswith("#"):
            continue
        data = line.partition("#")[0]
        range_text, property_name = [part.strip() for part in data.split(";", 1)]

        if ".." in range_text:
            begin, end = [int(value, 16) for value in range_text.split("..")]
        else:
            begin = end = int(range_text, 16)
        yield Range(begin, end), property_name


def main():
    if len(sys.argv) != 3:
        print(
            f"Usage: {sys.argv[0]} <derived properties> "
            "<path/to/DerivedCoreProperties.txt>"
        )
        sys.exit(1)

    property_names = set(sys.argv[1].split(","))
    matching_ranges = []
    with open(sys.argv[2], encoding="utf-8") as source:
        for code_point_range, property_name in parse_property_ranges(source):
            if property_name in property_names:
                matching_ranges.append(code_point_range)

    matching_ranges.sort(key=lambda code_point_range: code_point_range.begin)
    for code_point_range in matching_ranges:
        print(f"{{0x{code_point_range.begin:x}, 0x{code_point_range.end:x}}},")


if __name__ == "__main__":
    main()
