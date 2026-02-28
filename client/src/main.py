"""
Main entry point for FTP client application
Supports CLI interactive, CLI batch (-c), and GUI (-g)
"""

import sys
import os
import argparse

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from ftp.ui.cli import CLIInterface
from ftp.ui.gui import GUIInterface


def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(description='FTP Client')
    
    # Mode options
    parser.add_argument('-g', '--gui', action='store_true', help='Run GUI mode')
    parser.add_argument('-c', '--command', action='append', help='Add command for batch mode (can repeat)')
    
    # Connection parameters
    parser.add_argument('-ip', dest='ip', help='FTP server host (alternative to positional host)')
    parser.add_argument('host', nargs='?', help='FTP server host (required for batch mode if -ip not given)')
    parser.add_argument('-p', '--port', '-port', type=int, default=21, help='FTP server port')
    
    # Authentication parameters
    parser.add_argument('-u', '--user', default='anonymous', help='Username')
    parser.add_argument('-P', '--password', default='anonymous', help='Password')
    
    # Batch file operations
    parser.add_argument('--get', help='Download file in batch mode')
    parser.add_argument('--put', help='Upload file in batch mode')
    
    # Other options [for non-autograde]
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose CLI output')
    
    return parser.parse_args()


def run_gui_mode(args):
    """Run GUI mode"""
    host = args.ip or args.host
    gui = GUIInterface(host=host, port=args.port, user=args.user, password=args.password)
    gui.run()


def run_batch_mode(args):
    """Run batch mode"""
    host = args.ip or args.host
    if not host:
        print('Batch mode (-c) requires host argument via -ip or positional host')
        return
    
    # Create CLI interface and connect
    cli = CLIInterface(verbose=args.verbose)
    cli.cmd_connect([host, str(args.port)])
    if not cli.connected:
        return
    
    # Login
    cli.cmd_login([args.user, args.password])
    
    # Execute batch commands
    for cmd_line in args.command:
        tokens = cmd_line.split()
        if not tokens:
            continue
        
        cmd_name = tokens[0].lower()
        cmd_args = tokens[1:]
        
        if cmd_name in cli.commands:
            cli.commands[cmd_name](cmd_args)
        else:
            cli.cmd_raw(tokens)
    
    # Execute file transfer commands
    if args.get:
        cli.cmd_get([args.get])
    if args.put:
        cli.cmd_put([args.put])
    
    # Wait and quit
    cli.cmd_wait([])
    cli.cmd_quit([])


def run_interactive_mode(args):
    """Run interactive mode"""
    cli = CLIInterface(verbose=args.verbose)
    
    # If host parameter provided, auto-connect
    host = args.ip or args.host
    if host:
        cli.cmd_connect([host, str(args.port)])
        # Disable auto login if not in verbose mode
        if cli.connected and args.user and args.password and args.verbose:
            cli.cmd_login([args.user, args.password])
    
    # Enter interactive loop
    cli.run_interactive()


def main():
    """Main function: select run mode based on command line arguments"""
    args = parse_arguments()
    
    # Select run mode based on arguments
    if args.gui:
        run_gui_mode(args)
    elif args.command:
        run_batch_mode(args)
    else:
        run_interactive_mode(args)


if __name__ == '__main__':
    main()
