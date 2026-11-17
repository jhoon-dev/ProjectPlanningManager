"""Reconcile work-driven XML progress and forecast via independent civil-minute enumeration."""
import argparse
from collections import defaultdict
from datetime import datetime, timedelta
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
args = parser.parse_args()
root = args.root
load = lambda n: json.loads((root / n).read_text(encoding='utf-8-sig'))
before, merged, corrected = [load(n + '.json') for n in ('before', 'merged', 'corrected')]
raw = (root / 'original.xml').read_bytes()
digest = hashlib.sha256(raw).hexdigest()
ns = {'p': 'http://schemas.microsoft.com/project'}
text = lambda n, k: n.findtext('p:' + k, namespaces=ns)
tasks = {text(t, 'UID'): t for t in ET.fromstring(raw).findall('p:Tasks/p:Task', ns)}
assert len(before['progress']) == 2 and len(merged['progress']) == 6 and len(corrected['progress']) == 7
assert merged['progress'][:2] == before['progress'] and corrected['progress'][:6] == merged['progress']
assert merged['revision'] == before['revision'] + 1 == 6
assert {k: v for k, v in before.items() if k not in ('progress', 'revision')} == {
    k: v for k, v in merged.items() if k not in ('progress', 'revision')}
assert merged['periodActuals'] == before['periodActuals'] and not merged.get('costEntries')
assert merged['periodActuals'][0]['workUnitMinutes'] == 123

def quantity(value):
    if value in (None, '', 'NA'):
        return None
    match = re.fullmatch(r'PT(?:(\d+(?:\.\d+)?)H)?(?:(\d+(?:\.\d+)?)M)?(?:(\d+(?:\.\d+)?)S)?', value)
    assert match, value
    h, m, s = [Decimal(v or '0') for v in match.groups()]
    units = h * 6000 + m * 100 + s * Decimal(5) / Decimal(3)
    assert units == units.to_integral_value(), value
    return int(units)

def stamp(value):
    return datetime.fromisoformat(value).strftime('%Y-%m-%d %H:%M') if value not in (None, '', 'NA') else None

uid_map = {'1': 'NA', '2': 'NB', '3': 'NC', '4': 'NZ'}
for e in merged['progress'][2:]:
    marker = re.match(r'\[XML-PROGRESS sha256=([0-9a-f]{64}) uid=(\d+)\] ', e['reason'])
    assert marker and marker[1] == digest
    uid = marker[2]
    assert e['taskId'] == uid_map[uid] and e['statusAt'] == merged['statusAt']
    assert e['actualStart'] == stamp(text(tasks[uid], 'ActualStart'))
    assert e['actualFinish'] == stamp(text(tasks[uid], 'ActualFinish'))
    assert e['actualDurationMinutes'] * 100 == quantity(text(tasks[uid], 'ActualDuration'))
    assert e['remainingMinutes'] * 100 == quantity(text(tasks[uid], 'RemainingDuration'))
    if uid != '3':
        assert e['remainingWorkUnitMinutes'] == quantity(text(tasks[uid], 'RemainingWork'))
    else:
        assert e.get('remainingWorkUnitMinutes') is None  # Duration target ignores malformed work.
    assert e['sourceDocumentId'] == merged['documentId'] and e['recordedRevision'] == 6
    assert e['planDurationMinutes'] == {'1': 94, '2': 87, '3': 45, '4': 0}[uid]
previous = {}
for e in corrected['progress']:
    assert e['previousEntryId'] == previous.get(e['taskId'], '')
    previous[e['taskId']] = e['id']
assert merged['progress'][2]['previousEntryId'] == before['progress'][1]['id']
assert before['progress'][1]['statusAt'] > merged['statusAt']
assert merged['tasks'][0]['durationMinutes'] == 999 and merged['tasks'][1]['durationMinutes'] == 333

def opened(calendar, at, project):
    minute = at.hour * 60 + at.minute
    return (at.isoweekday() in calendar['weekdays'] and at.strftime('%Y-%m-%d') not in
            set(calendar['holidays']) | set(project.get('holidays', [])) and
            any(p['begin'] <= minute < p['end'] for p in calendar['periods']))

def oracle(project, task_id, work, start, dated=False):
    task = next(t for t in project['tasks'] if t['id'] == task_id)
    calendars = {c['id']: c for c in project['calendars']}
    resources = {r['id']: r for r in project['resources']}
    assignments = sorted((a for a in project['assignments'] if a['taskId'] == task_id), key=lambda a: a['resourceId'])
    at = datetime.fromisoformat(start)
    cells = []
    task_offset = 0
    for _ in range(1440 * 30):
        if opened(calendars[task['calendarId']], at, project) and not any(
                datetime.fromisoformat(b['start']) <= at < datetime.fromisoformat(b['finish']) for b in task.get('breaks', [])):
            rates = {}
            for a in assignments:
                if opened(calendars[resources[a['resourceId']]['calendarId']], at, project):
                    rate = a['units']
                    for period in a.get('periods', []):
                        if period['fromMinute'] <= task_offset < period['toMinute']:
                            rate = period['units']
                    if rate:
                        rates[a['resourceId']] = rate
            cells.append((at, task_offset, rates))
            task_offset += 1
        at += timedelta(minutes=1)
    assert cells
    mandatory = {}
    excluded = set()
    reserved = 0
    if dated:
        for plan in project['remainingWork']:
            if plan['taskId'] != task_id:
                continue
            for day in plan['days']:
                positions = [at for at, _, _ in cells if at.strftime('%Y-%m-%d') == day['date'] and
                             opened(calendars[resources[plan['resourceId']]['calendarId']], at, project)]
                reserved += day['workUnitMinutes']
                excluded.add((plan['resourceId'], day['date']))
                rate, rest = divmod(day['workUnitMinutes'], len(positions))
                for i, at in enumerate(positions):
                    if rate + (i < rest):
                        mandatory[at, plan['resourceId']] = rate + (i < rest)
    remaining = work - reserved
    assert remaining >= 0
    desired = dict(mandatory)
    for at, _, rates in cells:
        rates = {rid: v for rid, v in rates.items() if (rid, at.strftime('%Y-%m-%d')) not in excluded}
        total = sum(rates.values())
        if not total or not remaining:
            continue
        if total <= remaining:
            assigned = rates
        else:
            assigned = {rid: rate * remaining // total for rid, rate in rates.items()}
            remainder = remaining - sum(assigned.values())
            for rid in sorted(assigned):
                if remainder:
                    assigned[rid] += 1
                    remainder -= 1
        for rid, amount in assigned.items():
            if amount:
                desired[at, rid] = amount
        remaining -= sum(assigned.values())
    assert remaining == 0
    finish = max(at for at, _ in desired) + timedelta(minutes=1)
    duration = sum(at < finish for at, _, _ in cells)
    return cells[0][0], finish, duration, desired

def verify_forecast(project, projection, dated=False):
    start, finish, duration, expected = oracle(project, 'NA', projection['remainingWork'], project['statusAt'], dated)
    assert stamp(projection['remainingStart']) == start.strftime('%Y-%m-%d %H:%M')
    assert stamp(projection['finish']) == finish.strftime('%Y-%m-%d %H:%M')
    assert projection['duration'] == duration
    actual = {}
    for span in projection['spans']:
        at = datetime.fromisoformat(span['start'])
        while at < datetime.fromisoformat(span['finish']):
            key = at, span['resourceId']
            assert key not in actual
            actual[key] = span['units']
            at += timedelta(minutes=1)
    assert actual == expected
    assert sum(actual.values()) == projection['remainingWork']
    return len(actual)

main = load('forecast.json')
assert main['remainingWork'] == 3501 and main['duration'] == 37 and main['duration'] != merged['progress'][2]['remainingMinutes']
minutes = verify_forecast(merged, main)
for case in load('cases.json'):
    minutes += verify_forecast(merged, case)
dated = load('dated.json')
assert dated['remainingWork'][0]['days'] == [{'date': '2026-11-17', 'workUnitMinutes': 1000}]
assert dated['remainingWork'] and dated['periodActuals'] == merged['periodActuals']
minutes += verify_forecast(dated, load('dated-forecast.json'), True)
for tid, value in [('NA', 94), ('NB', 87)]:
    task = next(t for t in merged['tasks'] if t['id'] == tid)
    assert oracle(merged, tid, task['workUnitMinutes'], merged['startDate'] + ' 09:00')[2] == value
envelope = load('source.ppm')
assert envelope['schemaVersion'] == 24 and envelope['payload'] == merged

import openpyxl
from pypdf import PdfReader
book = openpyxl.load_workbook(root / 'audit.xlsx', data_only=True)
audit = next(s for s in book if s.title.startswith('XML 채택 작업의 진척 원장'))
entries = {e['id']: e for e in merged['progress']}
rows = [r for r in audit.values if r[1] in entries]
assert len(rows) == 6
for row in rows:
    e = entries[row[1]]
    assert (row[5], row[6], row[12], row[13]) == (e['actualDurationMinutes'], e['remainingMinutes'], e['planDurationMinutes'] if e['planDurationMinutes'] is not None else '미정', e.get('remainingWorkUnitMinutes') if e.get('remainingWorkUnitMinutes') is not None else '미정')
pdf = PdfReader(root / 'audit.pdf')
content = ''.join(''.join(p.extract_text() or '' for p in pdf.pages).split())
# Qt's bold PDF text may extract twice per wrapped header fragment.
assert digest in content and re.search(r'잔여(?:잔여)?공수(?:공수)?u(?:u)?', content)
assert re.search(r'보고(?:보고)?당시(?:당시)?계획분(?:계획분)?', content)
print(json.dumps(dict(status='PASS', source_sha256=digest,
                      native_file_sha256=hashlib.sha256((root/'source.ppm').read_bytes()).hexdigest(),
                      schema=24, original_reports=2, merged_reports=6,
                      corrected_reports=7, forecast_cases=10, checked_resource_minutes=minutes,
                      reported_remaining_minutes=123, forecast_remaining_minutes=37, remaining_work_units=3501,
                      actual_work_units_preserved=123, plan_duration_snapshots=[94,87], audit_rows=len(rows),
                      xlsx_sheets=len(book.sheetnames), pdf_pages=len(pdf.pages), external_product_run=False), ensure_ascii=False, indent=2))
