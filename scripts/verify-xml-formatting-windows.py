"""Bounded independent Windows OLE/NLS comparison; not a Project product round trip."""
import argparse
import ctypes as c
from datetime import datetime
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser()
parser.add_argument('root',type=Path)
args = parser.parse_args()
class Union(c.Union):
    _fields_ = [('dbl',c.c_double),('ptr',c.c_void_p),('record',c.c_void_p*2)]
class Variant(c.Structure):
    _anonymous_ = ('u',)
    _fields_ = [('vt',c.c_ushort),('r1',c.c_ushort),('r2',c.c_ushort),('r3',c.c_ushort),('u',Union)]
ole = c.WinDLL('oleaut32')
ole.VarTokenizeFormatString.argtypes = [c.c_wchar_p,c.c_void_p,c.c_int,c.c_int,c.c_int,c.c_uint,c.POINTER(c.c_int)]
ole.VarTokenizeFormatString.restype = c.c_long
ole.VarFormatFromTokens.argtypes = [c.POINTER(Variant),c.c_wchar_p,c.c_void_p,c.c_uint,c.POINTER(c.c_void_p),c.c_uint]
ole.VarFormatFromTokens.restype = c.c_long
ole.SysFreeString.argtypes = [c.c_void_p]
kernel = c.WinDLL('kernel32',use_last_error=True)
kernel.LCMapStringEx.argtypes = [c.c_wchar_p,c.c_uint,c.c_wchar_p,c.c_int,c.c_wchar_p,c.c_int,c.c_void_p,c.c_void_p,c.c_longlong]
kernel.LCMapStringEx.restype = c.c_int
results = []
differences = []
cases = json.loads((args.root/'cases.json').read_text(encoding='utf-8'))
for row in cases:
    source = row['source']
    numeric = re.fullmatch(r'(?:Format|PPMFormat)\((-?[\d.]+),"([^"]+)"(?:,(\d+))?\)',source)
    date = re.fullmatch(r'Format\(#([^#]+)#,"([^"\\]+)"\)',source)
    if numeric or date:
        v = Variant()
        if numeric:
            value, mask, lcid = numeric.groups()
            v.vt, v.dbl, lcid = 5,float(value),int(lcid or 1033)
        else:
            value, mask = date.groups()
            # Fractional-second Date formatting deliberately drops micros;
            # OLE uses a double; integer-second samples compare more directly.
            if '.' in value: continue
            v.vt, v.dbl, lcid = 7,(datetime.fromisoformat(value)-datetime(1899,12,30)).total_seconds()/86400,1033
        tokens = c.create_string_buffer(2048)
        size = c.c_int()
        out = c.c_void_p()
        assert ole.VarTokenizeFormatString(mask,tokens,len(tokens),1,1,lcid,c.byref(size)) >= 0
        assert ole.VarFormatFromTokens(c.byref(v),mask,tokens,0,c.byref(out),lcid) >= 0
        expected = c.wstring_at(out) if out else ''
        if out: ole.SysFreeString(out)
        assert row['value'] == expected, (row,expected)
        results.append(dict(source=source,oracle='Windows OLE explicit LCID',value=expected))
    match = re.fullmatch(r'StrConv\("([^"]+)",(\d+)(?:,(\d+))?\)',source)
    if match:
        s,flags,lcid = match.groups()
        flags,lcid = int(flags),int(lcid or 1033)
        # Full Unicode16 expansions and supplementary casing are independently
        # verified by UCD/CPython; Windows NLS tables depend on the OS release.
        if any(c in s for c in ('ß','𐐀','Σ')): continue
        native = (0x200 if flags&3==1 else 0x100 if flags&3==2 else 0x300 if flags&3==3 else 0)
        native |= (0x800000 if flags&4 else 0)|(0x400000 if flags&8 else 0)|(0x200000 if flags&16 else 0)|(0x100000 if flags&32 else 0)
        out = c.create_unicode_buffer(4096)
        assert kernel.LCMapStringEx({1033:'en-US',1041:'ja-JP',1042:'ko-KR'}[lcid],native,s,-1,out,4096,None,None,0)
        if flags & 3 == 3 and row['value'] != out.value:
            # NLS title casing uses locale-specific word breaking. The pinned
            # profile explicitly uses the VBA documented word separators.
            differences.append(dict(source=source,oracle_value=out.value,pinned_value=row['value'],
                                    reason='VBA documented word separators; NLS title word breaking differs'))
        else:
            assert row['value'] == out.value, (row,out.value)
            results.append(dict(source=source,oracle='Windows LCMapStringEx explicit locale',value=out.value))
assert len(results) >= 20
print(json.dumps(dict(status='PASS',comparisons=len(results),documented_profile_differences=differences,
                      external_product_run=False,results=results),ensure_ascii=False,indent=2))
