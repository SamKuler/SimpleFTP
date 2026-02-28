#!/usr/bin/env python3
"""
Basic Functionality Test - Quick Smoke Test
Run this first to verify basic server functionality
"""

import ftplib
import socket
import sys

FTP_HOST = 'localhost'
FTP_PORT = 2121
FTP_USER = 'anonymous'
FTP_PASS = ''

def test_connection():
    """Test 1: Can we connect to the server?"""
    print("Test 1: Connection... ", end='')
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        sock.connect((FTP_HOST, FTP_PORT))
        sock.close()
        print("✅ PASS")
        return True
    except Exception as e:
        print(f"❌ FAIL: {e}")
        return False

def test_login():
    """Test 2: Can we login?"""
    print("Test 2: Login... ", end='')
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        ftp.quit()
        print("✅ PASS")
        return True
    except Exception as e:
        print(f"❌ FAIL: {e}")
        return False

def test_pwd():
    """Test 3: PWD command"""
    print("Test 3: PWD... ", end='')
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        pwd = ftp.pwd()
        ftp.quit()
        print(f"✅ PASS (dir: {pwd})")
        return True
    except Exception as e:
        print(f"❌ FAIL: {e}")
        return False

def test_list():
    """Test 4: LIST command (async)"""
    print("Test 4: LIST (async)... ", end='')
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        files = []
        ftp.retrlines('LIST', files.append)
        ftp.quit()
        print(f"✅ PASS ({len(files)} entries)")
        return True
    except Exception as e:
        print(f"❌ FAIL: {e}")
        return False

def test_upload_download():
    """Test 5: Upload and download a small file"""
    print("Test 5: Upload/Download... ", end='')
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        
        # Upload
        test_data = b"Hello, FTP Server!\n" * 100
        filename = "test_basic.txt"
        
        from io import BytesIO
        ftp.storbinary(f'STOR {filename}', BytesIO(test_data))
        
        # Download
        downloaded = bytearray()
        ftp.retrbinary(f'RETR {filename}', downloaded.extend)
        
        # Verify
        if bytes(downloaded) == test_data:
            ftp.delete(filename)
            ftp.quit()
            print("✅ PASS")
            return True
        else:
            ftp.delete(filename)
            ftp.quit()
            print("❌ FAIL: Data mismatch")
            return False
    except Exception as e:
        print(f"❌ FAIL: {e}")
        return False

def test_type_commands():
    """Test 6: TYPE A and TYPE I commands"""
    print("Test 6: TYPE commands... ", end='')
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        
        ftp.sendcmd('TYPE A')
        ftp.sendcmd('TYPE I')
        
        ftp.quit()
        print("✅ PASS")
        return True
    except Exception as e:
        print(f"❌ FAIL: {e}")
        return False

def test_noop():
    """Test 7: NOOP command"""
    print("Test 7: NOOP... ", end='')
    try:
        ftp = ftplib.FTP()
        ftp.connect(FTP_HOST, FTP_PORT, timeout=10)
        ftp.login(FTP_USER, FTP_PASS)
        
        resp = ftp.sendcmd('NOOP')
        
        ftp.quit()
        print(f"✅ PASS ({resp})")
        return True
    except Exception as e:
        print(f"❌ FAIL: {e}")
        return False

def main():
    print("="*60)
    print("FTP Server Basic Smoke Test")
    print("="*60)
    print(f"Server: {FTP_HOST}:{FTP_PORT}")
    print(f"User: {FTP_USER}")
    print("="*60)
    print()
    
    tests = [
        test_connection,
        test_login,
        test_pwd,
        test_list,
        test_upload_download,
        test_type_commands,
        test_noop,
    ]
    
    results = []
    for test in tests:
        results.append(test())
    
    print()
    print("="*60)
    passed = sum(results)
    total = len(results)
    
    if passed == total:
        print(f"✅ All tests passed ({passed}/{total})")
        print("="*60)
        print("\nServer is working correctly! ✨")
        print("You can now run comprehensive tests:")
        print("  python3 test_comprehensive.py")
        print("  python3 test_abor.py")
        return 0
    else:
        print(f"❌ Some tests failed ({passed}/{total} passed)")
        print("="*60)
        print("\nPlease check server configuration and logs.")
        return 1

if __name__ == '__main__':
    sys.exit(main())
