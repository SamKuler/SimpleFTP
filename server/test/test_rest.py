#!/usr/bin/env python3
"""
REST Command Test Suite
Tests the FTP transfer resumption (breakpoint resume) implementation using REST.
"""

import ftplib
import socket
import time
import os
import tempfile
import sys
import io

# --- Configuration ---
FTP_HOST = 'localhost'
FTP_PORT = 2121
FTP_USER = 'anonymous'
FTP_PASS = ''
BREAK_SIZE = 2 * 1024 * 1024  # Interrupt after 2MB

# --- Utility Classes and Functions (Copied from Template) ---

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
            print("\n❌ Some tests failed")
            return 1
        else:
            print("\n✅ All tests passed")
            return 0

results = TestResults()

def create_large_file(size_mb=5):
    """Create a large test file (default 5MB)"""
    tmp = tempfile.NamedTemporaryFile(delete=False, suffix='.bin')
    chunk_size = 1024 * 1024  # 1MB chunks
    for _ in range(size_mb):
        tmp.write(os.urandom(chunk_size))
    tmp.close()
    return tmp.name, size_mb * chunk_size

def get_remote_file_size(ftp_instance, filename):
    """Get the size of a file on the FTP server."""
    try:
        return ftp_instance.size(filename)
    except Exception:
        return 0

# --- Test Functions for Resumption (REST) ---


def test_resume_download():
    """Test REST during RETR (Download Resumption)"""
    test_name = "Resume Download (RETR + REST)"
    print("\n" + "="*60)
    print(f"TEST 1: {test_name}")
    print("="*60)
    
    local_temp_file = None
    remote_filename = 'resume_download_test.bin'
    partial_local_file = None

    try:
        # 1. Setup: Create test file and upload it
        print("Creating 5MB test file...")
        local_temp_file, original_size = create_large_file(5)
        
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=30)
        ftp.login(FTP_USER, FTP_PASS)
        
        print(f"Uploading {remote_filename} (Size: {original_size} bytes)...")
        with open(local_temp_file, 'rb') as f:
            ftp.storbinary(f'STOR {remote_filename}', f)
        print("Upload complete")
        
        # 2. Simulate Interruption (Partial Download)
        print(f"Starting partial download, interrupting after {BREAK_SIZE} bytes...")
        
        # Create a temp file to save partial data
        with tempfile.NamedTemporaryFile(delete=False, suffix='.part') as f:
            partial_local_file = f.name
        
        received_bytes = 0
        
        def partial_callback(data):
            nonlocal received_bytes
            with open(partial_local_file, 'ab') as f_part:
                f_part.write(data)
            
            received_bytes += len(data)
            if received_bytes >= BREAK_SIZE:
                ftp.abort() # Use ABOR to stop the transfer cleanly
                ftp.voidresp()  # Clear response
                raise Exception("Interruption Point Reached")

        try:
            ftp.retrbinary(f'RETR {remote_filename}', partial_callback)
        except Exception as e:
            if "Interruption Point Reached" in str(e) or "transfer aborted" in str(e) or '426' in str(e):
                print(f"Download interrupted successfully. Received: {received_bytes} bytes.")
            else:
                # Re-raise other unexpected errors
                raise
        
        # 3. Resume Transfer
        partial_size = os.path.getsize(partial_local_file)
        if partial_size < original_size:
            print(f"Resuming download from offset: {partial_size} bytes...")
            
            with open(partial_local_file, 'ab') as f_resume:
                ftp.retrbinary(f'RETR {remote_filename}', f_resume.write, rest=partial_size)
            
            final_size = os.path.getsize(partial_local_file)
            
            # 4. Verification
            if final_size == original_size:
                results.add_result(test_name, True, f"Final size matches original: {final_size} bytes")
            else:
                results.add_result(test_name, False, f"Size mismatch. Expected: {original_size}, Found: {final_size}")
        else:
            results.add_result(test_name, False, "Transfer did not interrupt successfully or size mismatch.")
        
    except Exception as e:
        results.add_result(test_name, False, str(e))
        import traceback
        traceback.print_exc()

    finally:
        # 5. Cleanup
        try:
            if 'ftp' in locals():
                ftp.delete(remote_filename)
                ftp.quit()
        except:
            pass
        if local_temp_file and os.path.exists(local_temp_file):
            os.unlink(local_temp_file)
        if partial_local_file and os.path.exists(partial_local_file):
            os.unlink(partial_local_file)


def test_resume_upload():
    """Test REST during STOR (Upload Resumption)"""
    test_name = "Resume Upload (STOR + REST)"
    print("\n" + "="*60)
    print(f"TEST 2: {test_name}")
    print("="*60)
    
    local_temp_file = None
    remote_filename = 'resume_upload_test.bin'

    try:
        # 1. Setup: Create test file
        print("Creating 5MB test file...")
        local_temp_file, original_size = create_large_file(5)
        
        ftp = ftplib.FTP()
        ftp.debugging = 9
        ftp.connect(FTP_HOST, FTP_PORT, timeout=30)
        ftp.login(FTP_USER, FTP_PASS)
        ftp.sendcmd('TYPE I')  # Set binary mode
        
        # 2. Simulate Interruption (Partial Upload)
        print(f"Starting partial upload, interrupting after {BREAK_SIZE} bytes...")
        
        sent_bytes = 0
        
        # Manually manage the transfer to simulate the break/abort
        try:
            sock = ftp.transfercmd(f'STOR {remote_filename}')
            with open(local_temp_file, 'rb') as f:
                while True:
                    data = f.read(8192)
                    if not data:
                        break
                    sock.send(data)
                    sent_bytes += len(data)
                    
                    if sent_bytes >= BREAK_SIZE:
                        print(f"Uploaded {sent_bytes} bytes, sending ABOR...")
                        sock.shutdown(socket.SHUT_WR)  # Ensure data is sent
                        time.sleep(0.05)  # Allow partial data to be sent
                        ftp.abort()
                        ftp.voidresp()  # Clear response
                        print("ABOR sent.")
                        break
            print("Closing data connection...")
            sock.close()
            time.sleep(1)  # Wait for server to process ABOR
            
        except Exception as e:
            if "transfer aborted" in str(e) or '426' in str(e) or '502' in str(e):
                 print(f"Partial upload terminated as expected: {e}")
            else:
                raise
        
        # try:
        #     ftp.quit()
        # except Exception:
        #     pass
        ftp.quit()
        
        # 3. Resume Transfer
        print("Resuming upload...")
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=30)
        ftp.login(FTP_USER, FTP_PASS)
        
        partial_size = get_remote_file_size(ftp, remote_filename)
        print(f"Remote file partial size: {partial_size} bytes")

        if partial_size > 0 and partial_size < original_size:
            print(f"Resuming upload from offset: {partial_size} bytes...")
            
            with open(local_temp_file, 'rb') as f_resume:
                f_resume.seek(partial_size) 
                ftp.storbinary(f'STOR {remote_filename}', f_resume, rest=partial_size)
            
            final_size = get_remote_file_size(ftp, remote_filename)
            
            # 4. Verification
            if final_size == original_size:
                results.add_result(test_name, True, f"Final size matches original: {final_size} bytes")
            else:
                results.add_result(test_name, False, f"Size mismatch. Expected: {original_size}, Found: {final_size}")
        else:
            results.add_result(test_name, False, f"Transfer did not interrupt or remote size incorrect. Size: {partial_size}")

    except Exception as e:
        results.add_result(test_name, False, str(e))
        import traceback
        traceback.print_exc()

    finally:
        # 5. Cleanup
        try:
            if 'ftp' in locals():
                ftp.delete(remote_filename)
                ftp.quit()
        except:
            pass
        if local_temp_file and os.path.exists(local_temp_file):
            os.unlink(local_temp_file)

def main():
    print("============================================================")
    print("FTP REST Command (Breakpoint Resume) Test Suite")
    print("============================================================")
    print(f"Server: {FTP_HOST}:{FTP_PORT}")
    print(f"User: {FTP_USER}")
    print("============================================================")
    
    # Check if server is running
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2)
        sock.connect((FTP_HOST, FTP_PORT))
        sock.close()
        print("✓ Server is running\n")
    except:
        print("✗ Server is not running or not accessible!")
        return 1
    
    # Run tests
    test_resume_download()
    time.sleep(1)
    
    test_resume_upload()
    time.sleep(1)
    
    return results.summary()

if __name__ == '__main__':
    sys.exit(main())