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


def listing_groups(output):
    groups, rows = [], []
    for line in output.splitlines():
        if line.startswith("OK listed "):
            assert int(line.rsplit(" ", 1)[1]) == len(rows), output
            groups.append(rows)
            rows = []
        elif line.startswith('"'):
            rows.append(line)
    return groups


states = ("waiting", "ready", "running", "succeeded", "failed", "cancelled", "blocked", "retry_wait")
empty = subprocess.run([binary], input="".join(f"list {state}\n" for state in states) + "list\nquit\n",
                       text=True, capture_output=True, check=True)
assert listing_groups(empty.stdout) == [[]] * 9

commands = (
    'add ready-z -1 sum "4"\nadd ready-a 9 sum "5"\n'
    'add waiting 0 sum "1"\ndepend waiting ready-z\n'
    'add cancelled 0 sum "1"\ncancel cancelled\n'
    'list ready\nlist ReAdY\nlist failed\nlist waiting\nlist cancelled\nlist\nquit\n'
)
filtered = subprocess.run([binary], input=commands, text=True, capture_output=True, check=True)
groups = listing_groups(filtered.stdout)
ready = ['"ready-z" READY -1 0 ""', '"ready-a" READY 9 0 ""']
waiting = ['"waiting" WAITING 0 0 ""']
cancelled = ['"cancelled" CANCELLED 0 0 ""']
assert groups == [ready, ready, [], waiting, cancelled, ready + waiting + cancelled], filtered.stdout

invalid = subprocess.run(
    [binary],
    input='add keep 0 sum "1"\ncancel keep\nlist missing\nlist ready extra\nlist ""\nlist "unterminated\n'
          'list cancelled\nundo\nlist ready\nquit\n',
    text=True, capture_output=True,
)
assert invalid.returncode == 1 and invalid.stdout.count("ERROR ") == 4, invalid.stdout
assert "unknown task state; choose waiting, ready, running, succeeded, failed, cancelled, blocked or retry_wait" in invalid.stdout
assert listing_groups(invalid.stdout) == [['"keep" CANCELLED 0 0 ""'], ['"keep" READY 0 0 ""']], invalid.stdout

help_output = subprocess.run([binary], input="help\nquit\n", text=True, capture_output=True, check=True).stdout
assert "list [STATE]" in help_output and "retry_wait" in help_output
print("PASS CLI state filters, matching counts, input validation, and read-only undo behavior")

with tempfile.TemporaryDirectory(prefix="taskflow-priority-") as directory:
    snapshot = str(pathlib.Path(directory) / "priority.snapshot")
    commands = (
        'add first 0 sum "1"\nadd second 9 sum "2"\npolicy priority\npriority first 10\n'
        f'save "{snapshot}"\nundo\nshow first\nload "{snapshot}"\nnext\n'
        'priority second -1000\nnext\nquit\n'
    )
    updated = subprocess.run([binary], input=commands, text=True, capture_output=True, check=True)
    assert '"first" READY 0 0 ""' in updated.stdout, updated.stdout
    assert '"first" SUCCEEDED 10 1 "1"' in updated.stdout, updated.stdout
    assert '"second" SUCCEEDED -1000 1 "2"' in updated.stdout, updated.stdout

invalid = subprocess.run(
    [binary],
    input='add keep 0 sum "1"\npriority keep 6\npriority keep 6\npriority missing 3\n'
          'priority keep 1001\npriority keep -1001\npriority keep 1x\npriority keep 4 extra\n'
          'priority keep\nundo\nshow keep\nquit\n',
    text=True, capture_output=True,
)
assert invalid.returncode == 1 and invalid.stdout.count("ERROR ") == 6, invalid.stdout
assert '"keep" READY 0 0 ""' in invalid.stdout, invalid.stdout
assert "priority ID VALUE" in help_output
print("PASS CLI priority updates, snapshot persistence, undo, no-op history, and rejected inputs")

empty = subprocess.run([binary], input="analyze\nquit\n", text=True, capture_output=True, check=True)
assert "ANALYSIS tasks=0 dependency_min_ticks=0\n" in empty.stdout
assert "CRITICAL_PATH\n" in empty.stdout and "OK analyzed 0\n" in empty.stdout

with tempfile.TemporaryDirectory(prefix="taskflow-analysis-") as directory:
    before = pathlib.Path(directory) / "before.snapshot"
    after = pathlib.Path(directory) / "after.snapshot"
    commands = (
        'add prepare 0 sum "1"\nconfigure prepare 2 0\n'
        'add compile 0 sum "2"\nconfigure compile 5 0\ndepend compile prepare\n'
        'add test 0 sum "3"\nconfigure test 3 0\ndepend test compile\n'
        f'save "{before}"\nanalyze\nsave "{after}"\npriority test 9\nanalyze\nundo\nshow test\n'
        'run\nanalyze\nquit\n'
    )
    inspected = subprocess.run([binary], input=commands, text=True, capture_output=True, check=True)
    assert before.read_bytes() == after.read_bytes(), "Analysis changed the saved session"
    assert inspected.stdout.count("ANALYSIS tasks=3 dependency_min_ticks=10\n") == 3, inspected.stdout
    assert 'CRITICAL_PATH "prepare" "compile" "test"\n' in inspected.stdout
    assert 'TIMING "test" WAITING start=7 finish=10\n' in inspected.stdout
    assert 'UNFINISHED "test" "compile" WAITING\n' in inspected.stdout
    assert '"test" WAITING 0 0 ""\n' in inspected.stdout, "Analysis added an undo entry"
    assert 'TIMING "test" SUCCEEDED start=7 finish=10\n' in inspected.stdout
    assert inspected.stdout.count('UNFINISHED "test"') == 2, inspected.stdout
    assert "MODEL full_plan unlimited_workers no_failures no_retries\n" in inspected.stdout

invalid = subprocess.run([binary], input='analyze extra\nanalyze "unterminated\nanalyze\nquit\n',
                         text=True, capture_output=True)
assert invalid.returncode == 1 and invalid.stdout.count("ERROR ") == 2, invalid.stdout
assert "OK analyzed 0\n" in invalid.stdout
help_output = subprocess.run([binary], input="help\nquit\n", text=True, capture_output=True, check=True).stdout
assert "analyze (full-plan timings, critical path, and unfinished prerequisites)" in help_output
print("PASS CLI dependency analysis, critical paths, model assumptions, unchanged snapshots, and undo")
