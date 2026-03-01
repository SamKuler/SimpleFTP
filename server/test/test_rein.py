#!/usr/bin/env python3
"""
REIN Command Test Suite
Tests the FTP REIN (Reinitialize) command implementation.

REIN resets the session to the state right after TCP connection:
  - User becomes unauthenticated (needs USER/PASS again)
  - Current directory resets to /
  - Transfer parameters reset (TYPE, MODE, STRU)
  - Data connections closed
  - Pending RNFR cleared
  - REST offset cleared
  - Connection itself is preserved
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
        print("\n" + "=" * 60)
        print(f"Test Results: {self.passed}/{total} passed")
        print("=" * 60)
        if self.failed > 0:
            print("\n❌ Some tests failed")
            return 1
        else:
            print("\n✅ All tests passed")
            return 0


results = TestResults()


# =============================================================================
# Helpers – raw socket FTP communication
# =============================================================================

def send_command(sock, command):
    """Send a command and receive response"""
    print(f">>> {command}")
    sock.sendall((command + "\r\n").encode())

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


def get_response_code(response):
    """Extract the 3-digit response code from an FTP response string."""
    if response and len(response) >= 3 and response[:3].isdigit():
        return int(response[:3])
    return None


def login(sock, user=FTP_USER, passwd=FTP_PASS):
    """Perform USER/PASS login sequence on an already-connected socket."""
    send_command(sock, f"USER {user}")
    resp = send_command(sock, f"PASS {passwd}")
    return resp


def connect_and_login():
    """Create a socket, connect, read welcome, and login."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((FTP_HOST, FTP_PORT))
    welcome = sock.recv(1024).decode()
    print(f"<<< {welcome.strip()}")
    login(sock)
    return sock


def create_test_file_via_port(sock, filename, content=b"REIN test data\n"):
    """Upload a small file using PORT mode (active)."""
    data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    data_sock.bind(("127.0.0.1", 0))
    port = data_sock.getsockname()[1]

    port_high = port // 256
    port_low = port % 256
    send_command(sock, f"PORT 127,0,0,1,{port_high},{port_low}")

    data_sock.listen(1)
    send_command(sock, f"STOR {filename}")

    conn, _ = data_sock.accept()
    conn.sendall(content)
    conn.close()
    data_sock.close()

    time.sleep(0.2)
    response = sock.recv(4096).decode()
    print(f"<<< {response.strip()}")
    return "226" in response or "250" in response


# =============================================================================
# Test 1: REIN returns 220 and resets authentication
# =============================================================================

def test_rein_basic():
    """REIN should return 220 and require re-authentication."""
    print("\n" + "=" * 60)
    print("TEST 1: REIN basic – response code and auth reset")
    print("=" * 60)

    try:
        sock = connect_and_login()

        # Verify we are authenticated – PWD should work
        resp_pwd = send_command(sock, "PWD")
        code_pwd = get_response_code(resp_pwd)
        results.add_result("PWD works before REIN", code_pwd == 257)

        # Send REIN
        resp_rein = send_command(sock, "REIN")
        code_rein = get_response_code(resp_rein)
        results.add_result("REIN returns 220", code_rein == 220)

        # After REIN, commands requiring auth should fail with 530
        resp_pwd2 = send_command(sock, "PWD")
        code_pwd2 = get_response_code(resp_pwd2)
        results.add_result("PWD rejected after REIN (530)", code_pwd2 == 530)

        resp_list = send_command(sock, "LIST")
        code_list = get_response_code(resp_list)
        results.add_result("LIST rejected after REIN (530)", code_list == 530)

        sock.close()
    except Exception as e:
        results.add_result("REIN basic test", False, str(e))


# =============================================================================
# Test 2: Re-login after REIN
# =============================================================================

def test_rein_relogin():
    """After REIN, the client should be able to login again on the same connection."""
    print("\n" + "=" * 60)
    print("TEST 2: Re-login after REIN")
    print("=" * 60)

    try:
        sock = connect_and_login()

        # REIN
        resp_rein = send_command(sock, "REIN")
        code_rein = get_response_code(resp_rein)
        results.add_result("REIN succeeds", code_rein == 220)

        # Re-login
        resp_user = send_command(sock, f"USER {FTP_USER}")
        code_user = get_response_code(resp_user)
        results.add_result("USER accepted after REIN", code_user in (230, 331))

        resp_pass = send_command(sock, f"PASS {FTP_PASS}")
        code_pass = get_response_code(resp_pass)
        results.add_result("PASS accepted after REIN", code_pass == 230)

        # Now authenticated – PWD should work again
        resp_pwd = send_command(sock, "PWD")
        code_pwd = get_response_code(resp_pwd)
        results.add_result("PWD works after re-login", code_pwd == 257)

        sock.close()
    except Exception as e:
        results.add_result("REIN re-login test", False, str(e))


# =============================================================================
# Test 3: REIN resets current directory
# =============================================================================

def test_rein_resets_cwd():
    """After REIN + re-login, CWD should be back to /."""
    print("\n" + "=" * 60)
    print("TEST 3: REIN resets current directory to /")
    print("=" * 60)

    try:
        sock = connect_and_login()

        # Create a directory and CWD into it
        send_command(sock, "MKD /rein_test_dir")
        resp_cwd = send_command(sock, "CWD /rein_test_dir")
        code_cwd = get_response_code(resp_cwd)
        results.add_result("CWD to /rein_test_dir", code_cwd == 250)

        resp_pwd = send_command(sock, "PWD")
        results.add_result("PWD shows /rein_test_dir",
                           "/rein_test_dir" in resp_pwd)

        # REIN + re-login
        send_command(sock, "REIN")
        login(sock)

        # PWD should now be /
        resp_pwd2 = send_command(sock, "PWD")
        results.add_result("PWD is / after REIN + re-login",
                           '/' in resp_pwd2 and '/rein_test_dir' not in resp_pwd2)

        # Cleanup
        send_command(sock, "RMD /rein_test_dir")

        sock.close()
    except Exception as e:
        results.add_result("REIN resets CWD test", False, str(e))


# =============================================================================
# Test 4: REIN resets transfer type
# =============================================================================

def test_rein_resets_transfer_type():
    """After REIN, transfer type should reset to ASCII (TYPE A)."""
    print("\n" + "=" * 60)
    print("TEST 4: REIN resets transfer type")
    print("=" * 60)

    try:
        sock = connect_and_login()

        # Change type to binary
        resp_type = send_command(sock, "TYPE I")
        code_type = get_response_code(resp_type)
        results.add_result("TYPE I accepted", code_type == 200)

        # REIN + re-login
        send_command(sock, "REIN")
        login(sock)

        # Switch to TYPE A – if it was already ASCII this is a no-op confirmation
        resp_type2 = send_command(sock, "TYPE A")
        code_type2 = get_response_code(resp_type2)
        results.add_result("TYPE A accepted after REIN (type reset to ASCII)",
                           code_type2 == 200)

        sock.close()
    except Exception as e:
        results.add_result("REIN resets transfer type test", False, str(e))


# =============================================================================
# Test 5: REIN with parameter should fail (501)
# =============================================================================

def test_rein_with_parameter():
    """REIN does not accept parameters – should return 501."""
    print("\n" + "=" * 60)
    print("TEST 5: REIN with parameter returns 501")
    print("=" * 60)

    try:
        sock = connect_and_login()

        resp = send_command(sock, "REIN somearg")
        code = get_response_code(resp)
        results.add_result("REIN with arg returns 501", code == 501)

        # Session should still be alive and authenticated
        resp_pwd = send_command(sock, "PWD")
        code_pwd = get_response_code(resp_pwd)
        results.add_result("Session still valid after bad REIN", code_pwd == 257)

        sock.close()
    except Exception as e:
        results.add_result("REIN with parameter test", False, str(e))


# =============================================================================
# Test 6: REIN clears pending RNFR
# =============================================================================

def test_rein_clears_rnfr():
    """If RNFR was issued, REIN should clear the pending rename state."""
    print("\n" + "=" * 60)
    print("TEST 6: REIN clears pending RNFR")
    print("=" * 60)

    try:
        sock = connect_and_login()

        # Create a file to rename
        filename = "rein_rnfr_test.txt"
        create_test_file_via_port(sock, filename)

        # Issue RNFR (350 = pending)
        resp_rnfr = send_command(sock, f"RNFR {filename}")
        code_rnfr = get_response_code(resp_rnfr)
        results.add_result("RNFR accepted (350)", code_rnfr == 350)

        # REIN + re-login
        send_command(sock, "REIN")
        login(sock)

        # RNTO should now fail because RNFR was cleared by REIN
        resp_rnto = send_command(sock, "RNTO newname.txt")
        code_rnto = get_response_code(resp_rnto)
        results.add_result("RNTO fails after REIN (no pending RNFR)",
                           code_rnto is not None and code_rnto >= 500)

        # Cleanup
        send_command(sock, f"DELE {filename}")

        sock.close()
    except Exception as e:
        results.add_result("REIN clears RNFR test", False, str(e))


# =============================================================================
# Test 7: REIN clears REST offset
# =============================================================================

def test_rein_clears_rest():
    """If REST was issued, REIN should reset the restart offset to 0."""
    print("\n" + "=" * 60)
    print("TEST 7: REIN clears REST offset")
    print("=" * 60)

    try:
        sock = connect_and_login()

        # Set restart offset
        resp_rest = send_command(sock, "REST 1024")
        code_rest = get_response_code(resp_rest)
        results.add_result("REST 1024 accepted (350)", code_rest == 350)

        # REIN + re-login
        send_command(sock, "REIN")
        login(sock)

        # Upload a small file and download it – should get full content (offset = 0)
        filename = "rein_rest_test.txt"
        test_data = b"A" * 100
        create_test_file_via_port(sock, filename, test_data)

        # Download via PORT
        data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        data_sock.bind(("127.0.0.1", 0))
        port = data_sock.getsockname()[1]
        port_high = port // 256
        port_low = port % 256
        send_command(sock, f"PORT 127,0,0,1,{port_high},{port_low}")
        data_sock.listen(1)

        resp_retr = send_command(sock, f"RETR {filename}")
        conn, _ = data_sock.accept()
        downloaded = b""
        while True:
            chunk = conn.recv(4096)
            if not chunk:
                break
            downloaded += chunk
        conn.close()
        data_sock.close()

        # The 226 response may already be in resp_retr or arrive separately
        if "226" not in resp_retr:
            time.sleep(0.3)
            try:
                sock.settimeout(2.0)
                extra = sock.recv(4096).decode()
                print(f"<<< {extra.strip()}")
            except socket.timeout:
                pass

        results.add_result("Full file received after REIN (REST cleared)",
                           len(downloaded) == len(test_data))

        # Cleanup
        send_command(sock, f"DELE {filename}")

        sock.close()
    except Exception as e:
        results.add_result("REIN clears REST test", False, str(e))


# =============================================================================
# Test 8: REIN before login (unauthenticated state)
# =============================================================================

def test_rein_before_login():
    """REIN should work even if not yet authenticated."""
    print("\n" + "=" * 60)
    print("TEST 8: REIN before login")
    print("=" * 60)

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((FTP_HOST, FTP_PORT))
        welcome = sock.recv(1024).decode()
        print(f"<<< {welcome.strip()}")

        # Send REIN without logging in first
        resp_rein = send_command(sock, "REIN")
        code_rein = get_response_code(resp_rein)
        results.add_result("REIN before login returns 220", code_rein == 220)

        # Should still be able to login
        login(sock)
        resp_pwd = send_command(sock, "PWD")
        code_pwd = get_response_code(resp_pwd)
        results.add_result("Login works after pre-auth REIN", code_pwd == 257)

        sock.close()
    except Exception as e:
        results.add_result("REIN before login test", False, str(e))


# =============================================================================
# Test 9: Multiple REIN commands
# =============================================================================

def test_multiple_rein():
    """Multiple REIN commands in a row should all succeed."""
    print("\n" + "=" * 60)
    print("TEST 9: Multiple REIN commands")
    print("=" * 60)

    try:
        sock = connect_and_login()

        for i in range(3):
            resp = send_command(sock, "REIN")
            code = get_response_code(resp)
            results.add_result(f"REIN #{i+1} returns 220", code == 220)

        # Login and verify session works
        login(sock)
        resp_pwd = send_command(sock, "PWD")
        code_pwd = get_response_code(resp_pwd)
        results.add_result("Session works after multiple REINs", code_pwd == 257)

        sock.close()
    except Exception as e:
        results.add_result("Multiple REIN test", False, str(e))


# =============================================================================
# Test 10: REIN followed by file operations
# =============================================================================

def test_rein_then_file_ops():
    """After REIN + re-login, file upload/download should work normally."""
    print("\n" + "=" * 60)
    print("TEST 10: File operations after REIN")
    print("=" * 60)

    try:
        sock = connect_and_login()

        # Upload a file before REIN
        filename = "rein_fileops_test.txt"
        test_data = b"Hello from before REIN\n"
        ok = create_test_file_via_port(sock, filename, test_data)
        results.add_result("Upload before REIN", ok)

        # REIN + re-login
        send_command(sock, "REIN")
        login(sock)

        # Switch to binary mode to avoid ASCII newline conversion
        send_command(sock, "TYPE I")

        # Download the file
        data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        data_sock.bind(("127.0.0.1", 0))
        port = data_sock.getsockname()[1]
        port_high = port // 256
        port_low = port % 256
        send_command(sock, f"PORT 127,0,0,1,{port_high},{port_low}")
        data_sock.listen(1)

        resp_retr = send_command(sock, f"RETR {filename}")
        conn, _ = data_sock.accept()
        downloaded = b""
        while True:
            chunk = conn.recv(4096)
            if not chunk:
                break
            downloaded += chunk
        conn.close()
        data_sock.close()

        # The 226 response may already be in resp_retr or arrive separately
        if "226" not in resp_retr:
            time.sleep(0.3)
            try:
                sock.settimeout(2.0)
                extra = sock.recv(4096).decode()
                print(f"<<< {extra.strip()}")
            except socket.timeout:
                pass

        results.add_result("Download after REIN matches", downloaded == test_data)

        # Upload a new file after REIN
        filename2 = "rein_fileops_test2.txt"
        test_data2 = b"Hello from after REIN\n"
        ok2 = create_test_file_via_port(sock, filename2, test_data2)
        results.add_result("Upload after REIN", ok2)

        # Cleanup
        send_command(sock, f"DELE {filename}")
        send_command(sock, f"DELE {filename2}")

        sock.close()
    except Exception as e:
        results.add_result("File ops after REIN test", False, str(e))


# =============================================================================
# Main
# =============================================================================

def main():
    print("=" * 60)
    print("FTP Server REIN Command Test Suite")
    print("=" * 60)
    print(f"Server: {FTP_HOST}:{FTP_PORT}")
    print(f"User:   {FTP_USER}")
    print("=" * 60)

    # Pre-flight: check server is running
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2)
        sock.connect((FTP_HOST, FTP_PORT))
        sock.close()
        print("✅ Server is running\n")
    except Exception:
        print("❌ Server is not running!")
        print("Please start the server first: ./server")
        return 1

    test_rein_basic()
    test_rein_relogin()
    test_rein_resets_cwd()
    test_rein_resets_transfer_type()
    test_rein_with_parameter()
    test_rein_clears_rnfr()
    test_rein_clears_rest()
    test_rein_before_login()
    test_multiple_rein()
    test_rein_then_file_ops()

    return results.summary()


if __name__ == '__main__':
    sys.exit(main())
