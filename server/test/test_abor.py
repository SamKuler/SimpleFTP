#!/usr/bin/env python3
"""
ABOR Command Test Suite
Tests the ABOR implementation with various scenarios
"""

import ftplib
import socket
import time
import os
import tempfile
import sys
import io

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
            print("\n❌ Some tests failed")
            return 1
        else:
            print("\n✅ All tests passed")
            return 0

results = TestResults()

def create_large_file(size_mb=10):
    """Create a large test file"""
    tmp = tempfile.NamedTemporaryFile(delete=False, suffix='.bin')
    chunk_size = 1024 * 1024  # 1MB chunks
    for _ in range(size_mb):
        tmp.write(os.urandom(chunk_size))
    tmp.close()
    return tmp.name

def test_abor_retr():
    """Test ABOR during RETR (Download)"""
    print("\n" + "="*60)
    print("TEST 1: ABOR during RETR (Download)")
    print("="*60)
    
    try:
        # Create test file
        print("Creating 10MB test file...")
        test_file = create_large_file(10)
        filename = 'large_test_file.bin'
        
        # Upload
        print(f"Uploading {filename}...")
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=30)
        ftp.login(FTP_USER, FTP_PASS)
        
        with open(test_file, 'rb') as f:
            ftp.storbinary(f'STOR {filename}', f)
        print("Upload complete")
        
        # Start download and abort
        print(f"Starting download of {filename}...")
        received_bytes = 0
        
        def callback(data):
            nonlocal received_bytes
            received_bytes += len(data)
            if received_bytes > 2 * 1024 * 1024:  # After 2MB
                print(f"Downloaded {received_bytes} bytes, sending ABOR...")
                ftp.abort()
                ftp.voidresp()  # Clear response
                raise Exception("Abort requested")
        
        try:
            ftp.retrbinary(f'RETR {filename}', callback)
        except Exception as e:
            print(f"Download aborted: {e}")
        
        print(f"Total received before abort: {received_bytes} bytes")
        
        # Verify connection is still alive
        time.sleep(1)
        try:
            resp = ftp.sendcmd('NOOP')
            print(f"Server still responsive after ABOR: {resp}")
            results.add_result("ABOR during RETR", True)
        except Exception as e:
            results.add_result("ABOR during RETR", False, f"Server not responsive: {e}")
        
        # Cleanup
        ftp.delete(filename)
        ftp.quit()
        os.unlink(test_file)
        
        print("✓ Test 1 completed")
        
    except Exception as e:
        results.add_result("ABOR during RETR", False, str(e))
        import traceback
        traceback.print_exc()

def test_abor_stor():
    """Test ABOR during STOR (Upload)"""
    print("\n" + "="*60)
    print("TEST 2: ABOR during STOR (Upload)")
    print("="*60)
    
    try:
        # Create test file
        print("Creating 10MB test file...")
        test_file = create_large_file(10)
        filename = 'upload_test_file.bin'
        
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=30)
        ftp.login(FTP_USER, FTP_PASS)
        
        # Start upload and abort
        print(f"Starting upload of {filename}...")
        sent_bytes = 0
        
        # Open data connection
        sock = ftp.transfercmd(f'STOR {filename}')
        
        with open(test_file, 'rb') as f:
            while True:
                data = f.read(8192)
                if not data:
                    break
                sock.send(data)
                sent_bytes += len(data)
                
                if sent_bytes > 2 * 1024 * 1024:  # After 2MB
                    print(f"Uploaded {sent_bytes} bytes, sending ABOR...")
                    ftp.abort()
                    ftp.voidresp()  # Clear response
                    break
        
        sock.close()
        print(f"Total sent before abort: {sent_bytes} bytes")
        
        # Verify connection is still alive
        time.sleep(1)
        try:
            resp = ftp.sendcmd('NOOP')
            print(f"Server still responsive after ABOR: {resp}")
            results.add_result("ABOR during STOR", True)
        except Exception as e:
            results.add_result("ABOR during STOR", False, f"Server not responsive: {e}")
        
        # Cleanup
        try:
            ftp.delete(filename)
        except:
            pass  # May not exist if aborted early
        
        ftp.quit()
        os.unlink(test_file)
        
        print("✓ Test 2 completed")
        
    except Exception as e:
        results.add_result("ABOR during STOR", False, str(e))
        import traceback
        traceback.print_exc()

def test_abor_list():
    """Test ABOR during LIST"""
    print("\n" + "="*60)
    print("TEST 3: ABOR during LIST")
    print("="*60)
    
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=30)
        ftp.login(FTP_USER, FTP_PASS)
        
        # Start LIST
        print("Starting LIST command...")
        sock = ftp.transfercmd('LIST')
        
        # Receive some data
        data = sock.recv(1024)
        print(f"Received {len(data)} bytes, sending ABOR...")
        
        # Send ABOR
        ftp.abort()
        ftp.voidresp()  # Clear response
        sock.close()
        
        # Verify connection is still alive
        time.sleep(1)
        try:
            resp = ftp.sendcmd('NOOP')
            print(f"Server still responsive after ABOR: {resp}")
            results.add_result("ABOR during LIST", True)
        except Exception as e:
            results.add_result("ABOR during LIST", False, f"Server not responsive: {e}")
        
        ftp.quit()
        
        print("✓ Test 3 completed")
        
    except Exception as e:
        results.add_result("ABOR during LIST", False, str(e))
        import traceback
        traceback.print_exc()

def test_abor_no_transfer():
    """Test ABOR when no transfer is in progress"""
    print("\n" + "="*60)
    print("TEST 4: ABOR with No Transfer")
    print("="*60)
    
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=30)
        ftp.login(FTP_USER, FTP_PASS)
        
        # Send ABOR when nothing is happening
        print("Sending ABOR with no active transfer...")
        try:
            resp = ftp.sendcmd('ABOR')
            print(f"Response: {resp}")
            
            # Should get 225 (No transfer in progress)
            if resp.startswith('225'):
                results.add_result("ABOR with no transfer", True)
            else:
                results.add_result("ABOR with no transfer", False, f"Unexpected response: {resp}")
        except Exception as e:
            results.add_result("ABOR with no transfer", False, str(e))
        
        ftp.quit()
        
        print("✓ Test 4 completed")
        
    except Exception as e:
        results.add_result("ABOR with no transfer", False, str(e))
        import traceback
        traceback.print_exc()

def test_abor_post_operations():
    """Test that operations work correctly after ABOR"""
    print("\n" + "="*60)
    print("TEST 5: Post-ABOR Operations")
    print("="*60)
    
    try:
        # Create test file
        print("Creating test file for ABOR...")
        test_file = create_large_file(10)
        filename = 'test_abor_post.bin'
        
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=30)
        ftp.login(FTP_USER, FTP_PASS)
        
        # Upload file
        with open(test_file, 'rb') as f:
            ftp.storbinary(f'STOR {filename}', f)
        
        # Start download and abort
        received_bytes = 0
        def callback(data):
            nonlocal received_bytes
            received_bytes += len(data)
            if received_bytes > 1024 * 1024:  # After 1MB
                ftp.abort()
                ftp.voidresp()  # Clear response
                raise Exception("Abort")
        
        try:
            ftp.retrbinary(f'RETR {filename}', callback)
        except:
            pass
        
        time.sleep(1)
        
        # Test various operations after ABOR
        print("Testing operations after ABOR...")
        
        # Test LIST
        try:
            lines = []
            ftp.retrlines('LIST', lines.append)
            print(f"✓ LIST successful: {len(lines)} entries")
        except Exception as e:
            results.add_result("Post-ABOR LIST", False, str(e))
            ftp.quit()
            os.unlink(test_file)
            return
        
        # Test NLST
        try:
            names = ftp.nlst()
            print(f"✓ NLST successful: {len(names)} entries")
        except Exception as e:
            results.add_result("Post-ABOR NLST", False, str(e))
            ftp.quit()
            os.unlink(test_file)
            return
        
        # Test upload after ABOR
        post_name = "post_abor.bin"
        post_payload = b'Z' * (64 * 1024)
        try:
            ftp.storbinary(f'STOR {post_name}', io.BytesIO(post_payload))
            print("✓ Post-ABOR upload successful")
        except Exception as e:
            results.add_result("Post-ABOR upload", False, str(e))
            ftp.quit()
            os.unlink(test_file)
            return
        
        # Test download after ABOR
        try:
            received = bytearray()
            ftp.retrbinary(f'RETR {post_name}', received.extend)
            if received == post_payload:
                print("✓ Post-ABOR download successful and data matches")
                results.add_result("Post-ABOR operations", True)
            else:
                results.add_result("Post-ABOR operations", False, "Downloaded data mismatch")
        except Exception as e:
            results.add_result("Post-ABOR operations", False, str(e))
        
        # Cleanup
        ftp.delete(filename)
        ftp.delete(post_name)
        ftp.quit()
        os.unlink(test_file)
        
        print("✓ Test 5 completed")
        
    except Exception as e:
        results.add_result("Post-ABOR operations", False, str(e))
        import traceback
        traceback.print_exc()

def main():
    print("============================================================")
    print("FTP ABOR Command Test Suite")
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
        print("✗ Server is not running!")
        print("Please start the server first: ./server")
        return 1
    
    # Run tests
    test_abor_retr()
    time.sleep(1)
    
    test_abor_stor()
    time.sleep(1)
    
    test_abor_list()
    time.sleep(1)
    
    test_abor_no_transfer()
    time.sleep(1)
    
    test_abor_post_operations()
    
    return results.summary()

if __name__ == '__main__':
    sys.exit(main())
