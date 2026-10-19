import pathlib
import subprocess
import sys

binary = str(pathlib.Path(sys.argv[1]).resolve())
good = subprocess.run([binary], input='add first 2 sum "1 2 3"\nadd second 9 count "three small words"\nnext\nrun\nlist\nquit\n', text=True, capture_output=True, check=True)
assert '"first" SUCCEEDED 2 1 "6"' in good.stdout
assert '"second" SUCCEEDED 9 1 "3"' in good.stdout
assert "OK listed 2" in good.stdout
bad = subprocess.run([binary], input='add x 1 sum "2x"\nadd y 4 count "still works"\nrun\nshow y\n', text=True, capture_output=True)
assert bad.returncode == 1
assert "ERROR" in bad.stdout
assert '"y" SUCCEEDED 4 1 "2"' in bad.stdout
assert subprocess.run([binary], input='add x 1 sum "unterminated\n', text=True, capture_output=True).returncode == 1
print("PASS CLI workflow, invalid command recovery, and malformed quoting")
