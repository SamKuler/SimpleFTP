"""
User Interface module
Provides CLI and GUI interfaces for the FTP client
"""

from .cli import CLIInterface
from .gui import GUIInterface

__all__ = ['CLIInterface', 'GUIInterface']