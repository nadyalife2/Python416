#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
echo "=== INSTALLING PYTHON PACKAGES ==="
pip install flask requests --break-system-packages
python3 -c "import flask, sqlite3; print('FLASK VERSION:', flask.__version__); print('SQLITE READY')"
