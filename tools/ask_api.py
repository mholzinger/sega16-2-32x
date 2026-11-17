#!/usr/bin/env python3
"""One read-only question to Claude, billed to the API key; the cost goes to the ledger.

    tools/ask_api.sh [-n name] [-b budget_usd] [-m model] "<self-contained question>"

Goes through the Agent SDK, not `claude -p`: measured 2026-10-25, `ANTHROPIC_API_KEY=... claude
-p` reported apiKeySource 'none' and billed the Max plan, while the SDK reports
'ANTHROPIC_API_KEY'. Refuses to log a call that any other credential paid for.
"""
import argparse, asyncio, datetime, json, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from decomp_runner import api_key, RUNS  # noqa: E402

TOOLS = ['Read', 'Grep', 'Glob', 'Bash(git log:*)', 'Bash(git show:*)', 'Bash(git diff:*)',
         'Bash(grep:*)', 'Bash(rg:*)', 'Bash(sed -n:*)', 'Bash(head:*)', 'Bash(tail:*)',
         'Bash(wc:*)', 'Bash(ls:*)',
         'Bash(%s:*)' % os.path.expanduser('~/src/marsdev/mars/m68k-elf/bin/m68k-elf-objdump')]
SYSTEM = ('You are answering one question unattended. No human will reply: never ask for input; '
          'if you cannot answer, say what is missing. Read only; change nothing. Cite file:line '
          'for every claim.')


async def ask(q, model, budget):
    from claude_agent_sdk import query, ClaudeAgentOptions, ResultMessage, SystemMessage
    opts = ClaudeAgentOptions(
        model=model, cwd=ROOT, max_budget_usd=budget, permission_mode='dontAsk',
        allowed_tools=TOOLS, setting_sources=['user', 'project'],
        system_prompt={'type': 'preset', 'preset': 'claude_code', 'append': SYSTEM},
        env={'ANTHROPIC_API_KEY': api_key()})
    src, res = None, None
    async for m in query(prompt=q, options=opts):
        if isinstance(m, SystemMessage) and m.subtype == 'init':
            src = m.data.get('apiKeySource')
        elif isinstance(m, ResultMessage):
            res = m
    return src, res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-n', default='ask')
    ap.add_argument('-b', type=float, default=2.0)
    ap.add_argument('-m', default='claude-sonnet-5-5')
    ap.add_argument('question')
    a = ap.parse_args()
    if not api_key():
        sys.exit('ask_api: no key in $ANTHROPIC_API_KEY or ~/.config/anthropic/api_key')
    src, res = asyncio.run(ask(a.question, a.m, a.b))
    now = datetime.datetime.now()
    os.makedirs(os.path.join(ROOT, 'build/decomp/ask'), exist_ok=True)
    out = os.path.join(ROOT, 'build/decomp/ask', '%s-%s.json' % (now.strftime('%Y%m%d-%H%M%S'), a.n))
    cost = (res.total_cost_usd or 0) if res else 0
    rec = {'when': now.isoformat(timespec='seconds'), 'kind': 'ask', 'name': a.n, 'model': a.m,
           'cost_usd': cost, 'turns': res.num_turns if res else None, 'api_key_source': src,
           'is_error': res.is_error if res else True, 'usage': (res.usage or {}) if res else {},
           'question': a.question, 'result': res.result if res else None}
    json.dump(rec, open(out, 'w'), indent=1, sort_keys=True)
    if src != 'ANTHROPIC_API_KEY':
        # not this budget's spend: keep it out of the ledger the monthly cap reads
        print('ask_api: WARNING: paid by %r, not the API key; not logged to %s' % (src, RUNS), file=sys.stderr)
    else:
        with open(RUNS, 'a') as fh:
            fh.write(json.dumps({k: v for k, v in rec.items() if k not in ('question', 'result')}, sort_keys=True) + '\n')
    rows = [json.loads(l) for l in open(RUNS) if l.strip()] if os.path.exists(RUNS) else []
    day = sum(x.get('cost_usd') or 0 for x in rows if x['when'].startswith(now.strftime('%Y-%m-%d')))
    mon = sum(x.get('cost_usd') or 0 for x in rows if x['when'].startswith(now.strftime('%Y-%m')))
    print(res.result if res and res.result else '(no result: %s)' % (res.subtype if res else 'no response'))
    print('\n[ask_api] %s: $%.2f, %s turns, paid by %s | today $%.2f | month $%.2f | %s'
          % (a.n, cost, rec['turns'], src, day, mon, out), file=sys.stderr)


if __name__ == '__main__':
    main()
