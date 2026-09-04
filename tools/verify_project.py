#!/usr/bin/env python3
"""Compatibility entry point for the new verifier."""
from project import main
import sys
if __name__ == '__main__':
    sys.argv[1:1] = ['verify']
    sys.exit(main())
