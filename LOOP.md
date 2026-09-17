# LOOP — the runbook one tick executes

Drive this with `/loop` and the prompt in "INVOCATION" below.

**The loop's job is to move `MESSAGES.md` OPEN items forward without
Mike.** It stops and escalates at three gates and nowhere else.

---

## One tick

    1. READ      INTENT.md, STATE.md, LESSONS.md, .build_flags,
                 MESSAGES.md OPEN.  Nothing else. Not the logs.

    2. PICK      the highest OPEN item that is not blocked and not
                 gated. Prefer READ over BUILD. Prefer an item whose
                 outcome unblocks another.

    3. GATE      does this need Mike?
                   pixel he can see | rig time | direction call
                 -> if yes: write the GATE QUEUE entry, do NOT do it,
                    go to step 6.

    4. PREMISE   write the four-line check (LOOP-PROTOCOL.md).
                 If any line reads "not checked", stop and check it.
                 If the item is already in STATE.md MEASURED DEAD or
                 already in the SHIPPED flag list -> close it in the
                 LEDGER and pick another.

    5. DO        the read or the build. Name the instrument and its
                 known lie. Say gameplay or attract. Scene-anchor any
                 rig number.

    6. WRITE     - update STATE.md IN PLACE (prune, never append)
                 - close the OPEN item to the LEDGER, or update it
                 - append to LESSONS.md only if something COST work
                 - log the exchange in MESSAGES.md

    7. PACE      quiet tick -> noop:true, 1200-1800s
                 something moved -> noop:false
                 waiting on a build/run -> long fallback, 1200s+

## The three gates — escalate, do not proceed

    PIXEL       any build whose output Mike would see differs from the
                line. He is the acceptance gate; metrics rank, he
                decides.
    RIG         any run that occupies the MiSTer. Rig time is scarce
                and a bad rom on it costs a relaunch.
    DIRECTION   any change to what the project is for, the bar, or the
                live axis in STATE.md.

**Everything else the loop does on its own** — instrument repair,
premise checks, counter registries, arithmetic corrections, reads,
ablations that produce a number and no pixel.

## Hard stops — end the loop, do not schedule another tick

    - the GATE QUEUE has an item and nothing else is unblocked
    - a premise check kills the last OPEN item
    - two consecutive ticks produce no state change
    - an instrument is found lying and its fix is itself gated

## Standing rules the loop must not violate

    - rom/s16.32x holds the LINE build at the end of every tick
    - never hand Mike a probe as a playable build
    - never quote an ares number as a speed ranking (slave-gated)
    - never quote an attract number as a gameplay number
    - a subset counter may not exceed its parent; if it does, the
      instrument is broken, not the code
    - wrong pixels disqualify a CANDIDATE and qualify an ABLATION

## INVOCATION

    /loop Work MESSAGES.md OPEN per LOOP.md. Read INTENT.md, STATE.md,
    LESSONS.md and .build_flags first — not the logs. Run the premise
    check before any card. Escalate to the GATE QUEUE only for a pixel
    Mike can see, rig time, or a direction call; do everything else
    yourself. Update STATE.md in place and close items to the LEDGER.

## Why this exists

Mike was the only shared memory between the two threads, so every fact
had to survive a human relay to persist. Roughly half of one week's
exchanges were one thread catching the other's broken instrument — none
of which needed him. `MESSAGES.md` is the shared memory; this file is
what reads it.
