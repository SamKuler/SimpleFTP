"""
Transfer management module
Handles asynchronous file transfers with progress tracking and resume capability
"""

import os
import threading
import time
from enum import Enum


class TransferType(Enum):
    """Types of file transfers"""
    DOWNLOAD = "download"
    UPLOAD = "upload"
    APPEND = "append"


class TransferStatus(Enum):
    """Status of a transfer"""
    PENDING = "pending"
    RUNNING = "running"
    PAUSED = "paused"
    COMPLETED = "completed"
    FAILED = "failed"
    CANCELLED = "cancelled"


class Transfer:
    """Represents a single file transfer"""

    def __init__(self, transfer_id, transfer_type, remote_path, local_path=None,
                 data=None, offset=0, total_size=None):
        """
        Initialize transfer
        
        Args:
            transfer_id: Unique transfer identifier
            transfer_type: TransferType enum value
            remote_path: Remote file path
            local_path: Local file path
            data: Data bytes (for uploads without file)
            offset: Starting byte offset for resume
            total_size: Total file size (if known)
        """
        self.id = transfer_id
        self.type = transfer_type
        self.remote_path = remote_path
        self.local_path = local_path
        self.data = data
        self.offset = offset
        self.total_size = total_size

        self.status = TransferStatus.PENDING
        self.bytes_transferred = offset
        self.start_time = None
        self.end_time = None
        self.error = None

        self.thread = None
        self.pause_event = threading.Event()
        self.pause_event.set()  # Not paused initially
        self.cancel_event = threading.Event()

        self.callback = None
        self.progress_callback = None

    @property
    def is_active(self):
        """Check if transfer is active (running or paused)"""
        return self.status in (TransferStatus.RUNNING, TransferStatus.PAUSED)

    @property
    def is_complete(self):
        """Check if transfer is complete"""
        return self.status in (TransferStatus.COMPLETED, TransferStatus.FAILED, TransferStatus.CANCELLED)

    @property
    def progress_percent(self):
        """Get transfer progress percentage"""
        if self.total_size and self.total_size > 0:
            return (self.bytes_transferred / self.total_size) * 100
        return 0

    @property
    def speed(self):
        """Get transfer speed in bytes/second"""
        if self.start_time and self.status == TransferStatus.RUNNING:
            elapsed = time.time() - self.start_time
            if elapsed > 0:
                return self.bytes_transferred / elapsed
        return 0

    def pause(self):
        """Pause the transfer"""
        if self.status == TransferStatus.RUNNING:
            self.pause_event.clear()
            self.status = TransferStatus.PAUSED

    def resume(self):
        """Resume the transfer"""
        if self.status == TransferStatus.PAUSED:
            self.pause_event.set()
            self.status = TransferStatus.RUNNING

    def cancel(self):
        """Cancel the transfer"""
        self.cancel_event.set()
        self.status = TransferStatus.CANCELLED


class TransferManager:
    """Manages multiple concurrent file transfers"""

    def __init__(self, client, max_concurrent=None):
        """
        Initialize transfer manager
        
        Args:
            client: FTPClient instance
            max_concurrent: Maximum concurrent transfers (None = unlimited, but effectively 1 due to single data connection)
        """
        self.client = client
        self.max_concurrent = max_concurrent if max_concurrent is None or max_concurrent <= 1 else 1  # Limit to 1 due to single data connection

        self.transfers = {}  # transfer_id -> Transfer
        self.transfer_counter = 0
        self.transfer_lock = threading.Lock()

        self.active_count = 0
        self.queue_condition = threading.Condition()

    def _get_next_id(self):
        """Get next transfer ID"""
        with self.transfer_lock:
            self.transfer_counter += 1
            return self.transfer_counter

    def _can_start_transfer(self):
        """Check if a new transfer can be started"""
        if self.max_concurrent is None:
            return True
        return self.active_count < self.max_concurrent

    def _wait_for_slot(self):
        """Wait for an available transfer slot"""
        if self.max_concurrent is None:
            return

        with self.queue_condition:
            while self.active_count >= self.max_concurrent:
                self.queue_condition.wait()

    def _acquire_slot(self):
        """Acquire a transfer slot"""
        with self.queue_condition:
            self.active_count += 1

    def _release_slot(self):
        """Release a transfer slot"""
        with self.queue_condition:
            self.active_count -= 1
            self.queue_condition.notify()

    def start_download(self, filename, local_path=None, offset=0,
                       callback=None, progress_callback=None):
        """
        Start downloading a file
        
        Args:
            filename: Remote filename
            local_path: Local path to save file
            offset: Byte offset for resume
            callback: Completion callback(success, data_or_error)
            progress_callback: Progress callback(bytes_transferred, total_bytes)
            
        Returns:
            Transfer: Transfer object
        """
        # Do not issue SIZE here to avoid interfering with the control channel during RETR
        total_size = None

        transfer_id = self._get_next_id()
        transfer = Transfer(
            transfer_id=transfer_id,
            transfer_type=TransferType.DOWNLOAD,
            remote_path=filename,
            local_path=local_path,
            offset=offset,
            total_size=total_size
        )
        transfer.callback = callback
        transfer.progress_callback = progress_callback

        with self.transfer_lock:
            self.transfers[transfer_id] = transfer

        # Start transfer thread
        transfer.thread = threading.Thread(
            target=self._download_worker,
            args=(transfer,),
            daemon=True
        )
        transfer.thread.start()

        return transfer

    def start_upload(self, filename, data=None, local_path=None, offset=0,
                     callback=None, progress_callback=None):
        """
        Start uploading a file
        
        Args:
            filename: Remote filename
            data: File data as bytes
            local_path: Local file path (if data is None)
            offset: Byte offset for resume
            callback: Completion callback(success, response)
            progress_callback: Progress callback(bytes_transferred, total_bytes)
            
        Returns:
            Transfer: Transfer object
        """
        # Determine total size
        total_size = None
        if data:
            total_size = len(data)
        elif local_path and os.path.exists(local_path):
            total_size = os.path.getsize(local_path)

        transfer_id = self._get_next_id()
        transfer = Transfer(
            transfer_id=transfer_id,
            transfer_type=TransferType.UPLOAD,
            remote_path=filename,
            local_path=local_path,
            data=data,
            offset=offset,
            total_size=total_size
        )
        transfer.callback = callback
        transfer.progress_callback = progress_callback

        with self.transfer_lock:
            self.transfers[transfer_id] = transfer

        # Start transfer thread
        transfer.thread = threading.Thread(
            target=self._upload_worker,
            args=(transfer,),
            daemon=True
        )
        transfer.thread.start()

        return transfer

    def start_append(self, filename, data=None, local_path=None,
                     callback=None, progress_callback=None):
        """
        Start appending to a file
        
        Args:
            filename: Remote filename
            data: File data as bytes
            local_path: Local file path (if data is None)
            callback: Completion callback(success, response)
            progress_callback: Progress callback(bytes_transferred, total_bytes)
            
        Returns:
            Transfer: Transfer object
        """
        # Determine total size
        total_size = None
        if data:
            total_size = len(data)
        elif local_path and os.path.exists(local_path):
            total_size = os.path.getsize(local_path)

        transfer_id = self._get_next_id()
        transfer = Transfer(
            transfer_id=transfer_id,
            transfer_type=TransferType.APPEND,
            remote_path=filename,
            local_path=local_path,
            data=data,
            total_size=total_size
        )
        transfer.callback = callback
        transfer.progress_callback = progress_callback

        with self.transfer_lock:
            self.transfers[transfer_id] = transfer

        # Start transfer thread
        transfer.thread = threading.Thread(
            target=self._append_worker,
            args=(transfer,),
            daemon=True
        )
        transfer.thread.start()

        return transfer

    def _download_worker(self, transfer):
        """Worker thread for downloading files"""
        self._wait_for_slot()
        self._acquire_slot()

        try:
            transfer.status = TransferStatus.RUNNING
            transfer.start_time = time.time()

            # Connect data channel if not already connected
            try:
                if not (self.client.data_conn.connection and self.client.data_conn.connection.is_connected):
                    self.client.data_conn.connect()
            except Exception as e:
                raise

            # Open file for writing
            mode = 'ab' if transfer.offset > 0 else 'wb'
            file_handle = None

            if transfer.local_path:
                file_handle = open(transfer.local_path, mode)

            data_buffer = b''
            buffer_size = 8192

            # Receive data
            while not transfer.cancel_event.is_set():
                # Wait if paused
                transfer.pause_event.wait()

                try:
                    chunk = self.client.data_conn.recv_data(buffer_size)
                    if not chunk:
                        break

                    if file_handle:
                        file_handle.write(chunk)
                    else:
                        data_buffer += chunk

                    transfer.bytes_transferred += len(chunk)

                    # Report progress
                    if transfer.progress_callback:
                        transfer.progress_callback(transfer.bytes_transferred, transfer.total_size)

                except Exception as e:
                    transfer.error = str(e)
                    break

            # Close file
            if file_handle:
                file_handle.close()

            # Close data connection
            self.client.data_conn.close()

            # Get completion response
            lines = self.client.control_conn.recv_multiline()
            from .parser import ResponseParser
            final_response = ResponseParser.parse(lines)

            # Update status
            if transfer.cancel_event.is_set():
                transfer.status = TransferStatus.CANCELLED
                success = False
                result = "Transfer cancelled"
            elif final_response.is_success:
                transfer.status = TransferStatus.COMPLETED
                success = True
                result = final_response
                # Store in client for compatibility
                if not transfer.local_path:
                    self.client.last_transfer_data = data_buffer
            else:
                transfer.status = TransferStatus.FAILED
                transfer.error = str(final_response)
                success = False
                result = final_response

            transfer.end_time = time.time()

            # Call completion callback
            if transfer.callback:
                transfer.callback(success, result)

        except Exception as e:
            transfer.status = TransferStatus.FAILED
            transfer.error = str(e)
            transfer.end_time = time.time()

            if transfer.callback:
                transfer.callback(False, str(e))

        finally:
            self._release_slot()

    def _upload_worker(self, transfer):
        """Worker thread for uploading files"""
        self._wait_for_slot()
        self._acquire_slot()

        try:
            transfer.status = TransferStatus.RUNNING
            transfer.start_time = time.time()

            # Connect data channel if not already connected
            try:
                if not (self.client.data_conn.connection and self.client.data_conn.connection.is_connected):
                    self.client.data_conn.connect()
            except Exception as e:
                raise

            # Get data to send
            if transfer.data:
                data_to_send = transfer.data[transfer.offset:]
            elif transfer.local_path:
                with open(transfer.local_path, 'rb') as f:
                    if transfer.offset > 0:
                        f.seek(transfer.offset)
                    data_to_send = f.read()
            else:
                raise ValueError("No data or file specified for upload")

            # Send data in chunks
            buffer_size = 8192
            offset = 0

            while offset < len(data_to_send) and not transfer.cancel_event.is_set():
                # Wait if paused
                transfer.pause_event.wait()

                chunk = data_to_send[offset:offset + buffer_size]
                self.client.data_conn.send_data(chunk)

                offset += len(chunk)
                transfer.bytes_transferred = transfer.offset + offset

                # Report progress
                if transfer.progress_callback:
                    transfer.progress_callback(transfer.bytes_transferred, transfer.total_size)

            # Close data connection
            self.client.data_conn.close()

            # Get completion response
            lines = self.client.control_conn.recv_multiline()
            from .parser import ResponseParser
            final_response = ResponseParser.parse(lines)

            # Update status
            if transfer.cancel_event.is_set():
                transfer.status = TransferStatus.CANCELLED
                success = False
                result = "Transfer cancelled"
            elif final_response.is_success:
                transfer.status = TransferStatus.COMPLETED
                success = True
                result = final_response
            else:
                transfer.status = TransferStatus.FAILED
                transfer.error = str(final_response)
                success = False
                result = final_response

            transfer.end_time = time.time()

            # Call completion callback
            if transfer.callback:
                transfer.callback(success, result)

        except Exception as e:
            transfer.status = TransferStatus.FAILED
            transfer.error = str(e)
            transfer.end_time = time.time()

            if transfer.callback:
                transfer.callback(False, str(e))

        finally:
            self._release_slot()

    def _append_worker(self, transfer):
        """Worker thread for appending to files"""
        # Similar to upload but uses APPE instead of STOR
        self._wait_for_slot()
        self._acquire_slot()

        try:
            transfer.status = TransferStatus.RUNNING
            transfer.start_time = time.time()

            # Connect data channel if not already connected
            try:
                if not (self.client.data_conn.connection and self.client.data_conn.connection.is_connected):
                    self.client.data_conn.connect()
            except Exception as e:
                raise

            # Get data to send
            if transfer.data:
                data_to_send = transfer.data
            elif transfer.local_path:
                with open(transfer.local_path, 'rb') as f:
                    data_to_send = f.read()
            else:
                raise ValueError("No data or file specified for append")

            # Send data in chunks
            buffer_size = 8192
            offset = 0

            while offset < len(data_to_send) and not transfer.cancel_event.is_set():
                # Wait if paused
                transfer.pause_event.wait()

                chunk = data_to_send[offset:offset + buffer_size]
                self.client.data_conn.send_data(chunk)

                offset += len(chunk)
                transfer.bytes_transferred = offset

                # Report progress
                if transfer.progress_callback:
                    transfer.progress_callback(transfer.bytes_transferred, transfer.total_size)

            # Close data connection
            self.client.data_conn.close()

            # Get completion response
            lines = self.client.control_conn.recv_multiline()
            from .parser import ResponseParser
            final_response = ResponseParser.parse(lines)

            # Update status
            if transfer.cancel_event.is_set():
                transfer.status = TransferStatus.CANCELLED
                success = False
                result = "Transfer cancelled"
            elif final_response.is_success:
                transfer.status = TransferStatus.COMPLETED
                success = True
                result = final_response
            else:
                transfer.status = TransferStatus.FAILED
                transfer.error = str(final_response)
                success = False
                result = final_response

            transfer.end_time = time.time()

            # Call completion callback
            if transfer.callback:
                transfer.callback(success, result)

        except Exception as e:
            transfer.status = TransferStatus.FAILED
            transfer.error = str(e)
            transfer.end_time = time.time()

            if transfer.callback:
                transfer.callback(False, str(e))

        finally:
            self._release_slot()

    def get_transfer(self, transfer_id):
        """Get transfer by ID"""
        with self.transfer_lock:
            return self.transfers.get(transfer_id)

    def get_all_transfers(self):
        """Get all transfers"""
        with self.transfer_lock:
            return list(self.transfers.values())

    def get_active_transfers(self):
        """Get all active transfers"""
        with self.transfer_lock:
            return [t for t in self.transfers.values() if t.is_active]

    def pause_transfer(self, transfer_id):
        """Pause a transfer"""
        transfer = self.get_transfer(transfer_id)
        if transfer:
            transfer.pause()

    def resume_transfer(self, transfer_id):
        """Resume a transfer"""
        transfer = self.get_transfer(transfer_id)
        if transfer:
            transfer.resume()

    def cancel_transfer(self, transfer_id):
        """Cancel a transfer"""
        transfer = self.get_transfer(transfer_id)
        if transfer:
            transfer.cancel()
            # Also send ABOR to server if transfer is active
            if transfer.is_active:
                try:
                    self.client.abort_transfer()
                except:
                    pass

    def stop_all(self):
        """Stop all transfers"""
        with self.transfer_lock:
            for transfer in self.transfers.values():
                if transfer.is_active:
                    transfer.cancel()

        # Wait for all threads to finish
        with self.transfer_lock:
            threads = [t.thread for t in self.transfers.values() if t.thread]

        for thread in threads:
            if thread and thread.is_alive():
                thread.join(timeout=5)
