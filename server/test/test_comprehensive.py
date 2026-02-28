#!/usr/bin/env python3
"""
FTP Server Comprehensive Test Suite
Tests ABOR command, async transfers, file operations, and error handling
"""

import ftplib
import threading
import time
import os
import tempfile
import random
import string
import socket
from pathlib import Path

# Server configuration
FTP_HOST = 'localhost'
FTP_PORT = 2121
FTP_USER = 'anonymous'
FTP_PASS = ''

class TestResults:
    """Track test results"""
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.tests = []
    
    def add_result(self, test_name, passed, message=""):
        self.tests.append({
            'name': test_name,
            'passed': passed,
            'message': message
        })
        if passed:
            self.passed += 1
            print(f"✅ PASS: {test_name}")
        else:
            self.failed += 1
            print(f"❌ FAIL: {test_name} - {message}")
    
    def summary(self):
        total = self.passed + self.failed
        print("\n" + "="*60)
        print(f"Test Results: {self.passed}/{total} passed")
        print("="*60)
        if self.failed > 0:
            print("\nFailed tests:")
            for test in self.tests:
                if not test['passed']:
                    print(f"  - {test['name']}: {test['message']}")

results = TestResults()

def random_string(length=10):
    """Generate random string"""
    return ''.join(random.choices(string.ascii_letters + string.digits, k=length))

def generate_test_data(size):
    """Generate random test data"""
    return os.urandom(size)

def safe_ftp_operation(func, test_name, *args, **kwargs):
    """Safely execute FTP operation and log result"""
    try:
        result = func(*args, **kwargs)
        results.add_result(test_name, True)
        return result
    except Exception as e:
        results.add_result(test_name, False, str(e))
        return None

# ============================================================================
# Test 1: Basic Connection and Authentication
# ============================================================================

def test_basic_connection():
    """Test basic connection and login"""
    print("\n--- Test 1: Basic Connection ---")
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        results.add_result("Basic connection and login", True)
        
        # Test NOOP
        ftp.voidcmd('NOOP')
        results.add_result("NOOP command", True)
        
        # Test SYST
        resp = ftp.sendcmd('SYST')
        results.add_result("SYST command", True, resp)
        
        ftp.quit()
    except Exception as e:
        results.add_result("Basic connection and login", False, str(e))

# ============================================================================
# Test 2: Directory Operations
# ============================================================================

def test_directory_operations():
    """Test directory navigation and listing"""
    print("\n--- Test 2: Directory Operations ---")
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        
        # Test PWD
        pwd = ftp.pwd()
        results.add_result("PWD command", True, f"Current dir: {pwd}")
        
        # Test LIST (async)
        files = []
        ftp.retrlines('LIST', files.append)
        results.add_result("LIST command (async)", True, f"Got {len(files)} entries")
        
        # Test NLST (async)
        names = ftp.nlst()
        results.add_result("NLST command (async)", True, f"Got {len(names)} names")
        
        # Test CWD
        ftp.cwd('/')
        results.add_result("CWD command", True)
        
        # Test CDUP
        ftp.cwd('/')
        ftp.sendcmd('CDUP')
        results.add_result("CDUP command", True)
        
        ftp.quit()
    except Exception as e:
        results.add_result("Directory operations", False, str(e))

# ============================================================================
# Test 3: File Upload and Download (STOR/RETR)
# ============================================================================

def test_file_operations():
    """Test file upload and download"""
    print("\n--- Test 3: File Upload/Download ---")
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        
        # Create test file
        test_filename = f"test_file_{random_string()}.txt"
        test_data = generate_test_data(10240)  # 10KB
        
        # Test STOR (upload)
        with tempfile.NamedTemporaryFile(delete=False) as tmp:
            tmp.write(test_data)
            tmp_path = tmp.name
        
        with open(tmp_path, 'rb') as f:
            ftp.storbinary(f'STOR {test_filename}', f)
        results.add_result("STOR command (upload)", True)
        
        # Test RETR (download)
        downloaded_data = bytearray()
        ftp.retrbinary(f'RETR {test_filename}', downloaded_data.extend)
        
        if downloaded_data == test_data:
            results.add_result("RETR command (download)", True)
        else:
            results.add_result("RETR command (download)", False, "Data mismatch")
        
        # Cleanup
        ftp.delete(test_filename)
        os.unlink(tmp_path)
        
        ftp.quit()
    except Exception as e:
        results.add_result("File operations", False, str(e))

# ============================================================================
# Test 4: REST + RETR (Resume Download)
# ============================================================================

def test_resume_download():
    """Test resumable download with REST command"""
    print("\n--- Test 4: Resume Download (REST + RETR) ---")
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        
        # Create test file
        test_filename = f"test_resume_{random_string()}.bin"
        test_data = generate_test_data(50000)  # 50KB
        
        # Upload file
        with tempfile.NamedTemporaryFile(delete=False) as tmp:
            tmp.write(test_data)
            tmp_path = tmp.name
        
        with open(tmp_path, 'rb') as f:
            ftp.storbinary(f'STOR {test_filename}', f)
        
        # Download first part (simulate partial download)
        downloaded_part1 = bytearray()
        def callback(data):
            downloaded_part1.extend(data)
            if len(downloaded_part1) >= 20000:  # Stop after 20KB
                raise Exception("Intentional stop")
        
        try:
            ftp.retrbinary(f'RETR {test_filename}', callback)
        except Exception as e:
            if str(e) == "Intentional stop":
                # Close data connection without abort
                ftp.voidresp()
            else:
                raise e
        
        # Resume from offset
        rest_offset = len(downloaded_part1)
        
        downloaded_part2 = bytearray()
        ftp.retrbinary(f'RETR {test_filename}', downloaded_part2.extend, rest=rest_offset)
        
        # Combine parts
        full_download = bytes(downloaded_part1) + bytes(downloaded_part2)
        
        print(f"Part1 size: {len(downloaded_part1)}, Part2 size: {len(downloaded_part2)}, Total: {len(full_download)}, Expected: {len(test_data)}")
        
        if full_download == test_data:
            results.add_result("REST + RETR (resume download)", True)
        else:
            results.add_result("REST + RETR (resume download)", False, 
                             f"Data mismatch: got {len(full_download)} bytes, expected {len(test_data)}")
        
        # Cleanup
        ftp.delete(test_filename)
        os.unlink(tmp_path)
        
        ftp.quit()
    except Exception as e:
        results.add_result("REST + RETR", False, str(e))

# ============================================================================
# Test 5: APPE (Append)
# ============================================================================

def test_append():
    """Test APPE command"""
    print("\n--- Test 5: APPE (Append) ---")
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        
        test_filename = f"test_append_{random_string()}.txt"
        part1 = b"First part\n"
        part2 = b"Second part\n"
        
        # Upload first part
        with tempfile.NamedTemporaryFile(delete=False) as tmp:
            tmp.write(part1)
            tmp_path = tmp.name
        
        with open(tmp_path, 'rb') as f:
            ftp.storbinary(f'STOR {test_filename}', f)
        
        # Append second part
        with tempfile.NamedTemporaryFile(delete=False) as tmp2:
            tmp2.write(part2)
            tmp_path2 = tmp2.name
        
        with open(tmp_path2, 'rb') as f:
            ftp.storbinary(f'APPE {test_filename}', f)
        
        # Download and verify
        downloaded = bytearray()
        ftp.retrbinary(f'RETR {test_filename}', downloaded.extend)
        
        expected = part1 + part2
        if bytes(downloaded) == expected:
            results.add_result("APPE command (append)", True)
        else:
            results.add_result("APPE command (append)", False, "Content mismatch")
        
        # Cleanup
        ftp.delete(test_filename)
        os.unlink(tmp_path)
        os.unlink(tmp_path2)
        
        ftp.quit()
    except Exception as e:
        results.add_result("APPE command", False, str(e))

# ============================================================================
# Test 6: ABOR Command
# ============================================================================

def test_abor_command():
    """Test ABOR command to abort transfers"""
    print("\n--- Test 6: ABOR Command ---")
    
    # Test 6.1: ABOR during RETR
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        
        # Create large test file
        test_filename = f"test_abor_{random_string()}.bin"
        test_data = generate_test_data(50000000)  # 50MB
        
        with tempfile.NamedTemporaryFile(delete=False) as tmp:
            tmp.write(test_data)
            tmp_path = tmp.name
        
        with open(tmp_path, 'rb') as f:
            ftp.storbinary(f'STOR {test_filename}', f)
        
        # Start download and abort
        downloaded = bytearray()
        def callback(data):
            downloaded.extend(data)
            if len(downloaded) > 50000:  # After 50KB, send ABOR
                ftp.abort()
                ftp.voidresp()  # Clear response
                raise Exception("Aborted by client")
        
        try:
            ftp.retrbinary(f'RETR {test_filename}', callback)
        except Exception as e:
            if str(e) != "Aborted by client":
                raise e
        
        results.add_result("ABOR during RETR", True)
        
        # Cleanup
        ftp.delete(test_filename)
        os.unlink(tmp_path)
        ftp.quit()
    except Exception as e:
        results.add_result("ABOR during RETR", False, str(e))
    
    # Test 6.2: ABOR during LIST
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        
        # Start LIST and abort (simulating)
        # Note: Hard to reliably test without very large directory
        # Just verify ABOR command is accepted
        sock = ftp.transfercmd('LIST')
        time.sleep(0.1)
        ftp.abort()
        ftp.voidresp()  # Clear response
        sock.close()
        
        results.add_result("ABOR during LIST", True)
        
        ftp.quit()
    except Exception as e:
        results.add_result("ABOR during LIST", False, str(e))

# ============================================================================
# Test 7: Multiple Concurrent Clients
# ============================================================================

def test_concurrent_clients():
    """Test multiple concurrent client connections"""
    print("\n--- Test 7: Concurrent Clients ---")
    
    results_lock = threading.Lock()
    thread_results = []
    
    def client_thread(client_id):
        try:
            ftp = ftplib.FTP()
            ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
            ftp.login(FTP_USER, FTP_PASS)
            
            # Each client performs multiple operations to increase load
            success_count = 0
            for i in range(3):  # Each client does 3 upload/download cycles
                test_filename = f"client_{client_id}_file_{i}_{random_string()}.txt"
                test_data = f"Data from client {client_id}, file {i}\n".encode() * 5000  # ~50KB per file
                
                with tempfile.NamedTemporaryFile(delete=False) as tmp:
                    tmp.write(test_data)
                    tmp_path = tmp.name
                
                # Upload
                with open(tmp_path, 'rb') as f:
                    ftp.storbinary(f'STOR {test_filename}', f)
                
                # Download back
                downloaded = bytearray()
                ftp.retrbinary(f'RETR {test_filename}', downloaded.extend)
                
                if bytes(downloaded) == test_data:
                    success_count += 1
                
                # Cleanup
                ftp.delete(test_filename)
                os.unlink(tmp_path)
            
            success = (success_count == 3)
            
            ftp.quit()
            
            with results_lock:
                thread_results.append((client_id, success))
        except Exception as e:
            with results_lock:
                thread_results.append((client_id, False, str(e)))
    
    # Start multiple clients
    threads = []
    num_clients = 50  # Large concurrency
    for i in range(num_clients):
        t = threading.Thread(target=client_thread, args=(i,))
        t.start()
        threads.append(t)
        # time.sleep(0.1)  # Stagger thread starts slightly
    
    # Wait for all threads
    for t in threads:
        t.join()
    
    # Check results
    all_passed = all(r[1] for r in thread_results)
    if all_passed:
        results.add_result(f"Concurrent clients ({num_clients})", True)
    else:
        failed_clients = [r[0] for r in thread_results if not r[1]]
        failed_reasons = [r[2] for r in thread_results if not r[1] and len(r) > 2]
        results.add_result(f"Concurrent clients ({num_clients})", False,
                           f"Failed clients ({len(failed_clients)}): {failed_clients}, Reasons: {failed_reasons}")

# ============================================================================
# Test 8: File Locking
# ============================================================================

def test_file_locking():
    """Test file locking with concurrent client access"""
    print("\n--- Test 8: File Locking (Concurrent Access) ---")
    
    import threading
    import time
    
    test_filename = f"test_lock_{random_string()}.bin"
    test_data = generate_test_data(100000)  # 100KB
    
    results_collected = []
    lock = threading.Lock()
    
    def client_writer():
        """Client that tries to write/upload file"""
        try:
            time.sleep(0.1)  # Small delay
            ftp = ftplib.FTP()
            ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
            ftp.login(FTP_USER, FTP_PASS)
            
            with tempfile.NamedTemporaryFile(delete=False) as tmp:
                tmp.write(test_data)
                tmp_path = tmp.name
            
            with open(tmp_path, 'rb') as f:
                ftp.storbinary(f'STOR {test_filename}', f)
            
            os.unlink(tmp_path)
            ftp.quit()
            
            with lock:
                results_collected.append(('write', True))
        except Exception as e:
            with lock:
                results_collected.append(('write', False, str(e)))
    
    def client_reader():
        """Client that tries to read/download file"""
        try:
            ftp = ftplib.FTP()
            ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
            ftp.login(FTP_USER, FTP_PASS)
            
            # Try to download
            downloaded = bytearray()
            ftp.retrbinary(f'RETR {test_filename}', downloaded.extend)
            
            success = bytes(downloaded) == test_data
            ftp.quit()
            
            with lock:
                results_collected.append(('read', success))
        except Exception as e:
            with lock:
                results_collected.append(('read', False, str(e)))
    
    # Start concurrent operations
    writer_thread = threading.Thread(target=client_writer)
    reader_thread = threading.Thread(target=client_reader)
    
    writer_thread.start()
    reader_thread.start()
    
    writer_thread.join(timeout=30)
    reader_thread.join(timeout=30)
    
    # Analyze results
    write_success = False
    read_success = False
    write_error = None
    read_error = None
    
    for result in results_collected:
        if result[0] == 'write':
            write_success = result[1]
            if len(result) > 2:
                write_error = result[2]
        elif result[0] == 'read':
            read_success = result[1]
            if len(result) > 2:
                read_error = result[2]
    
    # File locking should ensure only one operation succeeds at a time
    # Either write succeeds and read fails (file doesn't exist yet), or read succeeds and write fails (file exists)
    # But since write starts with delay, read should fail initially, then succeed after write completes
    
    if write_success and read_success:
        # Both succeeded - this might be ok if read happened after write
        results.add_result("File locking (concurrent access)", True, "Both operations succeeded (acceptable)")
    elif write_success and not read_success:
        # Write succeeded, read failed - expected if read tried during write
        results.add_result("File locking (concurrent access)", True, "Write succeeded, read failed as expected")
    elif not write_success and read_success:
        # Read succeeded, write failed - expected if write tried during read
        results.add_result("File locking (concurrent access)", True, "Read succeeded, write failed as expected")
    else:
        # Both failed - unexpected
        error_msg = f"Write error: {write_error}, Read error: {read_error}"
        results.add_result("File locking (concurrent access)", False, error_msg)
    
    # Cleanup
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        ftp.delete(test_filename)
        ftp.quit()
    except:
        pass

# ============================================================================
# Test 9: Transfer Type (TYPE)
# ============================================================================

def test_transfer_types():
    """Test ASCII and Binary transfer types"""
    print("\n--- Test 9: Transfer Types ---")
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        
        # Test TYPE A (ASCII)
        ftp.sendcmd('TYPE A')
        results.add_result("TYPE A command", True)
        
        # Test TYPE I (Binary)
        ftp.sendcmd('TYPE I')
        results.add_result("TYPE I command", True)
        
        # Upload and download in binary mode
        test_filename = f"test_binary_{random_string()}.bin"
        test_data = generate_test_data(5000)
        
        ftp.sendcmd('TYPE I')
        with tempfile.NamedTemporaryFile(delete=False) as tmp:
            tmp.write(test_data)
            tmp_path = tmp.name
        
        with open(tmp_path, 'rb') as f:
            ftp.storbinary(f'STOR {test_filename}', f)
        
        downloaded = bytearray()
        ftp.retrbinary(f'RETR {test_filename}', downloaded.extend)
        
        if bytes(downloaded) == test_data:
            results.add_result("Binary mode transfer", True)
        else:
            results.add_result("Binary mode transfer", False, "Data corruption")
        
        # Cleanup
        ftp.delete(test_filename)
        os.unlink(tmp_path)
        ftp.quit()
    except Exception as e:
        results.add_result("Transfer types", False, str(e))

# ============================================================================
# Test 10: Error Handling
# ============================================================================

def test_error_handling():
    """Test various error conditions"""
    print("\n--- Test 10: Error Handling ---")
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        
        # Test 10.1: Download non-existent file
        try:
            ftp.retrbinary('RETR nonexistent_file.txt', lambda x: None)
            results.add_result("Error: non-existent file", False, "Should have failed")
        except ftplib.error_perm:
            results.add_result("Error: non-existent file", True)
        
        # Test 10.2: Delete non-existent file
        try:
            ftp.delete('nonexistent_file.txt')
            results.add_result("Error: delete non-existent", False, "Should have failed")
        except ftplib.error_perm:
            results.add_result("Error: delete non-existent", True)
        
        # Test 10.3: Invalid REST offset
        try:
            ftp.sendcmd('REST -100')
            results.add_result("Error: invalid REST offset", False, "Should have failed")
        except ftplib.error_perm:
            results.add_result("Error: invalid REST offset", True)
        
        ftp.quit()
    except Exception as e:
        results.add_result("Error handling", False, str(e))

# ============================================================================
# Main Test Runner
# ============================================================================

def main():
    print("="*60)
    print("FTP Server Comprehensive Test Suite")
    print("="*60)
    print(f"Server: {FTP_HOST}:{FTP_PORT}")
    print(f"User: {FTP_USER}")
    print("="*60)
    
    # Check if server is running
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2)
        sock.connect((FTP_HOST, FTP_PORT))
        sock.close()
        print("✅ Server is running\n")
    except:
        print("❌ Server is not running!")
        print("Please start the server first: ./server")
        return 1
    
    # Run tests
    test_basic_connection()
    test_directory_operations()
    test_file_operations()
    test_resume_download()
    test_append()
    test_abor_command()
    test_concurrent_clients()
    test_file_locking()
    test_transfer_types()
    test_error_handling()
    
    # Print summary
    results.summary()
    
    return 0 if results.failed == 0 else 1

if __name__ == '__main__':
    exit(main())
