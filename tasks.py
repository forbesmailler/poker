import glob
import os
import shutil

from invoke import task

SOLVER_EXE = os.path.join("engine", "build", "Release", "poker_solver.exe")

# clang-format: prefer PATH, fall back to VS2022 bundled copy
CLANG_FORMAT = shutil.which("clang-format") or os.path.join(
    os.environ.get("ProgramFiles", r"C:\Program Files"),
    "Microsoft Visual Studio",
    "2022",
    "Community",
    "VC",
    "Tools",
    "Llvm",
    "x64",
    "bin",
    "clang-format.exe",
)


@task
def format(c):
    """Format Python with ruff and C++ with clang-format."""
    c.run("ruff format --line-length 88 .")
    c.run("ruff check --line-length 88 --fix --unsafe-fixes .")
    if os.path.isfile(CLANG_FORMAT):
        cpp_files = glob.glob("engine/*.h") + glob.glob("engine/*.cpp")
        if cpp_files:
            files = " ".join(f'"{f}"' for f in cpp_files)
            c.run(f'"{CLANG_FORMAT}" -i {files}')
    else:
        print("clang-format not found, skipping C++ formatting")


@task
def gen_config(c):
    """Generate C++ config header from YAML."""
    c.run("python scripts/gen_config_header.py")


@task
def build(c, config="Release"):
    """Build C++ engine with CMake."""
    c.run("cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=" + config)
    c.run("cmake --build engine/build --config " + config)


@task
def test_cpp(c, config="Release"):
    """Run C++ tests via CTest."""
    c.run(f"ctest --test-dir engine/build -C {config} --output-on-failure")


@task
def test_py(c, cov=True):
    """Run Python tests."""
    cmd = "python -m pytest tests/scripts"
    if cov:
        cmd += " --cov=poker --cov-report=term-missing"
    c.run(cmd, pty=False)


@task
def test(c, cov=True, config="Release"):
    """Run all tests (C++ and Python)."""
    test_cpp(c, config=config)
    test_py(c, cov=cov)


@task(pre=[format])
def prepare(c, config="Release"):
    """Generate config, format, build, and test."""
    gen_config(c)
    build(c, config=config)
    test(c, config=config)


@task(pre=[format])
def train(c, config="Release"):
    """Format, gen-config, build, test, then run MCCFR training."""
    gen_config(c)
    build(c, config=config)
    test_cpp(c, config=config)
    exe = os.path.abspath(os.path.join("engine", "build", config, "poker_solver.exe"))
    import subprocess

    subprocess.run([exe, "train"], check=True)


@task(pre=[format, test_py])
def all(c):
    """Format and test Python."""
    pass
