"""Waits for the game's reply to a line a session sends, and for what a session reads meanwhile.

GameSession.command returns at the first text ending in a prompt character, and replies such as
stat, where and the editors end lines in "]" or ">"; reply_to waits for a named outcome instead,
then reads what is already on its way.
"""

from __future__ import annotations

from collections.abc import Sequence

from rots_harness.session import GameSession

# Seconds reply_to keeps reading after an outcome arrives, for text the game sent with it.
SETTLE_SECONDS = 1.0


def reply_to(
    session: GameSession,
    line: str,
    outcomes: Sequence[str],
    *,
    settle: float = SETTLE_SECONDS,
    timeout: float | None = None,
) -> str:
    """Sends line and returns what came back, up to the first of outcomes and settle seconds after it.

    Unlike GameSession.command, unread text is not discarded first: an outcome already waiting
    matches at once, and the returned text starts with whatever was unread before the line.
    Checks only that an outcome arrives; which one did is left to the caller. Fails with
    SessionTimeout (an AssertionError) when none arrives within timeout, expect's own default
    when timeout is None. settle=0 returns at the outcome without reading on.
    """
    session.send_line(line)
    return settled_expect(session, outcomes, settle=settle, timeout=timeout)


def settled_expect(
    session: GameSession,
    outcomes: Sequence[str],
    *,
    settle: float = SETTLE_SECONDS,
    timeout: float | None = None,
) -> str:
    """Return what session reads up to the first of outcomes and settle seconds after it.

    Sends nothing, and text unread before the call is part of what is returned. Checks only that
    an outcome arrives; which one did is left to the caller. Fails with SessionTimeout (an
    AssertionError) when none arrives within timeout, expect's own default when timeout is None.
    settle=0 returns at the outcome without reading on.
    """
    if timeout is None:
        text = session.expect(outcomes)
    else:
        text = session.expect(outcomes, timeout=timeout)

    if settle > 0:
        text += session.drain(settle)

    return text


def text_seen_before_a_say(actor: GameSession, actor_name: str, observer: GameSession) -> str:
    """Return what observer has read up to the actor's next `say`, without carriage returns.

    The actor, whose name is actor_name, says "done" through command(), and the say reaches
    observer after anything the actor's earlier commands showed the room, so the returned text
    holds all of it. Nothing is asserted; fails with SessionTimeout when observer does not hear the
    say within expect's default timeout.
    """
    actor.command("say done")
    heard = observer.expect([f"{actor_name} says '"])
    return heard.replace("\r", "")
