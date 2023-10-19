import tempfile
import sys
import random
import subprocess
import os
import time
import psutil

from testsupport import (
    run_project_executable,
    info,
    warn,
    find_project_executable,
    test_root,
    project_root,
    run,
    ensure_library,
)

def is_server_listening(port: int) -> bool:
    for conn in psutil.net_connections():
        if conn.laddr.port == port and conn.status == "LISTEN":
            return True
    return False

def run_client(num_threads: int, port: int, num_messages: int, add: int, sub: int):
    info(
        f"Running client with {num_threads} threads and {num_messages} messages per thread."
    )

    with tempfile.TemporaryFile(mode="w+") as stdout:
        run_project_executable(
            "client",
            args=[
                str(num_threads),
                "localhost",
                str(port),
                str(num_messages),
                str(add),
                str(sub),
            ],
            stdout=stdout,
        )

        stdout.seek(0)
        lines = stdout.read().splitlines()
        numbers = [int(x) for x in lines]
        if len(numbers) != num_threads:
            warn(f"Expected {num_threads} results from client, got {len(numbers)}")
            sys.exit(1)

        return sorted(numbers)


def test_server(
    num_server_threads: int,
    num_client_threads: int,
    num_client_instances: int,
    use_docker: bool = False,
) -> None:
    try:
        if use_docker:
            run(["docker-compose", "build", "server"])
            server = ["docker-compose", "run", "--service-ports", "-T", "server", "/app/server"]
        else:
            server = [find_project_executable("server")]

        sub = random.randint(1, 49)
        add = random.randint(50, 99)
        num_messages = 10000

        info(f"Run server with {num_server_threads} threads...")

        server.extend([str(num_server_threads), "1025"])
        print(" ".join(server))
        with subprocess.Popen(
            server,
            stdout=subprocess.PIPE,
            text=True,
        ) as proc:
            # Wait for server init
            if use_docker:
                timeout = time.time() + 60*1 # 1 minute timeout
                while not is_server_listening(1025):
                    time.sleep(0.1)
                    if time.time() > timeout:
                        warn(f"Server init timed-out")
                        sys.exit(1)
                info("Server is listening at port 1025")
            else:
                time.sleep(1.0)

            client_numbers = []

            for _ in range(num_client_instances):
                client_numbers = client_numbers + run_client(
                    num_client_threads, 1025, num_messages, add, sub
                )

            client_numbers.sort()

            proc.terminate()
            stdout, _ = proc.communicate()
            unsorted_server_numbers = [int(x) for x in stdout.splitlines()]
            sorted_server_numbers = sorted(unsorted_server_numbers)

            if client_numbers != sorted_server_numbers:
                warn(f"Client and server responded differently")
                sys.exit(1)

            expected = (
                num_client_instances
                * num_client_threads
                * (num_messages // 2)
                * (add - sub)
            )
            if unsorted_server_numbers[-1] != expected:
                warn(
                    f"Expected {expected} as last number, got {unsorted_server_numbers[-1]}"
                )
                sys.exit(1)

            info("OK")

    except OSError as e:
        warn(f"Failed to run command: {e}")
        sys.exit(1)
    finally:
        if use_docker:
            run(["docker-compose", "kill"])


def test_client(num_threads: int) -> None:
    test_server = test_root().joinpath("test_client_testserver")

    if not test_server.exists():
        run(["make", "-C", str(test_root()), "test_client_testserver"])

    ensure_library("libutils.so")

    try:
        info(f"Run test server {test_server}...")

        sub = random.randint(1, 49)
        add = random.randint(50, 99)
        num_messages = 10000

        argv = [str(test_server), "1025"]
        print(" ".join(argv))
        with subprocess.Popen(
            argv, stdout=subprocess.PIPE, text=True
        ) as proc:
            # Wait for server init
            time.sleep(1.0)

            client_numbers = run_client(num_threads, 1025, num_messages, add, sub)

            proc.terminate()
            stdout, _ = proc.communicate()
            unsorted_server_numbers = [int(x) for x in stdout.splitlines()]
            sorted_server_numbers = sorted(unsorted_server_numbers)

            if client_numbers != sorted_server_numbers:
                warn(f"Client and server responded differently")
                sys.exit(1)

            expected = num_threads * (num_messages // 2) * (add - sub)
            if unsorted_server_numbers[-1] != expected:
                warn(
                    f"Expected {expected} as last number, got {unsorted_server_numbers[-1]}"
                )
                sys.exit(1)

            info("OK")

    except OSError as e:
        warn(f"Failed to run command: {e}")
        sys.exit(1)
