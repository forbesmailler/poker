import platform
from invoke import task


@task
def format(c):
    """Format code with ruff."""
    c.run("ruff format --line-length 88 .")
    c.run("ruff check --line-length 88 --fix --unsafe-fixes .")


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


@task(pre=[format, test_py])
def all(c):
    """Format and test Python."""
    pass
