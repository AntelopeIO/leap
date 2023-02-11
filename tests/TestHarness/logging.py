from enum import Enum, auto
from typing import Any

class fc_log_level(Enum): # Convert to StrEnum once Python 3.11 is minimum on supported platforms
    '''Logging level names to match fc's include/fc/log/log_message.hpp'''
    # Numbers are not required on the Python side
    def __str__(self):
        return self.value
    @staticmethod
    def _generate_next_value_(name: str, start: int, count: int, last_values: list[Any]) -> Any:
        return name.lower()
    all = auto()
    debug = auto()
    info = auto()
    warn = auto()
    error = auto()
    off = auto()
