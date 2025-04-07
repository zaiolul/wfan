from dataclasses import dataclass, field
from collections import deque
from threading import Timer
import enum

@dataclass 
class WifiAp:
    ssid : str
    bssid : str
    channel : int

@dataclass
class RadioInfo:
    freq: int
    signal: int
    noise: int

@dataclass
class ScannerStats:
    average : int = 0
    signal_buf : deque[int] = field(default_factory=deque)
    variance_buf : deque[int] = field(default_factory=deque)
    done : int = 0

@dataclass
class ScannerClient:
    id : str
    ap_list : list[WifiAp] = field(default_factory=list)
    finished_scan : bool = False
    ready : bool = False
    stats : ScannerStats = field(default_factory=ScannerStats)
    crash_timer : Timer = None

class State(enum.Enum):
    SCANNING = 0
    SELECTING = 1
    IDLE = 2

@dataclass
class Manager:
    clients : dict[str, ScannerClient] = field(default_factory=dict)
    common_aps : list[WifiAp] = field(default_factory=list)
    selected_ap : WifiAp = None
    state: State = State.IDLE

class PayloadType(enum.Enum):
    AP_LIST = 0
    PKT_LIST = 1