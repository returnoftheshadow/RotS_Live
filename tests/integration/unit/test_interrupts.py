from __future__ import annotations

import signal
import threading
from typing import Iterator, List

import pytest

from rots_harness import interrupts

# SIGTERM everywhere, plus SIGHUP where the platform has it.
HANDLED_SIGNALS: List[signal.Signals] = [signal.SIGTERM] + ([signal.SIGHUP] if hasattr(signal, "SIGHUP") else [])


@pytest.fixture
def default_handlers() -> Iterator[None]:
    """Gives each handled signal its default action for the test, then puts the session's handlers back."""
    saved_handlers = {signal_number: signal.getsignal(signal_number) for signal_number in HANDLED_SIGNALS}
    for signal_number in HANDLED_SIGNALS:
        signal.signal(signal_number, signal.SIG_DFL)
    try:
        yield
    finally:
        for signal_number, handler in saved_handlers.items():
            signal.signal(signal_number, handler)


@pytest.mark.parametrize("signal_number", HANDLED_SIGNALS, ids=lambda signal_number: signal_number.name)
def test_the_installed_handler_raises_keyboard_interrupt(default_handlers: None, signal_number: signal.Signals) -> None:
    restore = interrupts.install_interrupt_handlers()
    try:
        handler = signal.getsignal(signal_number)
        assert callable(handler)
        with pytest.raises(KeyboardInterrupt):
            handler(signal_number, None)
    finally:
        restore()


def test_a_delivered_sigterm_arrives_as_keyboard_interrupt(default_handlers: None) -> None:
    restore = interrupts.install_interrupt_handlers()
    try:
        # Without the handler a raised SIGTERM would end this pytest process.
        assert callable(signal.getsignal(signal.SIGTERM))
        with pytest.raises(KeyboardInterrupt):
            signal.raise_signal(signal.SIGTERM)
    finally:
        restore()


def test_a_handler_someone_else_installed_is_left_alone(default_handlers: None) -> None:
    def existing_handler(_signal_number: int, _frame: object) -> None:
        pass

    signal.signal(signal.SIGTERM, existing_handler)
    restore = interrupts.install_interrupt_handlers()
    try:
        assert signal.getsignal(signal.SIGTERM) is existing_handler
    finally:
        restore()
    assert signal.getsignal(signal.SIGTERM) is existing_handler


def test_an_ignored_signal_stays_ignored(default_handlers: None) -> None:
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    restore = interrupts.install_interrupt_handlers()
    try:
        assert signal.getsignal(signal.SIGTERM) == signal.SIG_IGN
    finally:
        restore()


def test_restore_puts_the_original_handlers_back(default_handlers: None) -> None:
    restore = interrupts.install_interrupt_handlers()
    assert all(callable(signal.getsignal(signal_number)) for signal_number in HANDLED_SIGNALS)
    restore()
    assert all(signal.getsignal(signal_number) == signal.SIG_DFL for signal_number in HANDLED_SIGNALS)


def test_installing_off_the_main_thread_changes_nothing(default_handlers: None) -> None:
    outcomes: List[object] = []

    def install_and_restore() -> None:
        try:
            restore = interrupts.install_interrupt_handlers()
            restore()
            outcomes.append("returned")
        except BaseException as failure:
            outcomes.append(failure)

    worker = threading.Thread(target=install_and_restore)
    worker.start()
    worker.join()
    assert outcomes == ["returned"]
    assert all(signal.getsignal(signal_number) == signal.SIG_DFL for signal_number in HANDLED_SIGNALS)
