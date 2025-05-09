from dataclasses import dataclass, field
from collections import deque
from threading import Timer
import enum

@dataclass(eq=True, frozen=True)
class WifiAp:
    ssid : str
    bssid : str
    channel : int

@dataclass
class RadioInfo:
    freq: int
    signal: int
    noise: int

class ScannerState(enum.Enum):
    SCANNER_IDLE = 0
    SCANNER_SCANNING = 1
    SCANNER_CRASHED = 2

@dataclass
class ScannerStats:
    average : int = 0 #whole window
    variance : int = 0 #whole window
    signal_buf : deque[int] = field(default_factory=deque)
    variance_tmp_buf : deque[int] = field(default_factory=deque)
    variance_buf : deque[int] = field(default_factory=deque)
    ts_buf : deque[int] = field(default_factory=deque)
    done : int = 0

@dataclass
class ScannerClient:
    id : str
    ap_list : list[WifiAp] = field(default_factory=list)
    finished_scan : bool = False
    ready : bool = False
    scanning : bool = False
    stats : ScannerStats = field(default_factory=ScannerStats)
    crash_timer : Timer = None
    state : ScannerState = ScannerState.SCANNER_IDLE
    outfile : str = None

class ManagerState(enum.Enum):
    SCANNING = 0
    SELECTING = 1
    IDLE = 2
class PayloadType(enum.Enum):
    AP_LIST = 0
    PKT_LIST = 1

class ManagerEvent(enum.Enum):
    SCAN_START = 0
    AP_SELECT = 1
    CLIENT_REGISTER = 2
    CLIENT_UNREGISTER = 3
    PKT_DATA_RECV = 4