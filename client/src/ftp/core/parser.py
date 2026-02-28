"""
Response parser for FTP protocol
Handles parsing of server responses
"""

import re


class FTPResponse:
    """Represents an FTP server response"""
    
    def __init__(self, code, message, raw_lines=None):
        """
        Initialize FTP response
        
        Args:
            code: Response code (e.g., 220, 230)
            message: Response message
            raw_lines: Raw response lines from server
        """
        self.code = code
        self.message = message
        self.raw_lines = raw_lines or []
    
    @property
    def is_preliminary(self):
        """Check if response is preliminary (1xx)"""
        return 100 <= self.code < 200
    
    @property
    def is_success(self):
        """Check if response indicates success (2xx)"""
        return 200 <= self.code < 300
    
    @property
    def is_intermediate(self):
        """Check if response is intermediate (3xx)"""
        return 300 <= self.code < 400
    
    @property
    def is_error(self):
        """Check if response is error (4xx or 5xx)"""
        return self.code >= 400
    
    @property
    def is_transient_error(self):
        """Check if response is transient error (4xx)"""
        return 400 <= self.code < 500
    
    @property
    def is_permanent_error(self):
        """Check if response is permanent error (5xx)"""
        return 500 <= self.code < 600
        
    def __str__(self):
        """String representation"""
        return f"{self.code} {self.message}"

    def __repr__(self):
        """Debug representation"""
        return f"FTPResponse(code={self.code}, message={self.message!r})"


class ResponseParser:
    """Parser for FTP server responses"""
    
    @staticmethod
    def parse(lines):
        """
        Parse FTP response lines
        
        Args:
            lines: List of response lines or single line string
            
        Returns:
            FTPResponse: Parsed response object
        """
        if isinstance(lines, str):
            lines = [lines]
        
        if not lines:
            return FTPResponse(0, "No response", [])
        
        # First line contains the code
        first_line = lines[0]
        
        # Extract response code (first 3 digits)
        code_match = re.match(r'^(\d{3})', first_line)
        if not code_match:
            return FTPResponse(0, first_line, lines)
        
        code = int(code_match.group(1))
        
        # Handle multiline responses
        if len(lines) == 1:
            # Single line response
            message = first_line[4:] if len(first_line) > 4 else ""
        else:
            # Multiline response - combine all lines
            message_parts = []
            for line in lines:
                # Skip the code prefix
                if line.startswith(str(code)):
                    message_parts.append(line[4:])
                else:
                    message_parts.append(line)
            message = '\n'.join(message_parts)
        
        return FTPResponse(code, message.strip(), lines)
    
    @staticmethod
    def parse_pasv_response(response):
        """
        Parse PASV response to extract host and port
        
        Args:
            response: FTPResponse object from PASV command
            
        Returns:
            tuple: (host, port)
            
        Example:
            "227 Entering Passive Mode (192,168,1,1,234,56)"
            Returns: ("192.168.1.1", 60024)  # 234*256 + 56
        """
        # Find pattern (h1,h2,h3,h4,p1,p2)
        pattern = r'\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)'
        match = re.search(pattern, response.message)
        
        if not match:
            raise ValueError(f"Invalid PASV response: {response.message}")
        
        h1, h2, h3, h4, p1, p2 = map(int, match.groups())
        
        host = f"{h1}.{h2}.{h3}.{h4}"
        port = p1 * 256 + p2
        
        return host, port
    
    @staticmethod
    def format_port_command(host, port):
        """
        Format PORT command argument
        
        Args:
            host: IP address string (e.g., "192.168.1.1")
            port: Port number
            
        Returns:
            str: Formatted argument (e.g., "192,168,1,1,234,56")
            
        Example:
            format_port_command("192.168.1.1", 60024)
            Returns: "192,168,1,1,234,56"
        """
        # Split host into octets
        octets = host.split('.')
        if len(octets) != 4:
            raise ValueError(f"Invalid IP address: {host}")
        
        # Calculate port bytes
        p1 = port // 256
        p2 = port % 256
        
        return f"{octets[0]},{octets[1]},{octets[2]},{octets[3]},{p1},{p2}"
    
    @staticmethod
    def parse_size_response(response):
        """
        Parse SIZE response to extract file size
        
        Args:
            response: FTPResponse object from SIZE command
            
        Returns:
            int: File size in bytes
        """
        if not response.is_success:
            return None
        
        try:
            return int(response.message.strip())
        except ValueError:
            return None
    
    @staticmethod
    def parse_pwd_response(response):
        """
        Parse PWD response to extract current directory
        
        Args:
            response: FTPResponse object from PWD command
            
        Returns:
            str: Current directory path
            
        Example:
            '257 "/home/user" is current directory'
            Returns: "/home/user"
        """
        if not response.is_success:
            return None
        
        # Find quoted path
        match = re.search(r'"([^"]+)"', response.message)
        if match:
            return match.group(1)
        
        return None
