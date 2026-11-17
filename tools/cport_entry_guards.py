"""Explicit game-configured conditional native entry guards.

Entries are function:preprocessor_symbol:unsigned_ready_hook, comma separated.
The original body is left byte-for-byte intact after the conditional prefix;
with the symbol absent its preprocessed function is unchanged.
"""
import re

def apply_entry_guards(source,spec):
    seen=set();count=0
    for entry in filter(None,(x.strip() for x in spec.split(','))):
        fields=entry.split(':')
        if len(fields)!=3 or any(not re.fullmatch(r'[A-Za-z_]\w*',x) for x in fields):
            raise ValueError('entry guard must be function:symbol:ready_hook')
        function,symbol,hook=fields
        if function in seen:raise ValueError('duplicate guarded function')
        seen.add(function)
        pattern=r'(^[A-Za-z_][\w \t*]*\b'+re.escape(function)+r'\([^;{}]*\)\s*\n\{\n)'
        matches=list(re.finditer(pattern,source,re.M))
        if len(matches)!=1:raise ValueError(f'entry guard {function}: require exactly one definition')
        at=matches[0].end()
        guard=(f'#ifdef {symbol}\n  extern unsigned {hook}(void);\n'
               f'  if (!{hook}()) return 0;\n#endif\n')
        source=source[:at]+guard+source[at:];count+=1
    return source,count
