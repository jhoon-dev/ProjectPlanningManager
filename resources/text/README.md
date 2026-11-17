# Pinned Unicode text conversion data

`unicode-16.0.0.inc` is generated from the official Unicode16.0 UCD by
`scripts/generate-unicode-format-data.py`. The manifest records the archive and
individual source hashes. Full default upper/lower/title mappings, Cased and
Case_Ignorable ranges, width mappings, and kana voicing pairs are embedded in
the domain library. Locale-specific Turkic/Lithuanian casing is not enabled by
the three supported profiles. See LICENSE.txt (Unicode License V3).

Rebuild: `python scripts/generate-unicode-format-data.py path/to/UCD.zip`.
The generator never consults Python/OS casing data or downloads at runtime.
