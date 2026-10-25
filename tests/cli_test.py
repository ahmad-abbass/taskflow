import pathlib
import subprocess
import sys
import tempfile

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

with tempfile.TemporaryDirectory(prefix="taskflow-cli-") as directory:
    snapshot = str(pathlib.Path(directory) / "session.snapshot")
    commands = f'add beta 1 sum "4"\nadd alpha 9 count "three small words"\npolicy priority\nworkers 2\nconfigure alpha 2 0\nsave "{snapshot}"\nstep\nundo\nshow alpha\nrun\nload "{snapshot}"\nshow alpha\nordered\nrange alpha beta\nrank beta\nselect 0\nmetrics\nquit\n'
    session = subprocess.run([binary], input=commands, text=True, capture_output=True, check=True)
    assert session.stdout.count('"alpha" READY 9 0 ""') >= 3
    assert "OK undo" in session.stdout and "OK load" in session.stdout
    assert "RANK 1" in session.stdout and "OK range 2" in session.stdout
    assert "ready=2 running=0" in session.stdout
    invalid = subprocess.run([binary], input='workers -1\nconfigure alpha 1x 2\nselect -1\nundo\nquit\n', text=True, capture_output=True)
    assert invalid.returncode == 1 and invalid.stdout.count("ERROR") == 4
print("PASS CLI snapshots, undo, ordered queries, metrics, and bounded inputs")
