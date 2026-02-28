"""
Command-line interface for FTP client
Supports both interactive mode and command-line arguments
"""

import sys
import os
import readline
import shlex
import time
import threading
from ..core.client import FTPClient


class CLIInterface:
    """Command-line interface handler"""

    def __init__(self, verbose=False):
        """Initialize CLI"""
        self.client = None
        self.connected = False
        self.prompt = "ftp> "
        self._last_progress_len = 0
        self.interactive_mode = False
        self.async_mode = False  # default to synchronous
        self.current_path = '/'  # track working directory
        self.verbose = verbose  # when True, still only print server responses (no extra text)

        # Command mapping (primary names)
        self.commands = {
            'connect': self.cmd_connect,
            'login': self.cmd_login,
            'close': self.cmd_close,
            'quit': self.cmd_quit,
            'exit': self.cmd_quit,
            'get': self.cmd_get,
            'put': self.cmd_put,
            'ls': self.cmd_ls,
            'dir': self.cmd_ls,
            'cd': self.cmd_cd,
            'pwd': self.cmd_pwd,
            'mkdir': self.cmd_mkdir,
            'rmdir': self.cmd_rmdir,
            'delete': self.cmd_delete,
            'rename': self.cmd_rename,
            'size': self.cmd_size,
            'help': self.cmd_help,
            'passive': self.cmd_passive,
            'binary': self.cmd_binary,
            'ascii': self.cmd_ascii,
            'abort': self.cmd_abort,
            'transfers': self.cmd_transfers,
            'cancel': self.cmd_cancel_transfer,
            'wait': self.cmd_wait,
        }

        self.commands.update({
            'async': self.cmd_async_on,
            'sync': self.cmd_async_off,
        })

        self._setup_completion()

    def _setup_completion(self):
        def completer(text, state):
            options = []
            line = readline.get_line_buffer()
            tokens = line.split()
            if len(tokens) <= 1:
                # Get current line
                options = [cmd for cmd in self.commands.keys() if cmd.startswith(text)]
            else:
                command = tokens[0]
                if command in ['get', 'delete', 'size', 'ls', 'dir', 'cd']:
                    # Complete remote names
                    options = self._get_remote_completions(text)
                elif command in ['put']:
                    options = self._get_local_completions(text)
            try:
                return options[state]
            except IndexError:
                return None

        readline.set_completer(completer)
        readline.parse_and_bind("tab: complete")

    def _get_local_completions(self, text):
        try:
            dirname = os.path.dirname(text) or '.'
            basename = os.path.basename(text)
            entries = os.listdir(dirname)
            matches = [e for e in entries if e.startswith(basename)]
            comps = []
            for m in matches:
                full = os.path.join(dirname, m)
                comps.append(m + '/' if os.path.isdir(full) else m)
            return comps
        except Exception:
            return []

    def _get_remote_completions(self, text):
        if not self.connected:
            return []
        try:
            resp = self.client.execute_command('PASV')
            if not resp.is_success:
                return []
            dirname = os.path.dirname(text) or '.'
            basename = os.path.basename(text)
            # Execute NLST and consume all responses
            result = self.client.execute_command('NLST', dirname)
            # Consume generator if needed
            if hasattr(result, '__iter__') and not isinstance(result, (str, bytes)):
                for _ in result:
                    pass  # Just consume the responses
            # Get the listing data
            data = self.client.last_transfer_data
            if not data:
                return []
            names = [f.strip() for f in data.decode('utf-8', errors='ignore').splitlines() if f.strip()]
            return [n for n in names if n.startswith(basename)]
        except Exception:
            return []

    def run_interactive(self):
        if self.verbose:
            print("FTP Client - Interactive Mode")
            print("Type 'help' for available commands\n")
        self.interactive_mode = True
        while True:
            try:
                line = input(self.prompt if self.verbose else '').strip()
                self._last_progress_len = 0
                if not line:
                    continue
                try:
                    tokens = shlex.split(line)
                except ValueError:
                    tokens = line.split()
                if not tokens:
                    continue
                command = tokens[0].lower()
                args = tokens[1:]
                if command in self.commands:
                    self.commands[command](args)
                else:
                    self.cmd_raw([command] + args)
            except KeyboardInterrupt:
                print("\n^C")
                continue
            except EOFError:
                print("\nBye")
                self.cmd_quit([])
                break
            except Exception as e:
                # In strict response-only mode we avoid extra prints; still show errors to stderr
                print(f"Error: {e}", file=sys.stderr)
        self.interactive_mode = False

    # ===== Progress helpers =====
    def _transfer_progress(self, bytes_done, total):
        if not self.verbose:
            return
        # Render single-line progress without breaking prompt
        try:
            if self.interactive_mode and readline.get_line_buffer():
                return
        except Exception:  # noqa ：BLE001
            pass
        if total:
            percent = (bytes_done / total) * 100 if total else 0
            msg = f"Progress: {bytes_done}/{total} bytes ({percent:.1f}%)"
        else:
            msg = f"Progress: {bytes_done} bytes"
        padding = max(0, self._last_progress_len - len(msg))
        sys.stdout.write('\r' + msg + (' ' * padding))
        sys.stdout.flush()
        self._last_progress_len = len(msg)

    def _transfer_callback(self, success, result):
        if not self.verbose:
            return
        # clear progress line
        if self._last_progress_len:
            sys.stdout.write('\r' + (' ' * self._last_progress_len) + '\r')
            sys.stdout.flush()
        print("Transfer completed." if success else f"Transfer failed: {result}")
        self._last_progress_len = 0
        # Redisplay prompt only if called from background thread (true async callback)
        # If called from main thread, it's a synchronous failure and main loop will show prompt
        if self.interactive_mode and self.verbose and threading.current_thread() != threading.main_thread():
            try:
                # Check if user has typed anything
                if not readline.get_line_buffer():
                    sys.stdout.write(self.prompt)
                    sys.stdout.flush()
            except Exception:
                # If readline not available, always show prompt
                sys.stdout.write(self.prompt)
                sys.stdout.flush()

    def _active_transfer_ids(self):
        if not self.client:
            return []
        return [t.id for t in self.client.transfer_manager.get_active_transfers()]

    def _wait_for_all_transfers(self):
        if not self.client:
            return
        while self._active_transfer_ids():
            time.sleep(0.2)

    # ===== Commands =====
    def cmd_connect(self, args):
        if len(args) < 1:
            print("Usage: connect <host> [port]")
            return
        host = args[0]
        port = int(args[1]) if len(args) > 1 else 21
        try:
            self.client = FTPClient()
            response = self.client.connect(host, port)
            print(response)
            if response.is_success or response.code == 220:
                self.connected = True
                self.prompt = f"ftp ({host})> "
        except Exception as e:
            print(f"Connection failed: {e}")
            self.client = None

    def cmd_close(self, args):
        if not self.connected:
            print("Not connected")
            return
        try:
            self.client.close()
        except Exception:
            pass
        self.connected = False
        self.client = None
        self.prompt = "ftp> "
        print("Disconnected")

    def cmd_quit(self, args):
        if self.connected:
            self.cmd_close([])
        sys.exit(0)

    def cmd_login(self, args):
        if not self.connected:
            print("Not connected")
            return
        if len(args) < 1:
            username = input("Username: ")
        else:
            username = args[0]
        if len(args) < 2:
            import getpass
            password = getpass.getpass("Password: ")
        else:
            password = args[1]
        try:
            response = self.client.login(username, password)
            print(response)
        except Exception as e:
            print(f"Login failed: {e}")

    def cmd_get(self, args):
        if not self.connected:
            print("Not connected")
            return
        if len(args) < 1:
            print("Usage: get <remote-file> [local-file]")
            return
        remote_file = args[0]
        local_file = args[1] if len(args) > 1 else os.path.basename(remote_file)
        try:
            # Set up passive mode
            resp = self.client.execute_command('PASV')
            print(resp)
            if not resp.is_success:
                return
            # Execute raw RETR
            self._raw_retr(remote_file, local_file)
        except Exception as e:
            print(f"Download failed: {e}")

    def cmd_put(self, args):
        if not self.connected:
            print("Not connected")
            return
        if len(args) < 1:
            print("Usage: put <local-file> [remote-file]")
            return
        local_file = args[0]
        remote_file = args[1] if len(args) > 1 else os.path.basename(local_file)
        if not os.path.exists(local_file):
            print(f"Local file not found: {local_file}")
            return
        try:
            # Set up passive mode
            resp = self.client.execute_command('PASV')
            print(resp)
            if not resp.is_success:
                return
            # Execute raw STOR
            self._raw_stor(local_file, remote_file)
        except Exception as e:
            print(f"Upload failed: {e}")

    def cmd_ls(self, args):
        if not self.connected:
            print("Not connected")
            return
        path = args[0] if args else ''
        try:
            # Set up passive mode
            resp = self.client.execute_command('PASV')
            print(resp)
            if not resp.is_success:
                return
            # Execute raw LIST
            self._raw_list(path)
        except Exception as e:
            print(f"List failed: {e}")

    def cmd_cd(self, args):
        if not self.connected:
            print("Not connected")
            return
        if len(args) < 1:
            print("Usage: cd <directory>")
            return
        try:
            resp = self.client.execute_command('CWD', args[0])
            print(resp)
        except Exception as e:
            print(f"Failed: {e}")

    def cmd_pwd(self, args):
        if not self.connected:
            print("Not connected")
            return
        try:
            resp = self.client.execute_command('PWD')
            print(resp)
        except Exception as e:
            print(f"Failed: {e}")

    def cmd_mkdir(self, args):
        if not self.connected:
            print("Not connected")
            return
        if len(args) < 1:
            print("Usage: mkdir <directory>")
            return
        try:
            resp = self.client.execute_command('MKD', args[0])
            print(resp)
        except Exception as e:
            print(f"Failed: {e}")

    def cmd_rmdir(self, args):
        if not self.connected:
            print("Not connected")
            return
        if len(args) < 1:
            print("Usage: rmdir <directory>")
            return
        try:
            resp = self.client.execute_command('RMD', args[0])
            print(resp)
        except Exception as e:
            print(f"Failed: {e}")

    def cmd_delete(self, args):
        if not self.connected:
            print("Not connected")
            return
        if len(args) < 1:
            print("Usage: delete <file>")
            return
        try:
            resp = self.client.execute_command('DELE', args[0])
            print(resp)
        except Exception as e:
            print(f"Failed: {e}")

    def cmd_rename(self, args):
        if not self.connected:
            print("Not connected")
            return
        if len(args) < 2:
            print("Usage: rename <old-name> <new-name>")
            return
        try:
            resp = self.client.rename(args[0], args[1])
            print(resp)
        except Exception as e:
            print(f"Failed: {e}")

    def cmd_size(self, args):
        if not self.connected:
            print("Not connected")
            return
        if len(args) < 1:
            print("Usage: size <file>")
            return
        try:
            resp = self.client.execute_command('SIZE', args[0])
            print(resp)
        except Exception as e:
            print(f"Failed: {e}")

    def cmd_passive(self, args):
        if not self.connected:
            return
        try:
            resp = self.client.execute_command('PASV')
            print(resp)
        except Exception as e:
            print(f"Failed: {e}")

    def cmd_binary(self, args):
        if not self.connected:
            print("Not connected")
            return
        try:
            resp = self.client.execute_command('TYPE', 'I')
            print(resp)
        except Exception as e:
            print(f"Failed: {e}")

    def cmd_ascii(self, args):
        if not self.connected:
            print("Not connected")
            return
        try:
            resp = self.client.execute_command('TYPE', 'A')
            print(resp)
        except Exception as e:
            print(f"Failed: {e}")

    def cmd_abort(self, args):
        if not self.connected:
            print("Not connected")
            return
        try:
            for tid in self._active_transfer_ids():
                self.client.transfer_manager.cancel_transfer(tid)
            resp = self.client.abort_transfer()
            print(resp)
        except Exception as e:
            print(f"Failed: {e}")

    def cmd_transfers(self, args):
        if not self.connected:
            print("Not connected")
            return
        for t in self.client.transfer_manager.get_all_transfers():
            print(f"#{t.id} {t.type.value} {t.status.value} {t.bytes_transferred}/{t.total_size or '-'}")

    def cmd_cancel_transfer(self, args):
        if not self.connected:
            print("Not connected")
            return
        if not args:
            print("Usage: cancel <transfer-id>")
            return
        try:
            tid = int(args[0])
            self.client.transfer_manager.cancel_transfer(tid)
        except Exception as e:
            print(f"Failed: {e}")

    def cmd_wait(self, args):
        if not self.connected:
            return
        self._wait_for_all_transfers()

    def cmd_raw(self, args):
        """Send a raw FTP command. Special handling for transfer commands.
        Usage examples:
          RETR remote_name [local_name]
          STOR local_path [remote_name]
          APPE local_path [remote_name]
        Other commands are sent directly.
        """
        if not self.connected:
            print("Not connected")
            return
        if not args:
            print("Usage: <command> [args...]")
            return
        upper = args[0].upper()
        try:
            if upper == 'RETR':
                if len(args) < 2:
                    print("Usage: RETR <remote> [local]")
                    return
                remote = args[1]
                local = args[2] if len(args) > 2 else os.path.basename(remote)
                self._raw_retr(remote, local)
                return
            if upper == 'STOR':
                if len(args) < 2:
                    print("Usage: STOR <local> [remote]")
                    return
                local = args[1]
                remote = args[2] if len(args) > 2 else os.path.basename(local)
                self._raw_stor(local, remote)
                return
            if upper == 'APPE':
                if len(args) < 2:
                    print("Usage: APPE <local> [remote]")
                    return
                local = args[1]
                remote = args[2] if len(args) > 2 else os.path.basename(local)
                self._raw_appe(local, remote)
                return
            if upper == 'LIST':
                path = args[1] if len(args) > 1 else ''
                self._raw_list(path)
                return
            # Generic command path
            resp = self.client.execute_command(upper, *args[1:])
            # Handle potential generator response
            if hasattr(resp, '__iter__') and not isinstance(resp, (str, bytes)):
                for r in resp:
                    print(r)
            else:
                print(resp)
        except ConnectionError as e:
            # Connection broken - update connected state
            self.connected = False
            print(f"Connection lost: {e}")
            print("Use 'open' or 'connect' to reconnect")
        except Exception as e:
            print(f"Raw command failed: {e}")

    def cmd_async_on(self, args):
        self.async_mode = True
        print("Async transfer mode enabled")

    def cmd_async_off(self, args):
        self.async_mode = False
        print("Sync transfer mode enabled")

    def _raw_retr(self, remote_file, local_file):
        """Execute RETR command without auto-setting up data connection"""
        try:
            result = self.client.execute_command('RETR', remote_file, local_path=local_file,
                                                 callback=(self._transfer_callback if (
                                                             self.verbose and self.async_mode) else None),
                                                 progress_callback=(self._transfer_progress if (
                                                             self.verbose and self.async_mode) else None),
                                                 async_mode=self.async_mode)
            # Handle generator result
            if hasattr(result, '__iter__') and not isinstance(result, (str, bytes)):
                # In async mode with callback, just consume generator without printing
                # (responses will be handled by callback)
                if self.async_mode and self._transfer_callback:
                    for _ in result:
                        pass
                else:
                    # In sync mode, print all responses
                    for resp in result:
                        print(resp)
            else:
                print(result)
        except ConnectionError as e:
            self.connected = False
            print(f"Connection lost: {e}")
            print("Use 'open' or 'connect' to reconnect")
        except Exception as e:
            print(f"RETR failed: {e}")

    def _raw_stor(self, local_file, remote_file):
        """Execute STOR command without auto-setting up data connection"""
        if not os.path.exists(local_file):
            print(f"Local file not found: {local_file}")
            return
        try:
            result = self.client.execute_command('STOR', remote_file, local_path=local_file,
                                                 callback=(self._transfer_callback if (
                                                             self.verbose and self.async_mode) else None),
                                                 progress_callback=(self._transfer_progress if (
                                                             self.verbose and self.async_mode) else None),
                                                 async_mode=self.async_mode)
            # Handle generator result
            if hasattr(result, '__iter__') and not isinstance(result, (str, bytes)):
                # In async mode with callback, just consume generator without printing
                # (responses will be handled by callback)
                if self.async_mode and self._transfer_callback:
                    for _ in result:
                        pass
                else:
                    # In sync mode, print all responses
                    for resp in result:
                        print(resp)
            else:
                print(result)
        except ConnectionError as e:
            self.connected = False
            print(f"Connection lost: {e}")
            print("Use 'open' or 'connect' to reconnect")
        except Exception as e:
            print(f"STOR failed: {e}")

    def _raw_appe(self, local_file, remote_file):
        """Execute APPE command without auto-setting up data connection"""
        if not os.path.exists(local_file):
            print(f"Local file not found: {local_file}")
            return
        try:
            result = self.client.execute_command('APPE', remote_file, local_path=local_file,
                                                 callback=(self._transfer_callback if (
                                                             self.verbose and self.async_mode) else None),
                                                 progress_callback=(self._transfer_progress if (
                                                             self.verbose and self.async_mode) else None),
                                                 async_mode=self.async_mode)
            # Handle generator result
            if hasattr(result, '__iter__') and not isinstance(result, (str, bytes)):
                # In async mode with callback, just consume generator without printing
                # (responses will be handled by callback)
                if self.async_mode and self._transfer_callback:
                    for _ in result:
                        pass
                else:
                    # In sync mode, print all responses
                    for resp in result:
                        print(resp)
            else:
                print(result)
        except ConnectionError as e:
            self.connected = False
            print(f"Connection lost: {e}")
            print("Use 'open' or 'connect' to reconnect")
        except Exception as e:
            print(f"APPE failed: {e}")

    def _raw_list(self, path=''):
        """Execute LIST command without auto-setting up data connection"""
        try:
            result = self.client.execute_command('LIST', path)
            # Handle generator result
            if hasattr(result, '__iter__') and not isinstance(result, (str, bytes)):
                for resp in result:
                    print(resp)
            else:
                print(result)
            # Show listing payload only when verbose
            if self.verbose and self.client.last_transfer_data:
                try:
                    print(self.client.last_transfer_data.decode('utf-8', errors='ignore'))
                except:
                    pass
        except ConnectionError as e:
            self.connected = False
            print(f"Connection lost: {e}")
            print("Use 'open' or 'connect' to reconnect")
        except Exception as e:
            print(f"LIST failed: {e}")

    def cmd_help(self, args):
        print("""
Available commands:
  connect <host> [port]    - Connect to FTP server
  close                    - Close connection
  login [username] [pass]  - Login to server
  get <remote> [local]     - Download file (respects sync/async)
  put <local> [remote]     - Upload file (respects sync/async)
  ls [path]                - List directory
  cd <directory>           - Change directory
  pwd                      - Print working directory
  mkdir <directory>        - Create directory
  rmdir <directory>        - Remove directory
  delete <file>            - Delete file
  rename <old> <new>       - Rename file
  size <file>              - Get file size
  binary                   - Set binary mode
  ascii                    - Set ASCII mode
  passive                  - Enter passive mode (prints PASV response)
  abort                    - Abort all active transfers
  transfers                - List transfer statuses
  cancel <id>              - Cancel a transfer by id
  wait                     - Block until transfers finish
  async / sync             - Toggle asynchronous transfers
  help                     - Show this help
  quit / exit              - Exit program

Raw FTP commands can also be sent directly (e.g., SYST, NOOP, RETR, STOR, APPE). Sync/async affects RETR/STOR/APPE.
        """)
