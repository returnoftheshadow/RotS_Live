"""Makes SIGTERM and SIGHUP stop a pytest run the way Ctrl-C does, so fixture finalizers still run."""

from __future__ import annotations

import signal
import threading
from typing import Callable, Dict

# The signals that end a run from outside it: an IDE's stop button, `timeout`, a CI cancel (TERM),
# or a closed terminal (HUP, which Windows lacks).
INTERRUPT_SIGNAL_NAMES = ("SIGTERM", "SIGHUP")


def raise_keyboard_interrupt(signal_number: int, _frame: object) -> None:
    """Signal handler that turns the signal into a KeyboardInterrupt naming it."""
    raise KeyboardInterrupt(f"received {signal.Signals(signal_number).name}")


def install_interrupt_handlers() -> Callable[[], None]:
    """Routes each interrupt signal that still has its default action to raise_keyboard_interrupt.

    A signal with any other handler, including SIG_IGN, is left alone. Returns a function that
    puts back the handlers this call replaced. Off the main thread, where Python cannot install
    signal handlers, it changes nothing.
    """
    replaced_handlers: Dict[int, object] = {}
    if threading.current_thread() is not threading.main_thread():
        return lambda: None
    for signal_name in INTERRUPT_SIGNAL_NAMES:
        signal_number = getattr(signal, signal_name, None)
        if signal_number is None or signal.getsignal(signal_number) != signal.SIG_DFL:
            continue
        replaced_handlers[signal_number] = signal.signal(signal_number, raise_keyboard_interrupt)

    def restore() -> None:
        for signal_number, handler in replaced_handlers.items():
            signal.signal(signal_number, handler)

    return restore
