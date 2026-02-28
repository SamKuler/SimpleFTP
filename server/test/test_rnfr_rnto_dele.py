#!/usr/bin/env python3
"""
RNFR/RNTO/DELE Commands Test Suite
Tests file rename and delete operations with locking and permission checks
"""

import socket
import time
import sys

# Test configuration
FTP_HOST = '127.0.0.1'
FTP_PORT = 2121
FTP_USER = 'anonymous'
FTP_PASS = 'anonymous@'

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

def send_command(sock, command, expect_response=True):
    """Send a command and receive response"""
    print(f">>> {command}")
    sock.sendall((command + "\r\n").encode())
    
    if not expect_response:
        return ""
    
    time.sleep(0.1)
    response = ""
    sock.settimeout(2.0)
    try:
        while True:
            chunk = sock.recv(4096).decode()
            if not chunk:
                break
            response += chunk
            if response.endswith("\r\n"):
                break
    except socket.timeout:
        pass
    
    print(f"<<< {response.strip()}")
    return response

def create_test_file(sock, filename, content=b"Test file content\n"):
    """Helper to create a test file via STOR"""
    # 使用动态端口分配
    data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    data_sock.bind(("127.0.0.1", 0))  # 让系统自动分配可用端口
    port = data_sock.getsockname()[1]  # 获取分配的端口
    
    # 计算PORT命令参数
    ip_parts = "127,0,0,1".split(',')
    port_high = port // 256
    port_low = port % 256
    
    send_command(sock, f"PORT {','.join(ip_parts)},{port_high},{port_low}")
    
    data_sock.listen(1)
    
    send_command(sock, f"STOR {filename}")
    
    conn, _ = data_sock.accept()
    conn.sendall(content)
    conn.close()
    data_sock.close()
    
    time.sleep(0.2)
    response = sock.recv(1024).decode()
    print(f"<<< {response.strip()}")
    return "226" in response or "250" in response

def test_basic_operations():
    """Test basic RNFR, RNTO, and DELE operations"""
    print("\n" + "="*60)
    print("TEST 1: Basic RNFR/RNTO/DELE Operations")
    print("="*60)
    
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((FTP_HOST, FTP_PORT))
        
        welcome = sock.recv(1024).decode()
        print(f"<<< {welcome.strip()}")
        
        send_command(sock, f"USER {FTP_USER}")
        send_command(sock, f"PASS {FTP_PASS}")
        send_command(sock, "TYPE I")
        
        # Test 1.1: Create and rename a file
        print("\n--- Test 1.1: Create and rename file ---")
        if create_test_file(sock, "original.txt"):
            resp = send_command(sock, "RNFR original.txt")
            if "350" in resp:
                print("✓ RNFR accepted")
                resp = send_command(sock, "RNTO renamed.txt")
                if "250" in resp:
                    results.add_result("Basic rename operation", True)
                else:
                    results.add_result("Basic rename operation", False, "RNTO failed")
            else:
                results.add_result("Basic rename operation", False, "RNFR failed")
        else:
            results.add_result("Basic rename operation", False, "Failed to create test file")
        
        # Test 1.2: Delete the renamed file
        print("\n--- Test 1.2: Delete file ---")
        resp = send_command(sock, "DELE renamed.txt")
        if "250" in resp:
            results.add_result("Basic delete operation", True)
        else:
            results.add_result("Basic delete operation", False, "DELE failed")
        
        # Test 1.3: Verify file is gone
        print("\n--- Test 1.3: Verify file is deleted ---")
        resp = send_command(sock, "DELE renamed.txt")
        if "550" in resp:
            results.add_result("Delete non-existent file check", True)
        else:
            results.add_result("Delete non-existent file check", False, "Should report file not found")
        
        send_command(sock, "QUIT")
        sock.close()
        print("\n✓ Test 1 completed")
        
    except Exception as e:
        results.add_result("Basic operations test", False, str(e))
        import traceback
        traceback.print_exc()

def test_bad_sequences():
    """Test bad command sequences"""
    print("\n" + "="*60)
    print("TEST 2: Bad Command Sequences")
    print("="*60)
    
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((FTP_HOST, FTP_PORT))
        
        welcome = sock.recv(1024).decode()
        print(f"<<< {welcome.strip()}")
        
        send_command(sock, f"USER {FTP_USER}")
        send_command(sock, f"PASS {FTP_PASS}")
        send_command(sock, "TYPE I")
        
        # Test 2.1: RNTO without RNFR
        print("\n--- Test 2.1: RNTO without RNFR ---")
        resp = send_command(sock, "RNTO shouldfail.txt")
        if "503" in resp:
            results.add_result("RNTO without RNFR", True)
        else:
            results.add_result("RNTO without RNFR", False, "Should reject RNTO without RNFR")
        
        # Test 2.2: RNFR followed by non-RNTO command
        print("\n--- Test 2.2: RNFR interrupted by other command ---")
        create_test_file(sock, "test.txt")
        send_command(sock, "RNFR test.txt")
        resp = send_command(sock, "PWD")  # Interrupt with PWD
        resp = send_command(sock, "RNTO interrupted.txt")
        if "503" in resp:
            results.add_result("RNFR state management", True)
        else:
            results.add_result("RNFR state management", False, "RNFR state may not have been cleared properly")
        
        # Cleanup
        send_command(sock, "DELE test.txt")
        
        send_command(sock, "QUIT")
        sock.close()
        print("\n✓ Test 2 completed")
        
    except Exception as e:
        results.add_result("Bad sequences test", False, str(e))
        import traceback
        traceback.print_exc()

def test_nonexistent_files():
    """Test operations on non-existent files"""
    print("\n" + "="*60)
    print("TEST 3: Non-existent Files")
    print("="*60)
    
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((FTP_HOST, FTP_PORT))
        
        welcome = sock.recv(1024).decode()
        print(f"<<< {welcome.strip()}")
        
        send_command(sock, f"USER {FTP_USER}")
        send_command(sock, f"PASS {FTP_PASS}")
        send_command(sock, "TYPE I")
        
        # Test 3.1: RNFR on non-existent file
        print("\n--- Test 3.1: RNFR on non-existent file ---")
        resp = send_command(sock, "RNFR nonexistent.txt")
        if "550" in resp:
            results.add_result("RNFR on non-existent file", True)
        else:
            results.add_result("RNFR on non-existent file", False, "Should reject RNFR on non-existent file")
        
        # Test 3.2: DELE on non-existent file
        print("\n--- Test 3.2: DELE on non-existent file ---")
        resp = send_command(sock, "DELE nonexistent.txt")
        if "550" in resp:
            results.add_result("DELE on non-existent file", True)
        else:
            results.add_result("DELE on non-existent file", False, "Should reject DELE on non-existent file")
        
        # Test 3.3: RNTO to existing file
        print("\n--- Test 3.3: RNTO to existing file ---")
        create_test_file(sock, "source.txt")
        create_test_file(sock, "dest.txt")
        send_command(sock, "RNFR source.txt")
        resp = send_command(sock, "RNTO dest.txt")
        if "550" in resp:
            results.add_result("RNTO to existing file", True)
        else:
            results.add_result("RNTO to existing file", False, "Should reject when destination exists")
        
        # Cleanup
        send_command(sock, "DELE source.txt")
        send_command(sock, "DELE dest.txt")
        
        send_command(sock, "QUIT")
        sock.close()
        print("\n✓ Test 3 completed")
        
    except Exception as e:
        results.add_result("Non-existent files test", False, str(e))
        import traceback
        traceback.print_exc()

def test_directory_operations():
    """Test DELE on directories (should fail)"""
    print("\n" + "="*60)
    print("TEST 4: Directory Operations")
    print("="*60)
    
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((FTP_HOST, FTP_PORT))
        
        welcome = sock.recv(1024).decode()
        print(f"<<< {welcome.strip()}")
        
        send_command(sock, f"USER {FTP_USER}")
        send_command(sock, f"PASS {FTP_PASS}")
        send_command(sock, "TYPE I")
        
        # Test 4.1: Try to DELE a directory
        print("\n--- Test 4.1: DELE on directory ---")
        send_command(sock, "MKD testdir")
        resp = send_command(sock, "DELE testdir")
        if "550" in resp:
            results.add_result("DELE on directory", True)
        else:
            results.add_result("DELE on directory", False, "Should reject DELE on directory")
        
        # Cleanup
        send_command(sock, "RMD testdir")
        
        send_command(sock, "QUIT")
        sock.close()
        print("\n✓ Test 4 completed")
        
    except Exception as e:
        results.add_result("Directory operations test", False, str(e))
        import traceback
        traceback.print_exc()

def test_permission_checks():
    """Test rename to different directories"""
    print("\n" + "="*60)
    print("TEST 5: Permission and Path Checks")
    print("="*60)
    
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((FTP_HOST, FTP_PORT))
        
        welcome = sock.recv(1024).decode()
        print(f"<<< {welcome.strip()}")
        
        send_command(sock, f"USER {FTP_USER}")
        send_command(sock, f"PASS {FTP_PASS}")
        send_command(sock, "TYPE I")
        
        # Test 5.1: Rename to non-existent directory
        print("\n--- Test 5.1: RNTO to non-existent directory ---")
        create_test_file(sock, "moveme.txt")
        send_command(sock, "RNFR moveme.txt")
        resp = send_command(sock, "RNTO nonexistdir/file.txt")
        if "550" in resp:
            results.add_result("RNTO to non-existent directory", True)
        else:
            results.add_result("RNTO to non-existent directory", False, "May have allowed invalid destination path")
        
        # Cleanup
        send_command(sock, "DELE moveme.txt")
        
        # Test 5.2: Rename within same directory
        print("\n--- Test 5.2: Rename within same directory ---")
        create_test_file(sock, "file1.txt")
        send_command(sock, "RNFR file1.txt")
        resp = send_command(sock, "RNTO file2.txt")
        if "250" in resp:
            results.add_result("Rename within directory", True)
            send_command(sock, "DELE file2.txt")
        else:
            results.add_result("Rename within directory", False, "Rename failed")
            send_command(sock, "DELE file1.txt")
        
        send_command(sock, "QUIT")
        sock.close()
        print("\n✓ Test 5 completed")
        
    except Exception as e:
        results.add_result("Permission checks test", False, str(e))
        import traceback
        traceback.print_exc()

def main():
    print("============================================================")
    print("FTP RNFR/RNTO/DELE Test Suite")
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
        print("✅ Server is running\n")
    except:
        print("❌ Server is not running!")
        print("Please start the server first: ./server")
        return 1

    time.sleep(1)

    test_basic_operations()
    time.sleep(1)
    
    test_bad_sequences()
    time.sleep(1)
    
    test_nonexistent_files()
    time.sleep(1)
    
    test_directory_operations()
    time.sleep(1)
    
    test_permission_checks()
    
    return results.summary()

if __name__ == "__main__":
    sys.exit(main())
