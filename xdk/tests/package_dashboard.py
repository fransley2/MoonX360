"""Compatibility entrypoint for the current clean MoonX360 dashboard package."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).resolve().parents[2]/'tools/package_moonx360.py'),run_name='__main__')
