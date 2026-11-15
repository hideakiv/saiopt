"""Run the code examples in the guides and check their printed output.

Each page's examples are one continuous program, run top to bottom:

* Python: every ``.. code-block:: python`` on a page runs in order in one
  namespace.
* C++: every ``.. code-block:: cpp`` on a page goes, in order, into the
  body of one ``main()``, which is then compiled and run.

A ``.. code-block:: text`` is the expected output of the code block before
it (prose in between is fine). That block's stdout must match it exactly,
apart from trailing whitespace. Code blocks with no output block after
them still have to run without error.

A hidden comment right before a block changes how it's treated:

``.. check: file``
    (C++) Put this block at file scope, before ``main()``: classes,
    functions, registrations.
``.. check: program``
    (C++) This block is a complete program. Compile and run it on its own.
``.. check: skip``
    Don't run this block, e.g. a fragment that needs MPI around it.
``.. check: contents FILE``
    (before a text block) The code block before writes FILE; compare its
    contents, rather than the block's stdout, with the text block.
``.. check: run``
    (console) Run the ``$`` command in this block from the repository
    root and compare its output with the rest of the block.

Usage::

    python docs/check_examples.py --build-dir build              # both guides
    python docs/check_examples.py --build-dir build docs/cpp/indexed.rst

The Python examples import the installed ``saiopt``. The C++ examples are
compiled against ``include/`` and the libraries in ``--build-dir``.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import textwrap
import traceback
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# Headers every C++ page gets, so examples can stay focused on saiopt.
CPP_PRELUDE = """\
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"
"""

# Printed between C++ blocks, to split one program's output back into blocks.
MARKER = "@@check_examples block {}@@"

# Seconds any one compile, program, or page may take.
TIMEOUT = 300


@dataclass
class Block:
    lang: str
    code: str
    line: int  # 1-based line of the code-block directive
    mode: str | None = None  # from a preceding ``.. check:`` comment
    expected: str | None = None  # the following text block, if any
    expected_file: str | None = None  # compare that file, not stdout, with it

    def where(self, page: Path) -> str:
        return f"{page.relative_to(REPO)}:{self.line}"


def parse(page: Path) -> list[Block]:
    lines = page.read_text().split("\n")
    blocks: list[Block] = []
    mode = None
    i = 0
    while i < len(lines):
        if m := re.match(r"\.\. check: (\w+(?: \S+)?)\s*$", lines[i]):
            mode = m.group(1)
            i += 1
            continue
        m = re.match(r"\.\. code-block:: (\w+)\s*$", lines[i])
        if not m:
            i += 1
            continue
        start = i + 1
        i += 1
        while i < len(lines) and (not lines[i].strip() or lines[i].lstrip().startswith(":")):
            i += 1
        body = []
        while i < len(lines) and (not lines[i].strip() or lines[i].startswith("   ")):
            body.append(lines[i])
            i += 1
        code = textwrap.dedent("\n".join(body)).strip("\n")
        if m.group(1) == "text" and blocks and blocks[-1].expected is None:
            blocks[-1].expected = code
            if mode and mode.startswith("contents "):
                blocks[-1].expected_file = mode.split(" ", 1)[1]
        else:
            blocks.append(Block(m.group(1), code, start, mode))
        mode = None
    return blocks


def normalize(text: str) -> str:
    return "\n".join(line.rstrip() for line in text.strip("\n").split("\n"))


class Checker:
    def __init__(self, build_dir: Path, cxx: str):
        self.build_dir = build_dir
        self.cxx = cxx
        self.failures = 0
        self.checked = 0

    def fail(self, where: str, message: str) -> None:
        self.failures += 1
        print(f"FAIL {where}\n{textwrap.indent(message.rstrip(), '    ')}")

    def compare(self, where: str, expected: str | None, actual: str) -> None:
        if expected is None:
            return
        self.checked += 1
        if normalize(actual) != normalize(expected):
            self.fail(where, f"expected:\n{textwrap.indent(normalize(expected), '  ')}\n"
                             f"got:\n{textwrap.indent(normalize(actual), '  ')}")

    # Python ---------------------------------------------------------------

    def python_page(self, page: Path, workdir: Path) -> None:
        # In a fresh interpreter: pages must not share state, and a page that
        # imports saiopt.mpi initializes MPI, after which some MPI builds
        # abort on the fork() that starting a later subprocess needs.
        r = self.run([sys.executable, __file__, "--python-only", str(page)], page, cwd=workdir)
        if r is None:
            return
        *lines, last = r.stdout.rstrip("\n").split("\n")
        m = re.fullmatch(r"@@result (\d+) (\d+)", last)
        if r.returncode or not m:
            self.fail(str(page.relative_to(REPO)), f"python exited with {r.returncode}:\n{r.stdout}{r.stderr}")
            return
        if lines:
            print("\n".join(lines))
        self.checked += int(m.group(1))
        self.failures += int(m.group(2))

    def python_blocks(self, page: Path, blocks: list[Block]) -> None:
        """Run a page's Python blocks in this process (the --python-only child)."""
        namespace: dict = {"__name__": "__main__"}
        for b in blocks:
            if b.lang != "python" or b.mode == "skip":
                continue
            out = io.StringIO()
            try:
                with contextlib.redirect_stdout(out):
                    exec(compile(b.code, b.where(page), "exec"), namespace)
            except Exception:
                self.fail(b.where(page), traceback.format_exc())
                return  # later blocks depend on this one
            self.compare(b.where(page), b.expected, self.actual(b, out.getvalue(), Path.cwd()))

    @staticmethod
    def actual(b: Block, stdout: str, workdir: Path) -> str:
        """What a block's expected output is compared with."""
        if b.expected_file is None:
            return stdout
        path = workdir / b.expected_file
        return path.read_text() if path.exists() else f"<{b.expected_file} was not written>"

    def run(self, cmd: list[str], page: Path, **kwargs) -> subprocess.CompletedProcess | None:
        # In its own process group, so a timeout also kills what it started
        # (mpiexec's ranks), which would otherwise hold the pipes open.
        with subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                              start_new_session=True, **kwargs) as proc:
            try:
                out, err = proc.communicate(timeout=TIMEOUT)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.communicate()
                self.fail(str(page.relative_to(REPO)), f"timed out after {TIMEOUT}s: {shlex.join(cmd)}")
                return None
        return subprocess.CompletedProcess(cmd, proc.returncode, out, err)

    # C++ ------------------------------------------------------------------

    def compile_and_run(self, page: Path, where: str, source: str, workdir: Path, name: str) -> str | None:
        src = workdir / f"{name}.cpp"
        exe = workdir / name
        src.write_text(source)
        lib = str(self.build_dir)
        cmd = [self.cxx, "-std=c++20", "-Wall", f"-I{REPO / 'include'}", str(src), "-o", str(exe),
               f"-L{lib}", "-lsaiopt",
               # Nothing references the backends' symbols; keep them linked
               # so their registrations run.
               "-Wl,--no-as-needed", "-lsaiopt_highs", "-lsaiopt_scip", "-Wl,--as-needed",
               f"-Wl,-rpath,{lib}"]
        r = self.run(cmd, page)
        if r is None:
            return None
        if r.returncode:
            self.fail(where, f"compile failed ({src}):\n{r.stderr[:6000]}")
            return None
        r = self.run([str(exe)], page, cwd=workdir)
        if r is None:
            return None
        if r.returncode:
            self.fail(where, f"exited with {r.returncode}:\n{r.stdout}{r.stderr}")
            return None
        return r.stdout

    def cpp_page(self, page: Path, blocks: list[Block], workdir: Path) -> None:
        includes: list[str] = []
        file_scope: list[str] = []
        body: list[str] = []
        in_main: list[Block] = []
        for b in blocks:
            if b.lang != "cpp" or b.mode == "skip":
                continue
            if b.mode == "program":
                out = self.compile_and_run(page, b.where(page), b.code, workdir, f"program{b.line}")
                if out is not None:
                    self.compare(b.where(page), b.expected, out)
                continue
            rest = []
            for line in b.code.split("\n"):
                (includes if line.startswith("#include") else rest).append(line)
            code = "\n".join(rest)
            if b.mode == "file":
                file_scope.append(f"#line {b.line + 1} \"{page}\"\n{code}")
            else:
                n = len(in_main)
                in_main.append(b)
                body.append(f'std::cout << std::flush; std::printf("{MARKER.format(n)}\\n"); std::fflush(stdout);\n'
                            f"#line {b.line + 1} \"{page}\"\n{code}")
        if not in_main and not file_scope:
            return
        source = (CPP_PRELUDE + "\n".join(includes) + "\nusing namespace saiopt;\n\n"
                  + "\n".join(file_scope)
                  + "\n\nint main() {\n" + "\n".join(body) + "\nstd::cout << std::flush;\nreturn 0;\n}\n")
        out = self.compile_and_run(page, str(page.relative_to(REPO)), source, workdir, "page")
        if out is None:
            return
        pieces = re.split(r"^@@check_examples block (\d+)@@\n", out, flags=re.M)
        outputs = {int(pieces[k]): pieces[k + 1] for k in range(1, len(pieces), 2)}
        for n, b in enumerate(in_main):
            self.compare(b.where(page), b.expected, self.actual(b, outputs.get(n, ""), workdir))

    # console ----------------------------------------------------------------

    def console_blocks(self, page: Path, blocks: list[Block]) -> None:
        for b in blocks:
            if b.lang != "console" or b.mode != "run":
                continue
            lines = b.code.split("\n")
            if not lines[0].startswith("$ "):
                self.fail(b.where(page), "a `.. check: run` block must start with a `$` command")
                continue
            cmd = shlex.split(lines[0][2:])
            cmd = [sys.executable if c == "python" else c for c in cmd]
            cmd = [str(self.build_dir / c[len("./build/"):]) if c.startswith("./build/") else c for c in cmd]
            r = self.run(cmd, page, cwd=REPO)
            if r is None:
                continue
            if r.returncode:
                self.fail(b.where(page), f"exited with {r.returncode}:\n{r.stdout}{r.stderr}")
                continue
            self.compare(b.where(page), "\n".join(lines[1:]), r.stdout)

    def page(self, page: Path) -> None:
        print(f"-- {page.relative_to(REPO)}")
        sys.stdout.flush()
        blocks = parse(page)
        with tempfile.TemporaryDirectory() as tmp:
            self.python_page(page, Path(tmp))
        with tempfile.TemporaryDirectory() as tmp:
            self.cpp_page(page, blocks, Path(tmp))
        self.console_blocks(page, blocks)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("pages", nargs="*", type=Path, help="rst pages (default: every page of both guides)")
    ap.add_argument("--build-dir", type=Path, default=REPO / "build",
                    help="CMake build directory with libsaiopt and the backends (default: build)")
    ap.add_argument("--python-only", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--cxx", default=os.environ.get("CXX") or shutil.which("c++") or "g++")
    args = ap.parse_args()

    pages = [p.resolve() for p in args.pages] or sorted((REPO / "docs").glob("*/*.rst"))
    checker = Checker(args.build_dir.resolve(), args.cxx)
    if args.python_only:
        checker.python_blocks(pages[0], parse(pages[0]))
        print(f"@@result {checker.checked} {checker.failures}")
        return 0
    for page in pages:
        checker.page(page)
    print(f"{checker.checked} outputs checked, {checker.failures} failures")
    return 1 if checker.failures else 0


if __name__ == "__main__":
    sys.exit(main())
