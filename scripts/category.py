import sys
from dataclasses import dataclass


@dataclass
class Range:
    begin: int
    end: int


# Extract letter ranges from unicode DerivedCoreProperties.txt
def parse_property_ranges(lines):
    for line in lines:
        if not line.strip() or line.startswith("#"):
            continue
        data = line.split("#")[0]
        data = [s.strip() for s in data.split(";")]
        range_str, property = data[0], data[1]

        if ".." in range_str:
            begin, end = [int(s, 16) for s in range_str.split("..")]
        else:
            begin = end = int(range_str, 16)
        yield Range(begin, end), property


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <derived property> <path/to/DerivedCoreProperties.txt>")
        exit(1)
    matching_ranges = []
    properties = sys.argv[1].split(',')
    path = sys.argv[2]
    with open(path) as file:
        for r, property in parse_property_ranges(file):
            if property in properties:
                matching_ranges.append(r)
    matching_ranges.sort(key=lambda k: k.begin)
    for r in matching_ranges:
        print(f"{{0x{r.begin:x}, 0x{r.end:x}}},")


if __name__ == "__main__":
    main()
