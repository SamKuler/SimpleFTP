"""
Graphical User Interface for FTP client
Uses tkinter for cross-platform GUI
"""

import tkinter as tk
from tkinter import ttk, messagebox, filedialog, scrolledtext
import threading
import os
from ..core.client import FTPClient
import time


class GUIInterface:
    """GUI handler using tkinter"""

    def __init__(self, host=None, port=None, user=None, password=None):
        """Initialize GUI with optional prefilled connection parameters"""
        self.client = None
        self.connected = False
        self.current_remote_path = "/"

        # Create main window
        self.root = tk.Tk()
        self.root.title("FTP Client")
        self.root.geometry("900x600")

        # Store initial params
        self._init_host = host
        self._init_port = port
        self._init_user = user
        self._init_pass = password
        # Transfer control state
        self._last_action_ts = {}
        self._debounce_interval_default = 500  # ms
        self._transfer_lock = threading.Lock()
        self._transfer_active = False

        # Setup UI components
        self._setup_ui()

    def _consume_generator_response(self, result):
        """Helper to consume generator response and return final response"""
        if hasattr(result, '__iter__') and not isinstance(result, (str, bytes)):
            final = None
            for resp in result:
                final = resp
            return final
        return result

    def _setup_ui(self):
        """Setup all UI components"""
        # Connection frame
        self._setup_connection_frame()

        # Main content area with notebook (tabs)
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        # File browser tab
        self._setup_browser_tab()

        # Transfer tab
        self._setup_transfer_tab()

        # Command tab
        self._setup_command_tab()

        # Status bar
        self._setup_status_bar()

    def _setup_connection_frame(self):
        """Setup connection controls"""
        frame = ttk.LabelFrame(self.root, text="Connection", padding=10)
        frame.pack(fill=tk.X, padx=5, pady=5)

        # Host
        ttk.Label(frame, text="Host:").grid(row=0, column=0, sticky=tk.W, padx=5)
        self.host_entry = ttk.Entry(frame, width=20)
        self.host_entry.grid(row=0, column=1, padx=5)
        self.host_entry.insert(0, self._init_host or "localhost")

        # Port
        ttk.Label(frame, text="Port:").grid(row=0, column=2, sticky=tk.W, padx=5)
        self.port_entry = ttk.Entry(frame, width=8)
        self.port_entry.grid(row=0, column=3, padx=5)
        self.port_entry.insert(0, str(self._init_port or 21))

        # Username
        ttk.Label(frame, text="User:").grid(row=0, column=4, sticky=tk.W, padx=5)
        self.user_entry = ttk.Entry(frame, width=15)
        self.user_entry.grid(row=0, column=5, padx=5)
        self.user_entry.insert(0, self._init_user or "anonymous")

        # Password
        ttk.Label(frame, text="Pass:").grid(row=0, column=6, sticky=tk.W, padx=5)
        self.pass_entry = ttk.Entry(frame, width=15, show="*")
        self.pass_entry.grid(row=0, column=7, padx=5)
        self.pass_entry.insert(0, self._init_pass or "anonymous")

        # Connect button
        self.connect_btn = ttk.Button(frame, text="Connect", command=self._on_connect)
        self.connect_btn.grid(row=0, column=8, padx=5)

        # Disconnect button
        self.disconnect_btn = ttk.Button(frame, text="Disconnect",
                                         command=self._on_disconnect, state=tk.DISABLED)
        self.disconnect_btn.grid(row=0, column=9, padx=5)

    def _setup_browser_tab(self):
        """Setup file browser tab"""
        browser_frame = ttk.Frame(self.notebook)
        self.notebook.add(browser_frame, text="File Browser")

        # Create paned window for local and remote
        paned = ttk.PanedWindow(browser_frame, orient=tk.HORIZONTAL)
        paned.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        # Local files panel
        local_frame = ttk.LabelFrame(paned, text="Local Files", padding=5)
        paned.add(local_frame, weight=1)

        # Local path
        local_path_frame = ttk.Frame(local_frame)
        local_path_frame.pack(fill=tk.X, pady=5)

        ttk.Label(local_path_frame, text="Path:").pack(side=tk.LEFT)
        self.local_path_entry = ttk.Entry(local_path_frame)
        self.local_path_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        self.local_path_entry.insert(0, os.getcwd())

        ttk.Button(local_path_frame, text="Go",
                   command=self._refresh_local_list).pack(side=tk.LEFT)

        # Local file list
        local_list_frame = ttk.Frame(local_frame)
        local_list_frame.pack(fill=tk.BOTH, expand=True)

        self.local_listbox = tk.Listbox(local_list_frame, selectmode=tk.EXTENDED)
        local_scrollbar = ttk.Scrollbar(local_list_frame, orient=tk.VERTICAL,
                                        command=self.local_listbox.yview)
        self.local_listbox.config(yscrollcommand=local_scrollbar.set)

        self.local_listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        local_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        # Double-click to navigate
        self.local_listbox.bind('<Double-Button-1>', self._on_local_double_click)

        # Local buttons
        local_btn_frame = ttk.Frame(local_frame)
        local_btn_frame.pack(fill=tk.X, pady=5)

        ttk.Button(local_btn_frame, text="Upload",
                   command=self._on_upload).pack(side=tk.LEFT, padx=2)
        ttk.Button(local_btn_frame, text="Refresh",
                   command=self._refresh_local_list).pack(side=tk.LEFT, padx=2)

        # Remote files panel
        remote_frame = ttk.LabelFrame(paned, text="Remote Files", padding=5)
        paned.add(remote_frame, weight=1)

        # Remote path
        remote_path_frame = ttk.Frame(remote_frame)
        remote_path_frame.pack(fill=tk.X, pady=5)

        ttk.Label(remote_path_frame, text="Path:").pack(side=tk.LEFT)
        self.remote_path_entry = ttk.Entry(remote_path_frame)
        self.remote_path_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        self.remote_path_entry.insert(0, "/")

        ttk.Button(remote_path_frame, text="Go",
                   command=self._refresh_remote_list).pack(side=tk.LEFT)

        # Remote file list
        remote_list_frame = ttk.Frame(remote_frame)
        remote_list_frame.pack(fill=tk.BOTH, expand=True)

        self.remote_listbox = tk.Listbox(remote_list_frame, selectmode=tk.EXTENDED)
        remote_scrollbar = ttk.Scrollbar(remote_list_frame, orient=tk.VERTICAL,
                                         command=self.remote_listbox.yview)
        self.remote_listbox.config(yscrollcommand=remote_scrollbar.set)

        self.remote_listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        remote_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        # Double-click to navigate
        self.remote_listbox.bind('<Double-Button-1>', self._on_remote_double_click)

        # Remote buttons
        remote_btn_frame = ttk.Frame(remote_frame)
        remote_btn_frame.pack(fill=tk.X, pady=5)

        ttk.Button(remote_btn_frame, text="Download",
                   command=self._on_download).pack(side=tk.LEFT, padx=2)
        ttk.Button(remote_btn_frame, text="Append",
                   command=self._on_append).pack(side=tk.LEFT, padx=2)
        ttk.Button(remote_btn_frame, text="Resume D",
                   command=self._on_resume_download).pack(side=tk.LEFT, padx=2)
        ttk.Button(remote_btn_frame, text="Resume U",
                   command=self._on_resume_upload).pack(side=tk.LEFT, padx=2)
        ttk.Button(remote_btn_frame, text="Rename",
                   command=self._on_rename).pack(side=tk.LEFT, padx=2)
        ttk.Button(remote_btn_frame, text="Delete",
                   command=self._on_delete).pack(side=tk.LEFT, padx=2)
        ttk.Button(remote_btn_frame, text="Refresh",
                   command=self._refresh_remote_list).pack(side=tk.LEFT, padx=2)

        # Initialize local list
        self._refresh_local_list()

    def _setup_transfer_tab(self):
        """Setup transfer progress tab"""
        transfer_frame = ttk.Frame(self.notebook)
        self.notebook.add(transfer_frame, text="Transfers")

        # Transfer list
        list_frame = ttk.Frame(transfer_frame)
        list_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        columns = ('File', 'Type', 'Status', 'Progress', 'Speed')
        self.transfer_tree = ttk.Treeview(list_frame, columns=columns, show='headings')

        for col in columns:
            self.transfer_tree.heading(col, text=col)
            self.transfer_tree.column(col, width=120 if col == 'Speed' else 150)

        scrollbar = ttk.Scrollbar(list_frame, orient=tk.VERTICAL,
                                  command=self.transfer_tree.yview)
        self.transfer_tree.config(yscrollcommand=scrollbar.set)

        self.transfer_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        # Progress bar + options
        progress_frame = ttk.Frame(transfer_frame)
        progress_frame.pack(fill=tk.X, padx=5, pady=5)

        ttk.Label(progress_frame, text="Current Transfer:").pack(side=tk.LEFT)
        self.progress_var = tk.DoubleVar()
        self.progress_bar = ttk.Progressbar(progress_frame, variable=self.progress_var,
                                            maximum=100)
        self.progress_bar.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)

        # Buttons
        btn_frame = ttk.Frame(transfer_frame)
        btn_frame.pack(fill=tk.X, padx=5, pady=5)

        ttk.Button(btn_frame, text="Abort Transfer",
                   command=self._on_abort).pack(side=tk.LEFT, padx=2)
        ttk.Button(btn_frame, text="Clear Completed",
                   command=self._on_clear_transfers).pack(side=tk.LEFT, padx=2)

    def _setup_command_tab(self):
        """Setup raw command tab"""
        cmd_frame = ttk.Frame(self.notebook)
        self.notebook.add(cmd_frame, text="Commands")

        # Output area
        output_frame = ttk.LabelFrame(cmd_frame, text="Output", padding=5)
        output_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        self.output_text = scrolledtext.ScrolledText(output_frame, height=20,
                                                     state=tk.DISABLED)
        self.output_text.pack(fill=tk.BOTH, expand=True)

        # Command input
        input_frame = ttk.Frame(cmd_frame)
        input_frame.pack(fill=tk.X, padx=5, pady=5)

        ttk.Label(input_frame, text="Command:").pack(side=tk.LEFT)
        self.cmd_entry = ttk.Entry(input_frame)
        self.cmd_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        self.cmd_entry.bind('<Return>', lambda e: self._on_send_command())

        ttk.Button(input_frame, text="Send",
                   command=self._on_send_command).pack(side=tk.LEFT)

        # Quick commands
        quick_frame = ttk.LabelFrame(cmd_frame, text="Quick Commands", padding=5)
        quick_frame.pack(fill=tk.X, padx=5, pady=5)

        quick_cmds = [
            ("PWD", "PWD"),
            ("SYST", "SYST"),
            ("NOOP", "NOOP"),
            ("TYPE I", "TYPE I"),
            ("TYPE A", "TYPE A"),
        ]

        for i, (label, cmd) in enumerate(quick_cmds):
            ttk.Button(quick_frame, text=label,
                       command=lambda c=cmd: self._send_raw_command(c)).grid(
                row=0, column=i, padx=2, pady=2)

    def _setup_status_bar(self):
        """Setup status bar"""
        self.status_bar = ttk.Label(self.root, text="Not connected",
                                    relief=tk.SUNKEN, anchor=tk.W)
        self.status_bar.pack(fill=tk.X, side=tk.BOTTOM)

    def _on_connect(self):
        """Handle connect button"""
        host = self.host_entry.get()
        try:
            port = int(self.port_entry.get())
        except Exception:
            messagebox.showerror("Invalid Port", "Port must be an integer")
            return
        username = self.user_entry.get()
        password = self.pass_entry.get()

        def connect_thread():
            try:
                self.client = FTPClient()
                response = self.client.connect(host, port)
                self._log_output(f"Connected: {response}")

                response = self.client.login(username, password)
                self._log_output(f"Login: {response}")

                if response.is_success:
                    self.connected = True
                    self.root.after(0, self._on_connect_success)
                else:
                    self.root.after(0, lambda response=response: messagebox.showerror("Login Failed", str(response)))
            except Exception as e:
                self.root.after(0, lambda e=e: messagebox.showerror("Connection Error", str(e)))

        threading.Thread(target=connect_thread, daemon=True).start()

    def _format_bps(self, bps):
        try:
            if not bps or bps <= 0:
                return '-'
            units = ['B/s', 'KB/s', 'MB/s', 'GB/s', 'TB/s']
            i = 0
            while bps >= 1024 and i < len(units) - 1:
                bps /= 1024.0;
                i += 1
            return f"{bps:.1f} {units[i]}"
        except Exception:
            return '-'

    def _debounce(self, name, interval_ms=None):
        """Return True if action allowed, False if suppressed by debounce."""
        now = int(time.time() * 1000)
        interval = interval_ms or self._debounce_interval_default
        last = self._last_action_ts.get(name, 0)
        if now - last < interval:
            return False
        self._last_action_ts[name] = now
        return True

    def _begin_transfer(self):
        """Attempt to mark transfer active; returns True if allowed, False otherwise."""
        with self._transfer_lock:
            if self._transfer_active:
                return False
            self._transfer_active = True
            return True

    def _end_transfer(self):
        with self._transfer_lock:
            self._transfer_active = False

    def _on_connect_success(self):
        """Handle successful connection"""
        self.connect_btn.config(state=tk.DISABLED)
        self.disconnect_btn.config(state=tk.NORMAL)
        self.status_bar.config(text=f"Connected to {self.host_entry.get()}")

        # Refresh remote list
        self._refresh_remote_list()

    def _on_disconnect(self):
        """Handle disconnect button"""
        if self.client:
            try:
                self.client.close()
            except:
                pass

        self.connected = False
        self.client = None

        self.connect_btn.config(state=tk.NORMAL)
        self.disconnect_btn.config(state=tk.DISABLED)
        self.status_bar.config(text="Not connected")

        # Clear remote list
        self.remote_listbox.delete(0, tk.END)

    def _on_upload(self):
        # Debounce button double-click
        if not self._debounce('upload'): return
        if not self.connected:
            messagebox.showwarning("Not Connected", "Please connect to a server first")
            return
        if not self._begin_transfer():
            self._log_output("Another transfer is in progress; please wait.")
            return

        selections = self.local_listbox.curselection()
        if not selections:
            self._end_transfer()  # Release transfer lock
            messagebox.showwarning("No Selection", "Please select files to upload")
            return

        local_path = self.local_path_entry.get()

        for idx in selections:
            filename = self.local_listbox.get(idx)
            if filename == ".." or filename.endswith("/"):
                continue

            local_file = os.path.join(local_path, filename)
            remote_file = filename

            def upload_thread(lf=local_file, rf=remote_file):
                try:
                    item_id = self.transfer_tree.insert('', tk.END,
                                                        values=(rf, 'Upload', 'Starting', '0%', '-'))
                    start_time = time.time()
                    response = self.client.execute_command('PASV')
                    if not response.is_success:
                        self._log_output(f"PASV failed: {response}")
                        self.root.after(0, lambda: (self.transfer_tree.set(item_id, 'Status', 'Failed'),
                                                    self._end_transfer()))
                        return
                    self.root.after(0, lambda: self.transfer_tree.set(item_id, 'Status', 'Uploading'))

                    def on_progress(done, total, i=item_id, st=start_time):
                        percent = (done / total * 100) if total else 0
                        speed = self._format_bps(done / max(1e-6, time.time() - st))
                        self.root.after(0, lambda percent=percent, speed=speed: (
                            self.transfer_tree.set(i, 'Progress', f"{percent:.0f}%"),
                            self.transfer_tree.set(i, 'Speed', speed),
                            self.progress_var.set(percent)
                        ))

                    def on_complete(success, result, i=item_id, rf=rf, st=start_time):
                        elapsed = max(1e-6, time.time() - st)
                        sent = os.path.getsize(lf) if os.path.exists(lf) else 0
                        final_speed = self._format_bps(sent / elapsed) if success else '-'
                        if success:
                            self._log_output(f"Uploaded: {rf}")
                            self.root.after(0, lambda: (
                                self.transfer_tree.set(i, 'Status', 'Complete'),
                                self.transfer_tree.set(i, 'Progress', '100%'),
                                self.transfer_tree.set(i, 'Speed', final_speed),
                                self._end_transfer(),
                                self._refresh_remote_list()
                            ))
                        else:
                            self._log_output(f"Upload failed: {result}")
                            self.root.after(0, lambda: (
                                self.transfer_tree.set(i, 'Status', 'Failed'),
                                self.transfer_tree.set(i, 'Speed', '-'),
                                self._end_transfer()
                            ))

                    result = self.client.execute_command(
                        'STOR', rf, local_path=lf,
                        callback=on_complete,
                        progress_callback=on_progress,
                        async_mode=True
                    )
                    # Consume generator to trigger async transfer start
                    if hasattr(result, '__iter__') and not isinstance(result, (str, bytes)):
                        for _ in result:
                            pass
                except Exception as e:
                    self._log_output(f"Upload error: {e}")
                    self.root.after(0, self._end_transfer)

            threading.Thread(target=upload_thread, daemon=True).start()

    def _on_download(self):
        if not self._debounce('download'): return
        if not self.connected:
            messagebox.showwarning("Not Connected", "Please connect to a server first")
            return
        if not self._begin_transfer():
            self._log_output("Another transfer is in progress; please wait.")
            return

        selections = self.remote_listbox.curselection()
        if not selections:
            self._end_transfer()  # Release transfer lock
            messagebox.showwarning("No Selection", "Please select files to download")
            return

        local_path = self.local_path_entry.get()

        for idx in selections:
            filename = self.remote_listbox.get(idx)
            if filename == ".." or filename.endswith("/"):
                continue

            local_file = os.path.join(local_path, filename)

            def download_thread(rf=filename, lf=local_file):
                try:
                    item_id = self.transfer_tree.insert('', tk.END,
                                                        values=(rf, 'Download', 'Starting', '0%', '-'))
                    start_time = time.time()
                    response = self.client.execute_command('PASV')
                    if not response.is_success:
                        self._log_output(f"PASV failed: {response}")
                        self.root.after(0, lambda: (self.transfer_tree.set(item_id, 'Status', 'Failed'),
                                                    self._end_transfer()))
                        return
                    self.root.after(0, lambda: self.transfer_tree.set(item_id, 'Status', 'Downloading'))

                    def on_progress(done, total, i=item_id, st=start_time):
                        percent = (done / total * 100) if total else 0
                        speed = self._format_bps(done / max(1e-6, time.time() - st))
                        self.root.after(0, lambda percent=percent, speed=speed: (
                            self.transfer_tree.set(i, 'Progress', f"{percent:.0f}%"),
                            self.transfer_tree.set(i, 'Speed', speed),
                            self.progress_var.set(percent)
                        ))

                    def on_complete(success, result, i=item_id, rf=rf, st=start_time):
                        final_size = os.path.getsize(lf) if os.path.exists(lf) else 0
                        elapsed = max(1e-6, time.time() - st)
                        final_speed = self._format_bps(final_size / elapsed) if success else '-'
                        if success:
                            self._log_output(f"Downloaded: {rf}")
                            self.root.after(0, lambda: (
                                self.transfer_tree.set(i, 'Status', 'Complete'),
                                self.transfer_tree.set(i, 'Progress', '100%'),
                                self.transfer_tree.set(i, 'Speed', final_speed),
                                self._end_transfer(),
                                self._refresh_local_list()
                            ))
                        else:
                            self._log_output(f"Download failed: {result}")
                            self.root.after(0, lambda: (
                                self.transfer_tree.set(i, 'Status', 'Failed'),
                                self.transfer_tree.set(i, 'Speed', '-'),
                                self._end_transfer()
                            ))

                    result = self.client.execute_command(
                        'RETR', rf, local_path=lf,
                        callback=on_complete,
                        progress_callback=on_progress,
                        async_mode=True
                    )
                    # Consume generator to trigger async transfer start
                    if hasattr(result, '__iter__') and not isinstance(result, (str, bytes)):
                        for _ in result:
                            pass
                except Exception as e:
                    self._log_output(f"Download error: {e}")
                    self.root.after(0, self._end_transfer)

            threading.Thread(target=download_thread, daemon=True).start()

    def _on_delete(self):
        """Handle delete button"""
        if not self.connected:
            messagebox.showwarning("Not Connected", "Please connect to a server first")
            return

        selections = self.remote_listbox.curselection()
        if not selections:
            messagebox.showwarning("No Selection", "Please select files to delete")
            return

        files = [self.remote_listbox.get(idx) for idx in selections
                 if self.remote_listbox.get(idx) != ".."]

        if not messagebox.askyesno("Confirm Delete",
                                   f"Delete {len(files)} file(s)?"):
            return

        def delete_thread():
            for filename in files:
                if filename.endswith("/"):
                    # Directory
                    response = self.client.execute_command('RMD', filename[:-1])
                else:
                    # File
                    response = self.client.execute_command('DELE', filename)

                self._log_output(f"Delete {filename}: {response}")

            self.root.after(0, self._refresh_remote_list)

        threading.Thread(target=delete_thread, daemon=True).start()

    def _on_abort(self):
        """Handle abort button"""
        if not self.connected:
            return

        def abort_thread():
            resp = None
            err = None
            try:
                resp = self.client.abort_transfer()
                # Cancel any active transfers and close data channel to fully reset state
                try:
                    self.client.transfer_manager.stop_all()
                except Exception:
                    pass
                try:
                    self.client.data_conn.close()
                except Exception:
                    pass
            except Exception as e:
                err = e
            finally:
                # Ensure UI updates happen in main thread
                def update_ui():
                    self._end_transfer()
                    if err:
                        self._log_output(f"Abort error: {err}")
                        messagebox.showerror("Abort Error", str(err))
                    elif resp:
                        self._log_output(f"Abort: {resp}")
                        messagebox.showinfo("Transfer Aborted", str(resp))

                self.root.after(0, update_ui)

        threading.Thread(target=abort_thread, daemon=True).start()

    def _on_clear_transfers(self):
        """Clear completed transfers from list"""
        for item in self.transfer_tree.get_children():
            status = self.transfer_tree.set(item, 'Status')
            if status in ['Complete', 'Failed', 'Error']:
                self.transfer_tree.delete(item)

    def _on_send_command(self):
        """Send raw FTP command"""
        if not self.connected:
            messagebox.showwarning("Not Connected", "Please connect to a server first")
            return

        command = self.cmd_entry.get().strip()
        if not command:
            return

        self.cmd_entry.delete(0, tk.END)
        self._send_raw_command(command)

    def _send_raw_command(self, command):
        """Send raw command to server"""

        def cmd_thread():
            try:
                parts = command.split(None, 1)
                cmd = parts[0].upper()
                arg = parts[1] if len(parts) > 1 else None

                # Check if this is a transfer command that needs special handling
                if cmd in ['STOR', 'RETR', 'APPE']:
                    self._handle_raw_transfer_command(cmd, arg)
                    return

                # Regular command execution
                if arg:
                    response = self.client.execute_command(cmd, arg)
                else:
                    response = self.client.execute_command(cmd)

                # Consume generator response if needed
                response = self._consume_generator_response(response)

                self._log_output(f"> {command}\n{response}")

            except Exception as e:
                self._log_output(f"> {command}\nError: {e}")

        threading.Thread(target=cmd_thread, daemon=True).start()

    def _handle_raw_transfer_command(self, cmd, arg):
        """Handle transfer commands (STOR/RETR/APPE) from command input with transfer UI"""
        if not arg:
            self._log_output(f"> {cmd}\nError: Missing filename argument")
            return

        # Parse arguments - format can be: filename or "local_file remote_file"
        parts = arg.split(None, 1)
        
        if cmd == 'STOR':
            # STOR local_file [remote_file]
            local_file = parts[0]
            remote_file = parts[1] if len(parts) > 1 else os.path.basename(local_file)
            
            if not os.path.exists(local_file):
                self._log_output(f"> STOR {arg}\nError: Local file not found: {local_file}")
                return
            
            self._execute_raw_upload(local_file, remote_file)
            
        elif cmd == 'RETR':
            # RETR remote_file [local_file]
            remote_file = parts[0]
            local_file = parts[1] if len(parts) > 1 else os.path.basename(remote_file)
            
            self._execute_raw_download(remote_file, local_file)
            
        elif cmd == 'APPE':
            # APPE local_file [remote_file]
            local_file = parts[0]
            remote_file = parts[1] if len(parts) > 1 else os.path.basename(local_file)
            
            if not os.path.exists(local_file):
                self._log_output(f"> APPE {arg}\nError: Local file not found: {local_file}")
                return
            
            self._execute_raw_append(local_file, remote_file)

    def _execute_raw_upload(self, local_file, remote_file):
        """Execute upload from raw command with transfer UI"""
        def upload_thread():
            try:
                item_id = self.transfer_tree.insert('', tk.END,
                                                    values=(remote_file, 'Upload', 'Starting', '0%', '-'))
                start_time = time.time()
                
                self.root.after(0, lambda: self.transfer_tree.set(item_id, 'Status', 'Uploading'))

                def on_progress(done, total):
                    percent = (done / total * 100) if total else 0
                    speed = self._format_bps(done / max(1e-6, time.time() - start_time))
                    self.root.after(0, lambda: (
                        self.transfer_tree.set(item_id, 'Progress', f"{percent:.0f}%"),
                        self.transfer_tree.set(item_id, 'Speed', speed),
                        self.progress_var.set(percent)
                    ))

                def on_complete(success, result):
                    elapsed = max(1e-6, time.time() - start_time)
                    sent = os.path.getsize(local_file) if os.path.exists(local_file) else 0
                    final_speed = self._format_bps(sent / elapsed) if success else '-'
                    
                    if success:
                        self._log_output(f"> STOR {remote_file}\n{result}")
                        self.root.after(0, lambda: (
                            self.transfer_tree.set(item_id, 'Status', 'Complete'),
                            self.transfer_tree.set(item_id, 'Progress', '100%'),
                            self.transfer_tree.set(item_id, 'Speed', final_speed),
                            self._refresh_remote_list()
                        ))
                    else:
                        self._log_output(f"> STOR {remote_file}\nFailed: {result}")
                        self.root.after(0, lambda: (
                            self.transfer_tree.set(item_id, 'Status', 'Failed'),
                            self.transfer_tree.set(item_id, 'Speed', '-')
                        ))

                result = self.client.execute_command('STOR', remote_file, local_path=local_file,
                                                     callback=on_complete,
                                                     progress_callback=on_progress,
                                                     async_mode=True)
                # Consume generator
                if hasattr(result, '__iter__') and not isinstance(result, (str, bytes)):
                    for _ in result:
                        pass
            except Exception as e:
                self._log_output(f"> STOR {remote_file}\nError: {e}")
                self.root.after(0, lambda: self.transfer_tree.set(item_id, 'Status', 'Failed'))

        threading.Thread(target=upload_thread, daemon=True).start()

    def _execute_raw_download(self, remote_file, local_file):
        """Execute download from raw command with transfer UI"""
        def download_thread():
            try:
                item_id = self.transfer_tree.insert('', tk.END,
                                                    values=(remote_file, 'Download', 'Starting', '0%', '-'))
                start_time = time.time()
                
                self.root.after(0, lambda: self.transfer_tree.set(item_id, 'Status', 'Downloading'))

                def on_progress(done, total):
                    percent = (done / total * 100) if total else 0
                    speed = self._format_bps(done / max(1e-6, time.time() - start_time))
                    self.root.after(0, lambda: (
                        self.transfer_tree.set(item_id, 'Progress', f"{percent:.0f}%"),
                        self.transfer_tree.set(item_id, 'Speed', speed),
                        self.progress_var.set(percent)
                    ))

                def on_complete(success, result):
                    elapsed = max(1e-6, time.time() - start_time)
                    recv = os.path.getsize(local_file) if os.path.exists(local_file) else 0
                    final_speed = self._format_bps(recv / elapsed) if success else '-'
                    
                    if success:
                        self._log_output(f"> RETR {remote_file}\n{result}")
                        self.root.after(0, lambda: (
                            self.transfer_tree.set(item_id, 'Status', 'Complete'),
                            self.transfer_tree.set(item_id, 'Progress', '100%'),
                            self.transfer_tree.set(item_id, 'Speed', final_speed)
                        ))
                    else:
                        self._log_output(f"> RETR {remote_file}\nFailed: {result}")
                        self.root.after(0, lambda: (
                            self.transfer_tree.set(item_id, 'Status', 'Failed'),
                            self.transfer_tree.set(item_id, 'Speed', '-')
                        ))

                result = self.client.execute_command('RETR', remote_file, local_path=local_file,
                                                     callback=on_complete,
                                                     progress_callback=on_progress,
                                                     async_mode=True)
                # Consume generator
                if hasattr(result, '__iter__') and not isinstance(result, (str, bytes)):
                    for _ in result:
                        pass
            except Exception as e:
                self._log_output(f"> RETR {remote_file}\nError: {e}")
                self.root.after(0, lambda: self.transfer_tree.set(item_id, 'Status', 'Failed'))

        threading.Thread(target=download_thread, daemon=True).start()

    def _execute_raw_append(self, local_file, remote_file):
        """Execute append from raw command with transfer UI"""
        def append_thread():
            try:
                item_id = self.transfer_tree.insert('', tk.END,
                                                    values=(remote_file, 'Append', 'Starting', '0%', '-'))
                start_time = time.time()
                
                self.root.after(0, lambda: self.transfer_tree.set(item_id, 'Status', 'Appending'))

                def on_progress(done, total):
                    percent = (done / total * 100) if total else 0
                    speed = self._format_bps(done / max(1e-6, time.time() - start_time))
                    self.root.after(0, lambda: (
                        self.transfer_tree.set(item_id, 'Progress', f"{percent:.0f}%"),
                        self.transfer_tree.set(item_id, 'Speed', speed),
                        self.progress_var.set(percent)
                    ))

                def on_complete(success, result):
                    elapsed = max(1e-6, time.time() - start_time)
                    sent = os.path.getsize(local_file) if os.path.exists(local_file) else 0
                    final_speed = self._format_bps(sent / elapsed) if success else '-'
                    
                    if success:
                        self._log_output(f"> APPE {remote_file}\n{result}")
                        self.root.after(0, lambda: (
                            self.transfer_tree.set(item_id, 'Status', 'Complete'),
                            self.transfer_tree.set(item_id, 'Progress', '100%'),
                            self.transfer_tree.set(item_id, 'Speed', final_speed),
                            self._refresh_remote_list()
                        ))
                    else:
                        self._log_output(f"> APPE {remote_file}\nFailed: {result}")
                        self.root.after(0, lambda: (
                            self.transfer_tree.set(item_id, 'Status', 'Failed'),
                            self.transfer_tree.set(item_id, 'Speed', '-')
                        ))

                result = self.client.execute_command('APPE', remote_file, local_path=local_file,
                                                     callback=on_complete,
                                                     progress_callback=on_progress,
                                                     async_mode=True)
                # Consume generator
                if hasattr(result, '__iter__') and not isinstance(result, (str, bytes)):
                    for _ in result:
                        pass
            except Exception as e:
                self._log_output(f"> APPE {remote_file}\nError: {e}")
                self.root.after(0, lambda: self.transfer_tree.set(item_id, 'Status', 'Failed'))

        threading.Thread(target=append_thread, daemon=True).start()

    def _on_local_double_click(self, event):
        """Handle double-click on local file"""
        selection = self.local_listbox.curselection()
        if not selection:
            return

        filename = self.local_listbox.get(selection[0])
        current_path = self.local_path_entry.get()

        if filename == "..":
            # Go up
            new_path = os.path.dirname(current_path)
        else:
            # Go into directory
            new_path = os.path.join(current_path, filename)

        if os.path.isdir(new_path):
            self.local_path_entry.delete(0, tk.END)
            self.local_path_entry.insert(0, new_path)
            self._refresh_local_list()

    def _on_remote_double_click(self, event):
        """Handle double-click on remote file"""
        if not self.connected:
            return

        selection = self.remote_listbox.curselection()
        if not selection:
            return

        filename = self.remote_listbox.get(selection[0])

        if not filename.endswith("/") and filename != "..":
            return  # Not a directory

        def navigate_thread():
            try:
                if filename == "..":
                    # Go up
                    response = self.client.execute_command('CDUP')
                else:
                    # Go into directory
                    dirname = filename[:-1]  # Remove trailing /
                    response = self.client.execute_command('CWD', dirname)

                if response.is_success:
                    # Get new path
                    pwd_response = self.client.execute_command('PWD')
                    if pwd_response.is_success:
                        from ..core.parser import ResponseParser
                        new_path = ResponseParser.parse_pwd_response(pwd_response)
                        if new_path:
                            self.root.after(0, lambda new_path=new_path: self.remote_path_entry.delete(0, tk.END))
                            self.root.after(0, lambda new_path=new_path: self.remote_path_entry.insert(0, new_path))

                    self.root.after(0, self._refresh_remote_list)
                else:
                    self._log_output(f"Navigate failed: {response}")

            except Exception as e:
                self._log_output(f"Navigate error: {e}")

        threading.Thread(target=navigate_thread, daemon=True).start()

    def _refresh_local_list(self):
        """Refresh local file list"""
        path = self.local_path_entry.get()

        try:
            self.local_listbox.delete(0, tk.END)

            # Add parent directory
            if path != "/":
                self.local_listbox.insert(tk.END, "..")

            # List files
            entries = os.listdir(path)
            entries.sort()

            # Directories first
            dirs = [e + "/" for e in entries if os.path.isdir(os.path.join(path, e))]
            files = [e for e in entries if os.path.isfile(os.path.join(path, e))]

            for item in dirs + files:
                self.local_listbox.insert(tk.END, item)

        except Exception as e:
            messagebox.showerror("Error", f"Failed to list local directory: {e}")

    def _refresh_remote_list(self):
        """Refresh remote file list"""
        if not self.connected:
            return

        def list_thread():
            try:
                # Setup passive mode
                response = self.client.execute_command('PASV')
                if not response.is_success:
                    self._log_output(f"PASV failed: {response}")
                    return

                # List directory
                path = self.remote_path_entry.get()
                result = self.client.execute_command('LIST', path)
                # Consume generator and get final response
                response = None
                if hasattr(result, '__iter__') and not isinstance(result, (str, bytes)):
                    for resp in result:
                        response = resp
                else:
                    response = result

                if response and response.is_success and self.client.last_transfer_data:
                    try:
                        listing = self.client.last_transfer_data.decode('utf-8', errors='ignore')
                    except Exception:
                        listing = self.client.last_transfer_data.decode('latin-1', errors='ignore')

                    # Parse listing lines more robustly
                    entries = []
                    for line in listing.strip().splitlines():
                        if not line:
                            continue
                        parts = line.split(None, 8)
                        if len(parts) >= 9:
                            filename = parts[8]
                            is_dir = parts[0].startswith('d')
                            entries.append(filename + "/" if is_dir else filename)

                    def update_list():
                        self.remote_listbox.delete(0, tk.END)
                        if path != "/":
                            self.remote_listbox.insert(tk.END, "..")
                        for entry in sorted(entries):
                            self.remote_listbox.insert(tk.END, entry)

                    self.root.after(0, update_list)
                else:
                    self._log_output(f"List failed: {response}")

            except Exception as e:
                self._log_output(f"List error: {e}")

        threading.Thread(target=list_thread, daemon=True).start()

    def _log_output(self, text):
        """Log text to output area"""

        def log():
            self.output_text.config(state=tk.NORMAL)
            self.output_text.insert(tk.END, text + "\n")
            self.output_text.see(tk.END)
            self.output_text.config(state=tk.DISABLED)

        self.root.after(0, log)

    def run(self):
        """Run GUI main loop"""
        self.root.mainloop()

    def _ask_offset(self, title, default_offset=0, hint_msg=None):
        """Prompt user for resume offset (returns int or None)
        Args:
            title: dialog title
            default_offset: integer to prefill as suggested offset
            hint_msg: optional hint string displayed above entry
        """
        dlg = tk.Toplevel(self.root)
        dlg.title(title)
        dlg.transient(self.root)
        dlg.grab_set()
        frame = ttk.Frame(dlg, padding=10)
        frame.pack(fill=tk.BOTH, expand=True)
        if hint_msg:
            ttk.Label(frame, text=hint_msg, foreground="#555").pack(anchor=tk.W, pady=(0, 6))
        ttk.Label(frame, text="Offset (bytes):").pack(anchor=tk.W)
        entry = ttk.Entry(frame)
        entry.pack(fill=tk.X, pady=5)
        entry.insert(0, str(default_offset))
        result = {'offset': None}

        def ok():
            val = entry.get().strip()
            if not val.isdigit():
                messagebox.showerror("Invalid", "Offset must be a non-negative integer")
                return
            result['offset'] = int(val)
            dlg.destroy()

        def cancel():
            dlg.destroy()

        btn_frame = ttk.Frame(frame)
        btn_frame.pack(pady=5)
        ttk.Button(btn_frame, text="OK", command=ok).pack(side=tk.LEFT, padx=5)
        ttk.Button(btn_frame, text="Cancel", command=cancel).pack(side=tk.LEFT, padx=5)
        self.root.wait_window(dlg)
        return result['offset']

    def _on_append(self):
        if not self._debounce('append'): return
        if not self.connected:
            messagebox.showwarning("Not Connected", "Connect first")
            return
        if not self._begin_transfer():
            self._log_output("Another transfer is in progress; please wait.")
            return
        # Select local single file
        selections = self.local_listbox.curselection()
        if len(selections) != 1:
            self._end_transfer()  # Release transfer lock
            messagebox.showwarning("Select One", "Select exactly one local file to append")
            return
        local_path = self.local_path_entry.get()
        filename = self.local_listbox.get(selections[0])
        if filename.endswith('/') or filename == '..':
            self._end_transfer()  # Release transfer lock
            messagebox.showwarning("Invalid", "Cannot append a directory")
            return
        local_file = os.path.join(local_path, filename)
        remote_name = filename  # append to file of same name

        def append_thread(lf=local_file, rf=remote_name):
            try:
                item_id = self.transfer_tree.insert('', tk.END, values=(rf, 'Append', 'Starting', '0%', '-'))
                resp = self.client.execute_command('PASV')
                if not resp.is_success:
                    self._log_output(f"PASV failed: {resp}")
                    self.root.after(0,
                                    lambda: (self.transfer_tree.set(item_id, 'Status', 'Failed'), self._end_transfer()))
                    return
                self.root.after(0, lambda: self.transfer_tree.set(item_id, 'Status', 'Appending'))
                start_time = time.time()

                def on_progress(done, total, i=item_id, st=start_time):
                    percent = (done / total * 100) if total else 0
                    speed = self._format_bps(done / max(1e-6, time.time() - st))
                    self.root.after(0, lambda percent=percent, speed=speed: (
                        self.transfer_tree.set(i, 'Progress', f"{percent:.0f}%"),
                        self.transfer_tree.set(i, 'Speed', speed),
                        self.progress_var.set(percent)
                    ))

                def on_complete(success, result, i=item_id, rf=rf, st=start_time):
                    size_bytes = os.path.getsize(lf) if os.path.exists(lf) else 0
                    elapsed = max(1e-6, time.time() - st)
                    final_speed = self._format_bps(size_bytes / elapsed) if success else '-'
                    if success:
                        self._log_output(f"Appended: {rf}")
                        self.root.after(0, lambda: (
                            self.transfer_tree.set(i, 'Status', 'Complete'),
                            self.transfer_tree.set(i, 'Progress', '100%'),
                            self.transfer_tree.set(i, 'Speed', final_speed),
                            self._end_transfer(),
                            self._refresh_remote_list()
                        ))
                    else:
                        self._log_output(f"Append failed: {result}")
                        self.root.after(0, lambda: (
                            self.transfer_tree.set(i, 'Status', 'Failed'),
                            self.transfer_tree.set(i, 'Speed', '-'),
                            self._end_transfer()
                        ))

                result = self.client.execute_command('APPE', rf, local_path=lf,
                                                     callback=on_complete,
                                                     progress_callback=on_progress,
                                                     async_mode=True)
                # Consume generator to trigger async transfer start
                if hasattr(result, '__iter__') and not isinstance(result, (str, bytes)):
                    for _ in result:
                        pass
            except Exception as e:
                self._log_output(f"Append error: {e}")
                self.root.after(0, self._end_transfer)

        threading.Thread(target=append_thread, daemon=True).start()

    def _on_resume_download(self):
        if not self._debounce('resume_download'): return
        if not self.connected:
            messagebox.showwarning("Not Connected", "Connect first")
            return
        if not self._begin_transfer():
            self._log_output("Another transfer is in progress; please wait.")
            return
        selections = self.remote_listbox.curselection()
        if len(selections) != 1:
            self._end_transfer()  # Release transfer lock
            messagebox.showwarning("Select One", "Select exactly one remote file")
            return
        filename = self.remote_listbox.get(selections[0])
        if filename.endswith('/') or filename == '..':
            self._end_transfer()  # Release transfer lock
            messagebox.showwarning("Invalid", "Cannot resume a directory")
            return
        local_file = os.path.join(self.local_path_entry.get(), filename)
        local_size = 0
        if os.path.exists(local_file):
            try:
                local_size = os.path.getsize(local_file)
            except Exception:
                local_size = 0
        remote_size = self.client.get_file_size(filename)
        hint_parts = []
        if remote_size is not None:
            hint_parts.append(f"Remote size: {remote_size} bytes")
        if local_size > 0:
            hint_parts.append(f"Local existing size: {local_size} bytes")
        # Validation: if local_size >= remote_size (and remote known), reset suggestion to 0
        suggested = local_size
        if remote_size is not None and local_size >= remote_size:
            suggested = 0
            if local_size > 0:
                hint_parts.append("Local file not smaller than remote; starting from 0")
        hint_msg = " | ".join(hint_parts) if hint_parts else "Enter resume offset"
        offset = self._ask_offset("Resume Download Offset", default_offset=suggested, hint_msg=hint_msg)
        if offset is None:
            self._end_transfer()  # Release transfer lock
            return

        def resume_thread(rf=filename, lf=local_file, off=offset):
            try:
                item_id = self.transfer_tree.insert('', tk.END, values=(rf, 'ResumeD', 'Starting', '0%', '-'))
                resp = self.client.execute_command('PASV')
                if not resp.is_success:
                    self._log_output(f"PASV failed: {resp}")
                    self.root.after(0,
                                    lambda: (self.transfer_tree.set(item_id, 'Status', 'Failed'), self._end_transfer()))
                    return
                self.root.after(0, lambda: self.transfer_tree.set(item_id, 'Status', 'Resuming'))
                start_time = time.time()

                def on_progress(done, total, i=item_id, st=start_time):
                    percent = (done / total * 100) if total else 0
                    speed = self._format_bps(done / max(1e-6, time.time() - st))
                    self.root.after(0, lambda percent=percent, speed=speed: (
                        self.transfer_tree.set(i, 'Progress', f"{percent:.0f}%"),
                        self.transfer_tree.set(i, 'Speed', speed),
                        self.progress_var.set(percent)
                    ))

                def on_complete(success, result, i=item_id, rf=rf, st=start_time):
                    size = os.path.getsize(lf) if os.path.exists(lf) else 0
                    elapsed = max(1e-6, time.time() - st)
                    final_speed = self._format_bps(size / elapsed) if success else '-'
                    if success:
                        self._log_output(f"Resumed download: {rf}")
                        self.root.after(0, lambda: (
                            self.transfer_tree.set(i, 'Status', 'Complete'),
                            self.transfer_tree.set(i, 'Progress', '100%'),
                            self.transfer_tree.set(i, 'Speed', final_speed),
                            self._end_transfer(),
                            self._refresh_local_list()
                        ))
                    else:
                        self._log_output(f"Resume download failed: {result}")
                        self.root.after(0, lambda: (
                            self.transfer_tree.set(i, 'Status', 'Failed'),
                            self.transfer_tree.set(i, 'Speed', '-'),
                            self._end_transfer()
                        ))

                result = self.client.execute_command('RETR', rf, local_path=lf, offset=off,
                                                     callback=on_complete,
                                                     progress_callback=on_progress,
                                                     async_mode=True)
                # Consume generator to trigger async transfer start
                if hasattr(result, '__iter__') and not isinstance(result, (str, bytes)):
                    for _ in result:
                        pass
            except Exception as e:
                self._log_output(f"Resume download error: {e}")
                self.root.after(0, self._end_transfer)

        threading.Thread(target=resume_thread, daemon=True).start()

    def _on_resume_upload(self):
        if not self._debounce('resume_upload'): return
        if not self.connected:
            messagebox.showwarning("Not Connected", "Connect first")
            return
        if not self._begin_transfer():
            self._log_output("Another transfer is in progress; please wait.")
            return
        selections = self.local_listbox.curselection()
        if len(selections) != 1:
            self._end_transfer()  # Release transfer lock
            messagebox.showwarning("Select One", "Select exactly one local file")
            return
        local_path = self.local_path_entry.get()
        filename = self.local_listbox.get(selections[0])
        if filename.endswith('/') or filename == '..':
            self._end_transfer()  # Release transfer lock
            messagebox.showwarning("Invalid", "Cannot resume a directory")
            return
        local_file = os.path.join(local_path, filename)
        if not os.path.exists(local_file):
            self._end_transfer()  # Release transfer lock
            messagebox.showwarning("Missing", "Local file does not exist for resume")
            return
        try:
            local_size = os.path.getsize(local_file)
        except Exception:
            local_size = 0
        remote_size = self.client.get_file_size(filename)
        hint_parts = [f"Local size: {local_size} bytes"]
        if remote_size is not None:
            hint_parts.append(f"Remote existing size: {remote_size} bytes")
        # Suggested offset is remote_size (continue uploading remainder)
        suggested = remote_size if remote_size is not None else 0
        # If remote larger than local (inconsistent), fallback to 0
        if remote_size is not None and remote_size >= local_size:
            suggested = 0
            hint_parts.append("Remote size >= local size; starting from 0")
        hint_msg = " | ".join(hint_parts)
        offset = self._ask_offset("Resume Upload Offset", default_offset=suggested, hint_msg=hint_msg)
        if offset is None:
            self._end_transfer()  # Release transfer lock
            return
        remote_name = filename

        def resume_up_thread(lf=local_file, rf=remote_name, off=offset):
            try:
                item_id = self.transfer_tree.insert('', tk.END, values=(rf, 'ResumeU', 'Starting', '0%', '-'))
                resp = self.client.execute_command('PASV')
                if not resp.is_success:
                    self._log_output(f"PASV failed: {resp}")
                    self.root.after(0,
                                    lambda: (self.transfer_tree.set(item_id, 'Status', 'Failed'), self._end_transfer()))
                    return
                self.root.after(0, lambda: self.transfer_tree.set(item_id, 'Status', 'Resuming'))
                start_time = time.time()

                def on_progress(done, total, i=item_id, st=start_time):
                    percent = (done / total * 100) if total else 0
                    speed = self._format_bps(done / max(1e-6, time.time() - st))
                    self.root.after(0, lambda percent=percent, speed=speed: (
                        self.transfer_tree.set(i, 'Progress', f"{percent:.0f}%"),
                        self.transfer_tree.set(i, 'Speed', speed),
                        self.progress_var.set(percent)
                    ))

                def on_complete(success, result, i=item_id, rf=rf, st=start_time):
                    size = os.path.getsize(lf) if os.path.exists(lf) else 0
                    elapsed = max(1e-6, time.time() - st)
                    final_speed = self._format_bps(size / elapsed) if success else '-'
                    if success:
                        self._log_output(f"Resumed upload: {rf}")
                        self.root.after(0, lambda: (
                            self.transfer_tree.set(i, 'Status', 'Complete'),
                            self.transfer_tree.set(i, 'Progress', '100%'),
                            self.transfer_tree.set(i, 'Speed', final_speed),
                            self._end_transfer(),
                            self._refresh_remote_list()
                        ))
                    else:
                        self._log_output(f"Resume upload failed: {result}")
                        self.root.after(0, lambda: (
                            self.transfer_tree.set(i, 'Status', 'Failed'),
                            self.transfer_tree.set(i, 'Speed', '-'),
                            self._end_transfer()
                        ))

                result = self.client.execute_command('STOR', rf, local_path=lf, offset=off,
                                                     callback=on_complete,
                                                     progress_callback=on_progress,
                                                     async_mode=True)
                # Consume generator to trigger async transfer start
                if hasattr(result, '__iter__') and not isinstance(result, (str, bytes)):
                    for _ in result:
                        pass
            except Exception as e:
                self._log_output(f"Resume upload error: {e}")
                self.root.after(0, self._end_transfer)

        threading.Thread(target=resume_up_thread, daemon=True).start()

    def _on_rename(self):
        """Rename remote file/directory"""
        if not self.connected:
            messagebox.showwarning("Not Connected", "Connect first")
            return
        selections = self.remote_listbox.curselection()
        if len(selections) != 1:
            messagebox.showwarning("Select One", "Select exactly one remote entry")
            return
        old_name = self.remote_listbox.get(selections[0])
        if old_name == '..':
            messagebox.showwarning("Invalid", "Cannot rename parent placeholder")
            return
        # Remove trailing slash for directories
        if old_name.endswith('/'):
            old_name = old_name[:-1]
        # Ask new name
        dlg = tk.Toplevel(self.root)
        dlg.title("Rename")
        dlg.transient(self.root)
        dlg.grab_set()
        ttk.Label(dlg, text=f"Rename '{old_name}' to:").pack(padx=10, pady=5)
        entry = ttk.Entry(dlg)
        entry.pack(padx=10, pady=5)
        entry.insert(0, old_name)
        result = {'new': None}

        def ok():
            new_name = entry.get().strip()
            if not new_name or new_name == old_name:
                messagebox.showerror("Invalid", "New name must differ and not be empty")
                return
            result['new'] = new_name
            dlg.destroy()

        def cancel():
            dlg.destroy()

        bframe = ttk.Frame(dlg)
        bframe.pack(pady=5)
        ttk.Button(bframe, text="OK", command=ok).pack(side=tk.LEFT, padx=5)
        ttk.Button(bframe, text="Cancel", command=cancel).pack(side=tk.LEFT, padx=5)
        self.root.wait_window(dlg)
        new_name = result['new']
        if not new_name:
            return

        def rename_thread(o=old_name, n=new_name):
            try:
                resp = self.client.rename(o, n)
                self._log_output(f"Rename {o} -> {n}: {resp}")
                if resp.is_success:
                    self.root.after(0, self._refresh_remote_list)
            except Exception as e:
                self._log_output(f"Rename error: {e}")

        threading.Thread(target=rename_thread, daemon=True).start()
