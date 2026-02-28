"""
Main FTP Client class
Coordinates FTP operations
"""

from .connection import ControlConnection, DataConnection
from .commands import CommandRegistry
from .parser import ResponseParser
from .transfer import TransferManager


class FTPClient:
    """Main FTP client coordinating operations"""

    def __init__(self, timeout=30, max_concurrent_transfers=None):
        """
        Initialize FTP client
        
        Args:
            timeout: Socket timeout in seconds
            max_concurrent_transfers: Maximum simultaneous transfers (None = unlimited)
        """
        self.control_conn = ControlConnection(timeout=timeout)
        self.data_conn = DataConnection()
        self.command_registry = CommandRegistry(self)
        self.transfer_manager = TransferManager(self, max_concurrent=max_concurrent_transfers)

        self.last_transfer_data = None
        self.is_connected = False
        self.current_directory = '/'

    def connect(self, host, port=21):
        """
        Connect to FTP server
        
        Args:
            host: Server hostname/IP
            port: Server port (default 21)
            
        Returns:
            FTPResponse: Welcome response from server
        """
        self.control_conn.connect(host, port)

        # Read welcome message
        lines = self.control_conn.recv_multiline()
        response = ResponseParser.parse(lines)

        if response.is_success or response.code == 220:
            self.is_connected = True

        return response

    def login(self, username='anonymous', password='anonymous@'):
        """
        Login to FTP server
        
        Args:
            username: Username (default: anonymous)
            password: Password (default: anonymous)
            
        Returns:
            FTPResponse: Login response
        """
        # Send USER
        user_response = self.command_registry.execute('USER', username)

        # If password required, send PASS
        if user_response.code == 331:
            pass_response = self.command_registry.execute('PASS', password)
            return pass_response

        return user_response

    def execute_command(self, command, *args, **kwargs):
        """
        Execute any FTP command
        
        Args:
            command: Command name
            *args: Command arguments
            **kwargs: Additional options
            
        Returns:
            FTPResponse: Server response
        """
        return self.command_registry.execute(command, *args, **kwargs)

    def rename(self, old_name, new_name):
        """
        Rename file/directory (combines RNFR and RNTO)
        
        Args:
            old_name: Current name
            new_name: New name
            
        Returns:
            FTPResponse: Final response
        """
        rnfr_response = self.execute_command('RNFR', old_name)
        if rnfr_response.code == 350:
            return self.execute_command('RNTO', new_name)
        return rnfr_response

    def get_file_size(self, filename):
        """
        Get size of remote file
        
        Args:
            filename: Remote filename
            
        Returns:
            int: File size in bytes, or None if failed
        """
        response = self.execute_command('SIZE', filename)
        return ResponseParser.parse_size_response(response)

    def abort_transfer(self):
        """
        Abort current data transfer

        Returns:
            FTPResponse: Server response
        """
        return self.execute_command('ABOR')

    def close(self):
        """Close FTP connection"""
        if self.is_connected:
            try:
                self.execute_command('QUIT')
            except:
                pass

        self.transfer_manager.stop_all()
        self.control_conn.close()
        self.data_conn.close()
        self.is_connected = False

    def __enter__(self):
        """Context manager entry"""
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit"""
        self.close()
