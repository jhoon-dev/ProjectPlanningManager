"""Independent integer, civil-minute and report checks for resource planning evidence."""
import argparse
from copy import deepcopy
from datetime import datetime, timedelta
from fractions import Fraction
import json
from pathlib import Path


def verify(root):
    arithmetic=json.loads((root/'productivity-arithmetic.json').read_text(encoding='utf-8'))
    for row in arithmetic:
        exact=Fraction(row['quantity']*6000,row['rate'])
        expected=-(-exact.numerator//exact.denominator)
        if expected>500000000000: assert row.get('rejected') is True
        else: assert row['work']==expected
    cases=json.loads((root/'curve-cases.json').read_text(encoding='utf-8'))
    minutes=0
    for case in cases:
        p=case['input']; task=p['tasks'][0]
        calendars={v['id']:v for v in p['calendars']}
        rc=calendars[p['resources'][0]['calendarId']]; tc=calendars[task['calendarId']]
        anchor=datetime.fromisoformat(p['startDate']); groups=[[] for _ in range(20)]
        relative=0; at=0
        def working(c,t):
            minute=t.hour*60+t.minute
            return t.isoweekday() in c['weekdays'] and t.date().isoformat() not in c['holidays'] and any(v['begin']<=minute<v['end'] for v in c['periods'])
        while relative<task['durationMinutes']:
            stamp=anchor+timedelta(minutes=at)
            if working(tc,stamp) and not any(datetime.fromisoformat(b['start'])<=stamp<datetime.fromisoformat(b['finish']) for b in task['breaks']):
                if working(rc,stamp):groups[min(19,relative*20//task['durationMinutes'])].append(at)
                relative+=1
            at+=1
        assert len(case['weights'])==21 and sum(case['weights'])==100000000
        amounts=[case['target']*w//100000000 for w in case['weights'][1:]]
        residual=sorted(range(20),key=lambda i:-(case['target']*case['weights'][i+1]%100000000))
        for i in residual[:case['target']-sum(amounts)]:amounts[i]+=1
        expected={}
        for available,work in zip(groups,amounts):
            assert available or work==0
            if available:
                rate,extra=divmod(work,len(available))
                for j,t in enumerate(available):
                    if rate+(j<extra):expected[t]=rate+(j<extra)
        actual={}
        for span in case['spans']:
            for t in range(span['start'],span['finish']):
                assert t not in actual
                actual[t]=span['units']
        assert actual==expected and sum(actual.values())==case['target']
        left=deepcopy(p);right=deepcopy(case['output'])
        for obj in (left,right):
            obj['assignments'][0].pop('nativeCurve',None)
            obj['assignments'][0].pop('periods',None)
        assert left==right
        minutes+=len(actual)
    before=json.loads((root/'before.json').read_text(encoding='utf-8'))
    after=json.loads((root/'estimated.json').read_text(encoding='utf-8'))
    assert after['revision']==before['revision']+1
    estimate=after['assignments'][0]['productivityEstimate']
    assert estimate['quantityMilli']==120000 and estimate['workUnitMinutes']==24000
    for obj in (before,after):
        obj.pop('revision')
        obj['assignments'][0].pop('productivityEstimate',None)
        obj['assignments'][0].pop('periods',None)
    assert before==after
    return dict(status='PASS',arithmetic_cases=len(arithmetic),calendar_cases=len(cases),resource_minutes=minutes,unchanged='all fields except reviewed plan periods/metadata and one revision')


def reports(root):
    from openpyxl import load_workbook
    from pypdf import PdfReader
    result={}
    for prefix in ('report','curve-report'):
        book=load_workbook(root/(prefix+'.xlsx'),data_only=True)
        sheets={s.title:list(s.values) for s in book}
        catalog=[rows for title,rows in sheets.items() if '생산성' in title]
        assert catalog,book.sheetnames
        estimate=list(book['물량·생산성 공수 산정'].values)[6]
        assert estimate[2:6]==(120,'m3',30,4)
        cost=list(book['작업별 원가'].values)[6]
        assert cost[2]==3600 and cost[6]==12345
        cells=[v for rows in catalog for row in rows for v in row]
        assert 'm3' in cells and 30 in cells
        pdf=PdfReader(root/(prefix+'.pdf'))
        text=''.join(''.join(page.extract_text().split()) for page in pdf.pages)
        assert '생산성' in text and 'm3' in text and '120' in text
        if prefix=='curve-report':
            assert any('곡선' in title for title in sheets)
            assert 'Bell' in text
        result[prefix]=dict(sheets=len(sheets),pdf_pages=len(pdf.pages))
    return dict(status='PASS',reports=result)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);parser.add_argument('--reports',action='store_true');args=parser.parse_args()
    print(json.dumps(reports(args.root) if args.reports else verify(args.root),ensure_ascii=False,indent=2))
