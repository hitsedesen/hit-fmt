# Unicode data

`unicode_data.cpp` contains the `XID_Start` and `XID_Continue` ranges from
the Unicode 17.0.0 Character Database file
[`DerivedCoreProperties.txt`](https://www.unicode.org/Public/17.0.0/ucd/DerivedCoreProperties.txt).

The SHA-256 digest of the source file is:

```text
24c7fed1195c482faaefd5c1e7eb821c5ee1fb6de07ecdbaa64b56a99da22c08
```

Generate each range list with:

```sh
python3 scripts/category.py XID_Start DerivedCoreProperties.txt
python3 scripts/category.py XID_Continue DerivedCoreProperties.txt
```

The generated ranges are derived from Unicode data and are distributed under
the [Unicode License v3](../LICENSES/Unicode-3.0.txt). The rest of this
repository is distributed under the [MIT License](../LICENSE).
