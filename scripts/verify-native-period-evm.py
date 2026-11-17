"""Independent native EVM minute/Decimal oracle plus actual XLSX/PDF readback."""
from datetime import datetime, timedelta
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
import hashlib
import json
from itertools import groupby
import sys
import unicodedata

from openpyxl import load_workbook
from pypdf import PdfReader

build = Path(sys.argv[1] if len(sys.argv) > 1 else 'build-governance').resolve()
root = build/'native-period-evm-qa'
envelope = json.loads((root/'source.ppm').read_text(encoding='utf-8'))
p = envelope['payload']
assert envelope['schemaVersion'] == 11
canonical = json.dumps(p, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode()
assert hashlib.sha256(canonical).hexdigest() == envelope['sha256']
assert p == json.loads((root/'source.json').read_text(encoding='utf-8'))
baseline = p['baselines'][0]
plan = baseline['input']
tasks = {t['id']: t for t in plan['tasks']}
costs = {c['taskId']: c for c in plan['taskCosts']}
schedule = {t['taskId']: t for t in baseline['schedule']}
nodes = {n['id']: n for n in plan['wbsNodes']}
status = datetime.strptime(p['statusAt'], '%Y-%m-%d %H:%M')


def stamp(value):
    return datetime.strptime(value[:16], '%Y-%m-%d %H:%M')


def half_up(value):
    return int(Decimal(value).quantize(Decimal('1'), rounding=ROUND_HALF_UP))


def minutes(start, end):
    count = 0
    while start < end:
        if (start.weekday() < 5 and start.strftime('%Y-%m-%d') not in plan['holidays']
                and (9*60 <= start.hour*60+start.minute < 12*60
                     or 13*60 <= start.hour*60+start.minute < 18*60)):
            count += 1
        start += timedelta(minutes=1)
    return count


def pv(task, at, inclusive):
    config = costs[task]
    assert not plan['assignments']  # This fixture independently isolates frozen uniform fixed budgets.
    start, end = stamp(schedule[task]['start']), stamp(schedule[task]['finish'])
    amount = config['fixedMinor']
    assert amount is not None
    if config['accrual'] == 'uniform':
        elapsed = minutes(start, min(end, max(start, at)))
        return half_up(Decimal(amount)*elapsed/minutes(start, end))
    event = start if config['accrual'] == 'start' else end
    return amount if (at >= event if inclusive else at > event) else 0


def point(task, at, inclusive, observed):
    available = [(stamp(e['statusAt']), i, e) for i, e in enumerate(p['costEntries'])
                 if e['taskId'] == task and (stamp(e['statusAt']) <= at if inclusive else stamp(e['statusAt']) < at)]
    report = max(available, default=(None, None, None), key=lambda x: (x[0], x[1]))[2] if observed else None
    bac = costs[task]['fixedMinor']
    physical = None if report is None else report['physicalBasisPoints']
    ev = None if physical is None else half_up(Decimal(bac)*physical/10000)
    return {'bac': bac, 'pv': pv(task, at, inclusive), 'ev': ev,
            'ac': None if report is None else report['actualMinor'],
            'id': '' if report is None else report['id']}


def total(points, key):
    values = [v[key] for v in points]
    return None if None in values else sum(values)


def difference(a, b):
    return None if a is None or b is None else a-b


def ratio(a, b):
    return None if a is None or b is None or a < 0 or b <= 0 else a/b


dated = {}
for e in p['periodActuals']:
    if stamp(e['statusAt']) > status:
        continue
    key = e['taskId'], e['resourceId'], e['date']
    if key not in dated or e['statusAt'] >= dated[key]['statusAt']:
        dated[key] = e


def members(kind, row):
    if kind == 'periodEvm':
        return list(tasks)
    if kind == 'periodEvmTasks':
        return [row[3]]
    found = []
    for task, t in tasks.items():
        node = t['wbsId']
        while node:
            if node == row[3]:
                found.append(task)
                break
            node = nodes[node]['parentId']
    return found


checked = 0
for filename in ['daily.json', 'weekly.json', 'monthly.json']:
    report = json.loads((root/filename).read_text(encoding='utf-8'))
    assert report['policy'] == 'native-period-evm-v1'
    for table in report['tables'][:3]:
        for row in table['rows']:
            assert len(row) == len(table['columns']) == 31
            start, close = stamp(row[0]+' 00:00'), stamp(row[2])
            observed = start <= status
            opening = [point(t, start, False, observed) for t in members(table['id'], row)]
            closing = [point(t, close, row[2].endswith('이하'), observed) for t in members(table['id'], row)]
            bac = total(closing, 'bac')
            apv, bpv = total(opening, 'pv'), total(closing, 'pv')
            aev, bev = total(opening, 'ev'), total(closing, 'ev')
            aac, bac_actual = total(opening, 'ac'), total(closing, 'ac')
            dpv, dev, dac = difference(bpv, apv), difference(bev, aev), difference(bac_actual, aac)
            allocated = sum(e['actualMinor'] or 0 for e in dated.values()
                            if e['taskId'] in members(table['id'], row) and row[0] <= e['date'] <= row[1]) if observed else None
            eac = None if bev is None or bac_actual is None else bac_actual+bac-bev
            expected = {5: bac, 6: apv, 7: bpv, 8: dpv, 9: aev, 10: bev, 11: dev,
                        12: aac, 13: bac_actual, 14: dac, 15: difference(dev, dac), 16: difference(dev, dpv),
                        17: ratio(dev, dac), 18: ratio(dev, dpv), 19: eac,
                        20: difference(bac, bev), 21: allocated, 22: difference(dac, allocated)}
            for col, value in expected.items():
                actual = row[col]
                assert actual == value or (isinstance(value, float) and abs(actual-value) < 1e-12), (filename, row[:5], col, actual, value)
                checked += 1
            if table['id'] == 'periodEvmTasks':
                assert row[27:31] == [opening[0]['id'], closing[0]['id'], opening[0]['id'], closing[0]['id']]

daily = json.loads((root/'daily.json').read_text(encoding='utf-8'))['tables'][0]['rows']
assert [r[11] for r in daily] == [1900, 800, -50, 0, None]
assert [r[14] for r in daily] == [1800, 700, -50, 0, None]
assert [r[21] for r in daily] == [1400, 600, 50, 0, None]

workbooks = [root/'report.xlsx', build/'native-period-evm-ui-qa/ui-report.xlsx']
for path in workbooks:
    book = load_workbook(path, data_only=False)
    sheet = book['native 기간 EVM']
    rows = list(sheet.iter_rows(values_only=True))
    header = next(i for i, r in enumerate(rows) if r[0] == '시작일')
    values = rows[header+1:header+1+len(daily)]
    assert len(values) == len(daily)
    for actual, expected in zip(values, daily):
        assert list(actual[:2]) == expected[:2]
        for col in range(5, 23):
            target = expected[col]
            if target is None:
                target = '미정'
            if target is not None and col not in (17, 18):
                if isinstance(target, (int, float)):
                    target /= 100  # USD minor units -> numeric display units.
            assert actual[col] == target or (isinstance(target, (float, int)) and isinstance(actual[col], (float, int))
                                            and abs(actual[col]-target) < 1e-12), (path.name, col, actual[col], target)
    assert not any(c.data_type == 'f' for sh in book for row in sh for c in row)

pdf_pages = {}
for path in [root/'report.pdf', build/'native-period-evm-ui-qa/ui-report.pdf']:
    pdf = PdfReader(path, strict=True)
    text = unicodedata.normalize('NFKC', '\n'.join(page.extract_text() or '' for page in pdf.pages))
    # Qt synthetic bold can paint the same text twice; collapse adjacent identical extraction runs.
    compact = ''.join(word for word, _ in groupby(text.split()))
    for required in ['native기간EVM', '기간PV', '기간EV', '기간AC', '기간CPI', '미정']:
        assert required in compact, (path.name, required)
    assert not any(ord(c) < 32 and c not in '\n\r\t' for c in text), path.name
    assert len(pdf.pages) > 0
    pdf_pages[str(path.relative_to(build))] = len(pdf.pages)

print(json.dumps({'status': 'passed', 'policy': 'native-period-evm-v1', 'schema_unchanged': 11,
                  'oracle_numeric_cells': checked, 'oracle': ['civil minute/Decimal half-up', 'native report ID/status and last correction',
                                                            'period and cumulative scope sums', 'current dated allocations'],
                  'xlsx_files': len(workbooks), 'xlsx_formulas': 0, 'pdf_pages': pdf_pages,
                  'external_evm_included': False, 'external_product_execution': 'not run'}, ensure_ascii=False, indent=2))
