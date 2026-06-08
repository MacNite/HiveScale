"""Tests for SD-card bulk-import de-duplication.

Run: python3 server/test_sd_import.py   (no database or FastAPI required)

Re-uploading the same SD download must never create duplicate measurement rows.
The natural key is (device_id, measured_at); within one import request the device
is fixed by the URL, so we de-duplicate on measured_at alone. These tests cover
the pure logic in sd_import.split_new_and_duplicate, which the import endpoint
uses to decide which rows to insert.
"""

from datetime import datetime, timedelta, timezone

from sd_import import split_new_and_duplicate


NOW = datetime(2024, 6, 15, 12, 0, tzinfo=timezone.utc)

_failures = 0


def check(name, condition):
    global _failures
    status = "ok" if condition else "FAIL"
    if not condition:
        _failures += 1
    print(f"[{status}] {name}")


def _ts(minutes):
    return NOW + timedelta(minutes=minutes)


# 1. Fresh import into an empty device: everything is new, nothing duplicated.
keys = [_ts(0), _ts(10), _ts(20)]
new, dupes = split_new_and_duplicate(keys, set())
check("fresh import keeps all rows", new == keys)
check("fresh import reports no duplicates", dupes == 0)

# 2. The SD backup file commonly contains the same line twice (retry cache replay).
keys = [_ts(0), _ts(0), _ts(10)]
new, dupes = split_new_and_duplicate(keys, set())
check("in-file duplicate collapsed to one row", new == [_ts(0), _ts(10)])
check("in-file duplicate counted once", dupes == 1)

# 3. Re-uploading the exact same file a second time inserts nothing.
existing = {_ts(0), _ts(10), _ts(20)}
keys = [_ts(0), _ts(10), _ts(20)]
new, dupes = split_new_and_duplicate(keys, existing)
check("re-upload of identical file inserts nothing", new == [])
check("re-upload counts every row as duplicate", dupes == 3)

# 4. Overlapping upload: only the genuinely new rows are inserted.
existing = {_ts(0), _ts(10)}
keys = [_ts(0), _ts(10), _ts(20), _ts(30)]
new, dupes = split_new_and_duplicate(keys, existing)
check("overlapping upload inserts only new rows", new == [_ts(20), _ts(30)])
check("overlapping upload counts the overlap", dupes == 2)

# 5. Invariant: inserted + duplicates always equals the number of input rows.
keys = [_ts(0), _ts(0), _ts(10), _ts(20), _ts(20)]
existing = {_ts(20)}
new, dupes = split_new_and_duplicate(keys, existing)
check("inserted + duplicates == received", len(new) + dupes == len(keys))


if _failures:
    raise SystemExit(f"{_failures} check(s) failed")
print("\nAll SD-import de-duplication checks passed.")
