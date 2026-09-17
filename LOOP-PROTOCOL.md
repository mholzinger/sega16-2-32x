# LOOP PROTOCOL — how the two threads exchange

**The problem this solves:** Mike is the only shared memory between the
DECOMPILE and BUILDER threads. Every fact has to survive a human relay to
persist. That is why work gets re-done — the knowledge was never lost,
it only ever lived in one place.

---

## The premise gate — run it BEFORE proposing anything

Every card, every design, every measurement request. Four lines, and it
goes at the TOP of the message, not in the body:

    PREMISE CHECK
      record says      <what STATE.md / LESSONS.md already establish>
      flags on line    <from Makefile LINE_FLAGS -- NOT .build_flags,
                       which is the last build and usually a probe>
      instrument       <which one, and its known failure mode>
      already dead?    <checked MEASURED DEAD in STATE.md: yes/no>

**Two cards in one week were killed by this check after a full costing
exercise had begun.** Both would have been free to kill at the top.

If any line reads "not checked", the message is not ready to send.

## Message format

    TO: <decompile|builder>   NOTE <n>   commit <sha>

    PREMISE CHECK
      (four lines above)

    CLAIM
      One sentence. What you believe and how strongly.

    EVIDENCE
      Numbers with their instrument named. A file:line for every code
      fact. A rom address for every game fact.

    ASK
      Exactly one thing you want back, and whether it is a READ or a
      BUILD.

**READ before BUILD, always.** In the 2026-09-16 arc, five of six
mechanisms were killed by a read the other thread took before compiling.

## Rules for claims

1. **Name the instrument and its known lie.** `STATE.md` has the current
   list. A number without an instrument is not evidence.
2. **Say gameplay or attract.** Every number. No exceptions — this cost
   a week.
3. **State strength honestly.** "Measured", "derived from rom",
   "hypothesis". A hypothesis with six dead ancestors says so.
4. **A counter-example is a question, not a refutation.** Ask which case
   it is before withdrawing.
5. **Withdraw in writing.** Mark the file, do not quietly restate the
   number. Both threads did this in the 2026-09-16 arc and it is why it
   converged.

## Rules for measurements

1. **Scene-anchor.** The rig's flip rate varies 6x between cold runs of
   the same rom. A median over mixed scenes compares workloads, not
   roms.
2. **Cross-check subset counters.** A subset may never exceed its
   parent — **after** you have checked both count the same UNIT.
   `MDA[20]`/`MDA[19]` read as a violation and was a word count over a
   chunk count. The registries at `m_main.c:72` and `m_main.c:718` say
   what each slot counts; read one before quoting a counter.
3. **Look at the picture.** A census answers the question you asked. If
   you have the frame, open it.
4. **Wrong pixels disqualify a CANDIDATE and qualify an ABLATION.**
   A rom that renders wrong is the right instrument for a bound.

## When to involve Mike

**He is the acceptance gate and the direction-setter, not the bus.**

    ALWAYS         a build that changes a pixel he can see
                   a decision about what the project is for
                   spending rig time
    NEVER          instrument repair
                   premise checks
                   one thread correcting the other's arithmetic
                   anything that ends with "no pixel changed"

**Roughly half of the 2026-09-16 exchanges were one thread catching the
other's broken instrument.** None of that needed him.

## Document discipline

    INTENT.md          stable. What we are building and why.
    STATE.md           volatile. PRUNE IT. What is true now.
    LESSONS.md         append when a lesson cost real work.
    LOOP-PROTOCOL.md   this file.
    docs/log/*         history. Not state. Do not read first.
    docs/handoff/*     history. 31 files. Do not read first.

**If a line in STATE.md is stale, FIX THE LINE.** Do not add a newer line
below it. Appending is how 30 log files and 31 handoffs became
unreadable, and it is why the newest entry wins attention over the
better-founded older one.

## Session start, both threads

1. Read `INTENT.md`, `STATE.md`, `LESSONS.md`. In that order. Nothing
   else.
2. Read `.build_flags` AND the Makefile's `LINE_FLAGS`. The first is
   what is built; the second is the line. They are routinely different.
3. State the premise check for whatever you are about to do.

**Do not start by reading the logs.**
