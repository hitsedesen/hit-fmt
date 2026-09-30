# Unicode data

`unicode_data.cpp` contains the `XID_Start` and `XID_Continue` ranges from
the Unicode 18.0.0 Character Database file
[`DerivedCoreProperties.txt`](https://www.unicode.org/Public/18.0.0/ucd/DerivedCoreProperties.txt).

The SHA-256 digest of the source file is:

```text
09c928886a178fcafd93c29e4bd59073a058e5a100b716d425cb563ab50f68c9
```

Generate each range list with:

```sh
python3 scripts/category.py XID_Start DerivedCoreProperties.txt
python3 scripts/category.py XID_Continue DerivedCoreProperties.txt
```

The generated ranges are derived from Unicode data and are distributed under
the [Unicode License v3](../LICENSES/Unicode-3.0.txt). The rest of this
repository is distributed under the [MIT License](../LICENSE).
