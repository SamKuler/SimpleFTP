"""
Connection module for handling socket communications
Manages control and data connections
"""

import socket
import threading


class BaseConnection:
    """Manages a single socket connection"""

    def __init__(self, host=None, port=None, timeout=30):
        """
        Initialize connection
        
        Args:
            host: Remote host address
            port: Remote port number
            timeout: Socket timeout in seconds
        """
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self.is_connected = False
        self._lock = threading.Lock()

    def connect(self, host=None, port=None):
        """
        Establish connection to a server
        
        Args:
            host: Remote host (overrides init value if provided)
            port: Remote port (overrides init value if provided)
            
        Returns:
            bool: True if connection successful
        """
        if host:
            self.host = host
        if port:
            self.port = port

        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(self.timeout)
            self.sock.connect((self.host, self.port))
            self.is_connected = True
            return True
        except Exception as e:
            self.is_connected = False
            raise ConnectionError(f"Failed to connect: {e}")

    def send(self, data):
        """
        Send data through socket
        
        Args:
            data: String or bytes to send
        """
        if not self.is_connected:
            raise ConnectionError("Not connected")

        if isinstance(data, str):
            data = data.encode('utf-8')

        try:
            with self._lock:
                self.sock.sendall(data)
        except (BrokenPipeError, ConnectionResetError, OSError) as e:
            # Connection broken - update state and re-raise
            self.is_connected = False
            raise ConnectionError(f"Connection lost: {e}")

    def recv(self, buffer_size=8192):
        """
        Receive data from socket
        
        Args:
            buffer_size: Maximum bytes to receive
            
        Returns:
            bytes: Received data
        """
        if not self.is_connected:
            raise ConnectionError("Not connected")

        try:
            data = self.sock.recv(buffer_size)
            if not data:
                # Connection closed by remote
                self.is_connected = False
                raise ConnectionError("Connection closed by remote")
            return data
        except (BrokenPipeError, ConnectionResetError, OSError) as e:
            # Connection broken - update state and re-raise
            self.is_connected = False
            raise ConnectionError(f"Connection lost: {e}")

    def close(self):
        """Close the connection"""
        if self.sock:
            try:
                self.sock.close()
            except:
                pass
            self.is_connected = False
            self.sock = None


class ControlConnection(BaseConnection):
    """Manages the FTP control connection"""

    def __init__(self, host=None, port=21, timeout=30):
        """
        Initialize control connection
        
        Args:
            host: FTP server host
            port: FTP server port (default 21)
            timeout: Connection timeout in seconds
        """
        super().__init__(host, port, timeout)
        self._recv_lock = threading.RLock()

    def recv_line(self):
        """
        Receive a line of text (until CRLF)
        
        Returns:
            str: Received line without CRLF
        """
        with self._recv_lock:
            line = b''
            while not line.endswith(b'\r\n'):
                chunk = self.recv(1)
                if not chunk:
                    break
                line += chunk
            return line.decode('utf-8').rstrip('\r\n')

    def recv_multiline(self):
        """
        Receive multiline response (RFC 959 section 4.2)
        
        Returns:
            list: List of response lines
        """
        with self._recv_lock:
            lines = []
            first_line = self.recv_line()
            lines.append(first_line)

            # Check if multiline response '-' follows the code
            if len(first_line) >= 4 and first_line[3] == '-':
                code = first_line[:3]
                while True:
                    line = self.recv_line()
                    lines.append(line)
                    # End of multiline when we see "code<space>"
                    if line.startswith(code + ' '):
                        break

            return lines


class DataConnection:
    """Manages FTP data connection for file transfers"""

    def __init__(self):
        """Initialize data connection handler"""
        self.mode = 'passive'  # 'passive' or 'active'
        self.connection = None
        self.server_socket = None  # Only for active mode
        self._lock = threading.Lock()

    def setup_passive(self, host, port):
        """
        Setup passive mode data connection (PASV)
        
        Args:
            host: Data connection host
            port: Data connection port
        """
        with self._lock:
            self.mode = 'passive'
            self.connection = BaseConnection(host, port)

    def setup_active(self, listen_host='0.0.0.0', listen_port=0):
        """
        Setup active mode data connection (PORT)
        
        Args:
            listen_host: Local address to listen on
            listen_port: Local port (0 for random)
            
        Returns:
            tuple: (host, port) for PORT command
        """
        with self._lock:
            self.mode = 'active'
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.bind((listen_host, listen_port))
            self.server_socket.listen(1)

            # Get actual bound address
            host, port = self.server_socket.getsockname()
            return host, port

    def connect(self):
        """
        Establish data connection
        
        Returns:
            bool: True if successful
        """
        if self.mode == 'passive':
            return self.connection.connect()
        else:
            # Accept incoming connection in active mode
            self.connection = BaseConnection()
            client_sock, addr = self.server_socket.accept()
            self.connection.sock = client_sock
            self.connection.is_connected = True
            return True

    def send_data(self, data):
        """Send data through data connection"""
        if isinstance(data, str):
            data = data.encode('utf-8')
        self.connection.send(data)

    def recv_data(self, buffer_size=8192):
        """Receive data through data connection"""
        return self.connection.recv(buffer_size)

    def recv_all(self):
        """
        Receive all data until connection closes
        
        Returns:
            bytes: All received data
        """
        data = b''
        while True:
            try:
                chunk = self.recv_data()
                if not chunk:
                    break
                data += chunk
            except:
                break
        return data

    def close(self):
        """Close data connection"""
        with self._lock:
            if self.connection:
                self.connection.close()
            if self.server_socket:
                try:
                    self.server_socket.close()
                except:
                    pass
                self.server_socket = None
