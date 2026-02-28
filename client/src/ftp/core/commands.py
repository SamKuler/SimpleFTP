"""
Command handlers for FTP commands
Extensible command registry pattern
:)
"""

from .parser import ResponseParser
import socket


class CommandHandler:
    """Base class for FTP command handlers"""

    def __init__(self, client):
        """
        Initialize command handler
        
        Args:
            client: FTPClient instance
        """
        self.client = client

    def execute(self, *args, **kwargs):
        """
        Execute the command
        
        Args:
            *args: Command arguments
            **kwargs: Additional options (e.g., callback, progress_callback)
            
        Returns:
            FTPResponse: Server response
        """
        raise NotImplementedError("Subclasses must implement execute()")

    def format_command(self, command, *args):
        """
        Format command string for sending
        
        Args:
            command: Command name
            *args: Command arguments
            
        Returns:
            str: Formatted command with CRLF
        """
        if args:
            return f"{command} {' '.join(str(a) for a in args)}\r\n"
        return f"{command}\r\n"


# ===== Authentication Commands =====

class UserCommand(CommandHandler):
    """USER command handler - specify user for authentication"""

    def execute(self, username):
        """Send USER command"""
        cmd = self.format_command("USER", username)
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


class PassCommand(CommandHandler):
    """PASS command handler - specify password for authentication"""

    def execute(self, password):
        """Send PASS command"""
        cmd = self.format_command("PASS", password)
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


# ===== Data Connection Commands =====

class PasvCommand(CommandHandler):
    """PASV command handler - enter passive mode"""

    def execute(self):
        """Send PASV command and setup data connection"""
        cmd = self.format_command("PASV")
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        response = ResponseParser.parse(lines)

        if response.is_success:
            # Parse and setup passive data connection
            host, port = ResponseParser.parse_pasv_response(response)
            self.client.data_conn.setup_passive(host, port)

        return response


class PortCommand(CommandHandler):
    """PORT command handler - specify data port for active mode"""

    def execute(self, *args):
        """Send PORT command and setup data connection"""
        host, port = self._determine_active_address(args)

        listen_host, listen_port = self.client.data_conn.setup_active(host, port)

        # Format and send PORT command using the actual bound address/port
        port_arg = ResponseParser.format_port_command(listen_host, listen_port)
        cmd = self.format_command("PORT", port_arg)
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)

    def _determine_active_address(self, args):
        """Resolve host/port tuples for active data mode."""
        if not args:
            # Use the same local address as the control connection, let OS pick port
            return self.client.control_conn.sock.getsockname()[0], 0

        tokens = []
        for arg in args:
            tokens.extend(str(part) for part in str(arg).replace(',', ' ').split())

        if len(tokens) == 6:
            numbers = [int(tok) for tok in tokens]
            host = '.'.join(str(n) for n in numbers[:4])
            port = numbers[4] * 256 + numbers[5]
            return host, port

        if len(tokens) == 2:
            host = tokens[0]
            port = int(tokens[1])
            return host, port

        if len(tokens) == 1:
            # Treat single token as hostname/ip, let OS pick ephemeral port
            return tokens[0], 0

        raise ValueError("PORT command expects six integers, a host and port, or just a host")


# ===== File Transfer Commands =====

class RetrCommand(CommandHandler):
    """RETR command handler - retrieve (download) a file"""

    def execute(self, filename, local_path=None, offset=0, callback=None, progress_callback=None, async_mode=True):
        """
        Retrieve file from server
        
        Args:
            filename: Remote filename
            local_path: Local path to save file (if None, stores in client.last_transfer_data)
            offset: Byte offset for resume (REST marker)
            callback: Completion callback(success, data_or_error)
            progress_callback: Progress callback(bytes_transferred, total_bytes)
            async_mode: Whether to force asynchronous transfer
        """
        # If offset specified, send REST command first
        if offset > 0:
            rest_handler = RestCommand(self.client)
            rest_response = rest_handler.execute(offset)
            # REST should return 3xx (intermediate) - "350 Restart position accepted"
            if not (rest_response.is_success or rest_response.is_intermediate):
                if callback:
                    callback(False, rest_response)
                return rest_response

        # Send RETR command
        cmd = self.format_command("RETR", filename)
        self.client.control_conn.send(cmd)

        # Get preliminary response (150 or 125)
        lines = self.client.control_conn.recv_multiline()
        response = ResponseParser.parse(lines)

        if response.is_preliminary or response.is_success:
            if async_mode and (callback or local_path):
                # Delegate to transfer manager for async transfer
                try:
                    if not (self.client.data_conn.connection and self.client.data_conn.connection.is_connected):
                        self.client.data_conn.connect()
                except Exception:
                    pass
                self.client.transfer_manager.start_download(
                    filename=filename,
                    local_path=local_path,
                    offset=offset,
                    callback=callback,
                    progress_callback=progress_callback
                )
                yield response
                return
            else:
                # Synchronous path (force or no local_path/callback)
                # Yield preliminary response first
                yield response

                try:
                    self.client.data_conn.connect()
                except Exception as e:
                    if callback:
                        callback(False, f"Data connection failed: {e}")
                    # Read final response to keep protocol in sync
                    try:
                        lines = self.client.control_conn.recv_multiline()
                        final_response = ResponseParser.parse(lines)
                        yield final_response
                    except:
                        pass
                    return

                data = self.client.data_conn.recv_all()
                self.client.data_conn.close()

                # Get completion response
                lines = self.client.control_conn.recv_multiline()
                final_response = ResponseParser.parse(lines)

                # Only write when local_path specified
                if local_path and data:
                    try:
                        mode = 'ab' if offset > 0 else 'wb'
                        with open(local_path, mode) as f:
                            f.write(data)
                    except Exception as e:
                        if callback:
                            callback(False, str(e))
                        yield final_response
                        return

                # Store received data in client
                self.client.last_transfer_data = data

                if callback:
                    callback(final_response.is_success, data if final_response.is_success else final_response)

                # Yield final response
                yield final_response
                return

        if callback:
            callback(False, response)
        yield response
        return


class StorCommand(CommandHandler):
    """STOR command handler - store (upload) a file"""

    def execute(self, filename, data=None, local_path=None, offset=0, callback=None, progress_callback=None,
                async_mode=True):
        """
        Store file on server
        
        Args:
            filename: Remote filename
            data: File data as bytes (if provided, used directly)
            local_path: Local file path (if data is None)
            offset: Byte offset for resume (REST marker)
            callback: Completion callback(success, response)
            progress_callback: Progress callback(bytes_transferred, total_bytes)
            async_mode: Whether to force asynchronous transfer
        """
        # If offset specified, send REST command first
        if offset > 0:
            rest_handler = RestCommand(self.client)
            rest_response = rest_handler.execute(offset)
            # REST should return 3xx (intermediate) - "350 Restart position accepted"
            if not (rest_response.is_success or rest_response.is_intermediate):
                if callback:
                    callback(False, rest_response)
                return rest_response

        # Send STOR command
        cmd = self.format_command("STOR", filename)
        self.client.control_conn.send(cmd)

        # Get preliminary response
        lines = self.client.control_conn.recv_multiline()
        response = ResponseParser.parse(lines)

        if response.is_preliminary or response.is_success:
            if async_mode and (callback or local_path):
                # Delegate to transfer manager for async transfer
                try:
                    if not (self.client.data_conn.connection and self.client.data_conn.connection.is_connected):
                        self.client.data_conn.connect()
                except Exception:
                    pass
                self.client.transfer_manager.start_upload(
                    filename=filename,
                    data=data,
                    local_path=local_path,
                    offset=offset,
                    callback=callback,
                    progress_callback=progress_callback
                )
                yield response
                return
            else:
                # Synchronous path
                # Yield preliminary response first
                yield response

                try:
                    self.client.data_conn.connect()
                except Exception as e:
                    if callback:
                        callback(False, f"Data connection failed: {e}")
                    # Read final response to keep protocol in sync
                    try:
                        lines = self.client.control_conn.recv_multiline()
                        final_response = ResponseParser.parse(lines)
                        yield final_response
                    except:
                        pass
                    return

                # Prepare bytes to send
                if data is None and local_path:
                    try:
                        with open(local_path, 'rb') as f:
                            if offset > 0:
                                f.seek(offset)
                            data = f.read()
                    except Exception as e:
                        if callback:
                            callback(False, str(e))
                        # still read final response to keep protocol in sync
                        lines = self.client.control_conn.recv_multiline()
                        final_response = ResponseParser.parse(lines)
                        yield final_response
                        return
                if data:
                    self.client.data_conn.send_data(data)
                self.client.data_conn.close()

                # Get completion response
                lines = self.client.control_conn.recv_multiline()
                final_response = ResponseParser.parse(lines)

                if callback:
                    callback(final_response.is_success, final_response)

                # Yield final response
                yield final_response
                return

        if callback:
            callback(False, response)
        yield response
        return


class AppeCommand(CommandHandler):
    """APPE command handler - append to a file"""

    def execute(self, filename, data=None, local_path=None, callback=None, progress_callback=None, async_mode=True):
        """
        Append data to file on server
        
        Args:
            filename: Remote filename
            data: File data as bytes (if provided, used directly)
            local_path: Local file path (if data is None)
            callback: Completion callback(success, response)
            progress_callback: Progress callback(bytes_transferred, total_bytes)
            async_mode: Whether to force asynchronous transfer
        """
        # Send APPE command
        cmd = self.format_command("APPE", filename)
        self.client.control_conn.send(cmd)

        # Get preliminary response
        lines = self.client.control_conn.recv_multiline()
        response = ResponseParser.parse(lines)

        if response.is_preliminary or response.is_success:
            if async_mode and (callback or local_path):
                # Delegate to transfer manager
                try:
                    if not (self.client.data_conn.connection and self.client.data_conn.connection.is_connected):
                        self.client.data_conn.connect()
                except Exception:
                    pass
                self.client.transfer_manager.start_append(
                    filename=filename,
                    data=data,
                    local_path=local_path,
                    callback=callback,
                    progress_callback=progress_callback
                )
                yield response
                return
            else:
                # Synchronous path
                # Yield preliminary response first
                yield response

                try:
                    self.client.data_conn.connect()
                except Exception as e:
                    if callback:
                        callback(False, f"Data connection failed: {e}")
                    # Read final response to keep protocol in sync
                    try:
                        lines = self.client.control_conn.recv_multiline()
                        final_response = ResponseParser.parse(lines)
                        yield final_response
                    except:
                        pass
                    return

                if data is None and local_path:
                    try:
                        with open(local_path, 'rb') as f:
                            data = f.read()
                    except Exception as e:
                        if callback:
                            callback(False, str(e))
                        lines = self.client.control_conn.recv_multiline()
                        final_response = ResponseParser.parse(lines)
                        yield final_response
                        return
                if data:
                    self.client.data_conn.send_data(data)
                self.client.data_conn.close()

                # Get completion response
                lines = self.client.control_conn.recv_multiline()
                final_response = ResponseParser.parse(lines)

                if callback:
                    callback(final_response.is_success, final_response)

                # Yield final response
                yield final_response
                return

        if callback:
            callback(False, response)
        yield response
        return


class RestCommand(CommandHandler):
    """REST command handler - restart transfer from specified point"""

    def execute(self, offset):
        """
        Send REST command to set restart marker

        Args:
            offset: Byte offset to restart from

        Returns:
            FTPResponse: Server response
        """
        cmd = self.format_command("REST", offset)
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


class AborCommand(CommandHandler):
    """ABOR command handler - abort current transfer"""

    def execute(self):
        """
        Send ABOR command to abort current data transfer
        Per RFC 959, ABOR is sent as urgent data using Telnet IP/Synch
        """
        # Send Telnet IP + Synch, then ABOR
        try:
            # Send as urgent data
            self.client.control_conn.sock.sendall(b'\xff\xf4', socket.MSG_OOB)  # Telnet IP
            self.client.control_conn.sock.sendall(b'\xff\xf2')  # Telnet Synch
            cmd = self.format_command("ABOR")
            self.client.control_conn.send(cmd)

            # Get responses - may get 426 (transfer aborted) then 226 (ABOR successful)
            lines = self.client.control_conn.recv_multiline()
            response = ResponseParser.parse(lines)

            # May receive second response
            if not response.is_success:
                try:
                    lines2 = self.client.control_conn.recv_multiline()
                    response2 = ResponseParser.parse(lines2)
                    # Return the final response
                    return response2
                except:
                    return response

            # Return the first successful response
            return response

        except Exception as e:
            # Fallback: send ABOR normally
            cmd = self.format_command("ABOR")
            self.client.control_conn.send(cmd)
            lines = self.client.control_conn.recv_multiline()
            return ResponseParser.parse(lines)


# ===== Directory Listing Commands =====

class ListCommand(CommandHandler):
    """LIST command handler - list directory contents"""

    def execute(self, path='', callback=None):
        """
        List directory contents

        Args:
            path: Path to list (empty for current directory)
            callback: Completion callback(success, data_or_error)
        """
        # Send LIST command
        cmd = self.format_command("LIST", path) if path else self.format_command("LIST")
        self.client.control_conn.send(cmd)

        # Get preliminary response
        lines = self.client.control_conn.recv_multiline()
        response = ResponseParser.parse(lines)

        if response.is_preliminary or response.is_success:
            # Yield preliminary response first
            yield response

            # Connect data channel and receive listing
            self.client.data_conn.connect()
            data = self.client.data_conn.recv_all()
            self.client.data_conn.close()

            # Get completion response
            lines = self.client.control_conn.recv_multiline()
            final_response = ResponseParser.parse(lines)

            # Store listing data
            self.client.last_transfer_data = data

            if callback:
                callback(final_response.is_success, data if final_response.is_success else final_response)

            # Yield final response
            yield final_response
            return

        if callback:
            callback(False, response)
        yield response
        return


class NlstCommand(CommandHandler):
    """NLST command handler - name list of directory"""

    def execute(self, path='', callback=None):
        """
        Get name list of directory
        
        Args:
            path: Path to list (empty for current directory)
            callback: Completion callback(success, data_or_error)
        """
        # Send NLST command
        cmd = self.format_command("NLST", path) if path else self.format_command("NLST")
        self.client.control_conn.send(cmd)

        # Get preliminary response
        lines = self.client.control_conn.recv_multiline()
        response = ResponseParser.parse(lines)

        if response.is_preliminary or response.is_success:
            # Yield preliminary response first
            yield response

            # Connect data channel and receive listing
            self.client.data_conn.connect()
            data = self.client.data_conn.recv_all()
            self.client.data_conn.close()

            # Get completion response
            lines = self.client.control_conn.recv_multiline()
            final_response = ResponseParser.parse(lines)

            # Store listing data
            self.client.last_transfer_data = data

            if callback:
                callback(final_response.is_success, data if final_response.is_success else final_response)

            # Yield final response
            yield final_response
            return

        if callback:
            callback(False, response)
        yield response
        return


# ===== Directory Management Commands =====

class CwdCommand(CommandHandler):
    """CWD command handler - change working directory"""

    def execute(self, directory):
        """Change working directory"""
        cmd = self.format_command("CWD", directory)
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


class CdupCommand(CommandHandler):
    """CDUP command handler - change to parent directory"""

    def execute(self):
        """Change to parent directory"""
        cmd = self.format_command("CDUP")
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


class PwdCommand(CommandHandler):
    """PWD command handler - print working directory"""

    def execute(self):
        """Print working directory"""
        cmd = self.format_command("PWD")
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


class MkdCommand(CommandHandler):
    """MKD command handler - make directory"""

    def execute(self, directory):
        """Make directory"""
        cmd = self.format_command("MKD", directory)
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


class RmdCommand(CommandHandler):
    """RMD command handler - remove directory"""

    def execute(self, directory):
        """Remove directory"""
        cmd = self.format_command("RMD", directory)
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


# ===== File Management Commands =====

class DeleCommand(CommandHandler):
    """DELE command handler - delete file"""

    def execute(self, filename):
        """Delete file"""
        cmd = self.format_command("DELE", filename)
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


class RnfrCommand(CommandHandler):
    """RNFR command handler - rename from (first step of rename)"""

    def execute(self, filename):
        """Specify file to rename"""
        cmd = self.format_command("RNFR", filename)
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


class RntoCommand(CommandHandler):
    """RNTO command handler - rename to (second step of rename)"""

    def execute(self, filename):
        """Specify new filename"""
        cmd = self.format_command("RNTO", filename)
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


# ===== Additional Utility Commands =====

class SizeCommand(CommandHandler):
    """SIZE command handler - get file size"""

    def execute(self, filename):
        """
        Get size of file
        
        Args:
            filename: Remote filename
            
        Returns:
            FTPResponse: Response with file size in message
        """
        cmd = self.format_command("SIZE", filename)
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


class TypeCommand(CommandHandler):
    """TYPE command handler - set transfer type"""

    def execute(self, type_code='I'):
        """
        Set representation type
        
        Args:
            type_code: 'A' for ASCII, 'I' for Image (binary)
        """
        cmd = self.format_command("TYPE", type_code)
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


class SystCommand(CommandHandler):
    """SYST command handler - get system type"""

    def execute(self):
        """Get server system type"""
        cmd = self.format_command("SYST")
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


class QuitCommand(CommandHandler):
    """QUIT command handler - logout and close connection"""

    def execute(self):
        """Send QUIT command"""
        cmd = self.format_command("QUIT")
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


class NoopCommand(CommandHandler):
    """NOOP command handler - no operation (keep-alive)"""

    def execute(self):
        """Send NOOP command"""
        cmd = self.format_command("NOOP")
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


# ===== Generic Command Handler =====

class GenericCommand(CommandHandler):
    """Generic command handler for any FTP command"""

    def execute(self, command, *args):
        """
        Send any FTP command
        
        Args:
            command: Command name
            *args: Command arguments
        """
        cmd = self.format_command(command, *args)
        self.client.control_conn.send(cmd)
        lines = self.client.control_conn.recv_multiline()
        return ResponseParser.parse(lines)


# ===== Command Registry =====

class CommandRegistry:
    """Registry for FTP commands"""

    def __init__(self, client):
        """
        Initialize command registry
        
        Args:
            client: FTPClient instance
        """
        self.client = client
        self.commands = {}
        self._register_default_commands()

    def _register_default_commands(self):
        """Register default FTP commands"""
        # Authentication
        self.register('USER', UserCommand)
        self.register('PASS', PassCommand)

        # Data connection
        self.register('PASV', PasvCommand)
        self.register('PORT', PortCommand)

        # File transfer
        self.register('RETR', RetrCommand)
        self.register('STOR', StorCommand)
        self.register('APPE', AppeCommand)
        self.register('REST', RestCommand)
        self.register('ABOR', AborCommand)

        # Directory listing
        self.register('LIST', ListCommand)
        self.register('NLST', NlstCommand)

        # Directory management
        self.register('CWD', CwdCommand)
        self.register('CDUP', CdupCommand)
        self.register('PWD', PwdCommand)
        self.register('MKD', MkdCommand)
        self.register('RMD', RmdCommand)

        # File management
        self.register('DELE', DeleCommand)
        self.register('RNFR', RnfrCommand)
        self.register('RNTO', RntoCommand)

        # Utility commands
        self.register('SIZE', SizeCommand)
        self.register('TYPE', TypeCommand)
        self.register('SYST', SystCommand)
        self.register('QUIT', QuitCommand)
        self.register('NOOP', NoopCommand)

    def register(self, command_name, handler_class):
        """
        Register a command handler
        
        Args:
            command_name: Command name (uppercase)
            handler_class: CommandHandler subclass
        """
        self.commands[command_name.upper()] = handler_class

    def get_handler(self, command_name):
        """
        Get handler for command
        
        Args:
            command_name: Command name
            
        Returns:
            CommandHandler: Handler instance
        """
        command_name = command_name.upper()
        handler_class = self.commands.get(command_name, GenericCommand)
        return handler_class(self.client)

    def execute(self, command_name, *args, **kwargs):
        """
        Execute a command
        
        Args:
            command_name: Command name
            *args: Command arguments
            **kwargs: Additional options (callback, etc.)
            
        Returns:
            FTPResponse: Server response
        """
        handler = self.get_handler(command_name)

        # For generic commands, pass command name as first argument
        if isinstance(handler, GenericCommand):
            return handler.execute(command_name, *args)

        return handler.execute(*args, **kwargs)
