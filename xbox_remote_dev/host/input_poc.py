"""Transport-independent remote input state proof of concept.

This model has no Xbox injection path. It is used to validate release and
takeover semantics before connecting a verified resident service.
"""
from dataclasses import dataclass, field, replace

BUTTONS = frozenset("A B X Y START BACK LB RB UP DOWN LEFT RIGHT L3 R3 GUIDE".split())
KEYS = frozenset("UP DOWN LEFT RIGHT ENTER ESCAPE SPACE TAB".split())
WATCHDOG_MS = 500


@dataclass(frozen=True)
class State:
    buttons: frozenset[str] = field(default_factory=frozenset)
    keys: frozenset[str] = field(default_factory=frozenset)
    lx: int = 0
    ly: int = 0
    rx: int = 0
    ry: int = 0
    lt: int = 0
    rt: int = 0


NEUTRAL = State()


class InputModel:
    def __init__(self):
        self.state = NEUTRAL
        self.session = None
        self.last_seq = -1
        self.last_seen_ms = None
        self.physical_takeover = False

    def connect(self, session: str, now_ms: int):
        if not session or len(session) > 64:
            raise ValueError("invalid session")
        self.state = NEUTRAL
        self.session = session
        self.last_seq = -1
        self.last_seen_ms = now_ms
        self.physical_takeover = False

    def disconnect(self):
        self.state = NEUTRAL
        self.session = None
        self.last_seen_ms = None

    def takeover(self):
        self.physical_takeover = True
        self.state = NEUTRAL

    def tick(self, now_ms: int) -> bool:
        if self.session is not None and self.last_seen_ms is not None and \
                now_ms - self.last_seen_ms > WATCHDOG_MS:
            self.disconnect()
            return True
        return False

    def apply(self, session: str, seq: int, sent_ms: int, now_ms: int,
              operation: str, **payload):
        if self.tick(now_ms) or self.session != session or self.physical_takeover:
            return False
        if seq <= self.last_seq or sent_ms > now_ms + 100 or now_ms - sent_ms > WATCHDOG_MS:
            return False
        if operation == "heartbeat":
            next_state = self.state
        elif operation == "release_all":
            next_state = NEUTRAL
        elif operation in ("press", "release"):
            button = payload.get("button")
            if button not in BUTTONS:
                raise ValueError("unknown button")
            buttons = set(self.state.buttons)
            (buttons.add if operation == "press" else buttons.discard)(button)
            next_state = replace(self.state, buttons=frozenset(buttons))
        elif operation == "state":
            buttons = frozenset(payload.get("buttons", ()))
            keys = frozenset(payload.get("keys", ()))
            if not buttons <= BUTTONS or not keys <= KEYS:
                raise ValueError("unknown input")
            axes = {k: payload.get(k, 0) for k in ("lx", "ly", "rx", "ry", "lt", "rt")}
            if any(not isinstance(v, int) for v in axes.values()):
                raise ValueError("non-integer axis")
            if any(not -32768 <= axes[k] <= 32767 for k in ("lx", "ly", "rx", "ry")):
                raise ValueError("stick outside range")
            if any(not 0 <= axes[k] <= 255 for k in ("lt", "rt")):
                raise ValueError("trigger outside range")
            next_state = State(buttons, keys, **axes)
        else:
            raise ValueError("unknown operation")
        self.state = next_state
        self.last_seq = seq
        self.last_seen_ms = now_ms
        return True
