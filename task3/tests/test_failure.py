#!/usr/bin/env python3

import random
import sys
from time import sleep
from testsupport import subtest, info, warn, run
from socketsupport import run_client, run_server


class Tester:

    def __init__(self,
                 num_servers: int = 3,
                 have_leader_at_startup: bool = True,
                 kvs_port_start: int = 30000,
                 raft_port_start: int = 1025,
                 num_operations: int = 100,
                 seed: int = 1024,
                 debug: bool = False):

        self.num_servers = num_servers
        self.have_leader_at_startup = have_leader_at_startup
        self.kvs_port_start = kvs_port_start
        self.raft_port_start = raft_port_start
        self.num_operations = num_operations
        self.debug = debug

        self.raft_ports = []
        for i in range(self.num_servers):
            self.raft_ports.append(self.raft_port_start + i)

        random.seed(seed)
        info(f"num_servers = {num_servers}")
        info(f"have_leader_at_startup = {have_leader_at_startup}")
        info(f"kvs_port_start = {kvs_port_start}")
        info(f"raft_port_start = {raft_port_start}")
        info(f"debug = {debug}")

    def find_leader_port(self) -> int:
        leader_port = -1
        if self.have_leader_at_startup:
            leader_port = kvs_port_start
        else:
            for i in range(self.num_servers):
                port = self.kvs_port_start + i
                ret, _ = run_client(port, "GET", "NON_EXIST_KEY", 0)
                if ret == 3:  # no key
                    leader_port = port
                    break
        return leader_port

    def test(self):
        with subtest("Testing replication 2"):

            info("start servers")
            self.servers = []
            for i in range(self.num_servers):
                leader = False
                if self.have_leader_at_startup and i == 0:
                    leader = True
                self.servers.append(
                    run_server(self.kvs_port_start + i, leader,
                               self.raft_ports, i))

            # wait for completion of a leader election (if any)
            sleep(5)

            fail = False
            try:
                fail = self.test_main()
            except Exception as e:
                warn(f"{e}")
                fail = True

            info("terminate servers")
            for server in self.servers:
                server.terminate()

            if fail:
                warn("test failed")
                sys.exit(1)

    def test_main(self) -> bool:
        server_alive = [True] * len(self.servers)
        num_server_active = self.num_servers
        kvs = {}
        leader_port = self.find_leader_port()
        if leader_port == -1:
            return True
        for i in range(self.num_operations):
            r = random.random()
            if r < 0.20:
                # Try to kill server
                if num_server_active > (self.num_servers // 2 + 1):
                    i = random.randint(0, self.num_servers - 1)
                    if server_alive[i]:
                        if self.kvs_port_start + i == leader_port:
                            leader = True
                            info(f"Kill leader #{i}")
                        else:
                            leader = False
                            info(f"Kill follower #{i}")
                        self.servers[i].terminate()
                        server_alive[i] = False
                        num_server_active -= 1
                        if leader:
                            # leader change
                            sleep(5)
                            leader_port = self.find_leader_port()
                            if leader_port == -1:
                                return True
            elif r < 0.40:
                # Try to restart server
                i = random.randint(0, self.num_servers - 1)
                if not server_alive[i]:
                    info(f"Restart server #{i}")
                    self.servers[i] = run_server(self.kvs_port_start + i,
                                                 False, self.raft_ports, i,
                                                 self.debug)
                    num_server_active += 1
                    server_alive[i] = True
                    sleep(5)

            key = random.randrange(10000)
            value = random.randrange(10000)
            if key in kvs:
                continue

            kvs[key] = value
            ret, _ = run_client(leader_port, "PUT", key, value)
            if ret != 0:
                info("client error")
                return True

        # restart the server
        for i in range(self.num_servers):
            if not server_alive[i]:
                info(f"Restart server #{i}")
                self.servers[i] = run_server(self.kvs_port_start + i, False,
                                             self.raft_ports, i, self.debug)
        sleep(10)

        for k, v in kvs.items():
            ret = 0
            for i in range(self.num_servers):
                ret, stdout = run_client(self.kvs_port_start + i, "DIRECT_GET",
                                         k, v)
                if ret != 0:
                    warn(f"client error: ret={ret}")
                    return True
                value = stdout.strip()
                if value != str(v):
                    warn(f"value mismatch: {value}, {v}")
                    return True
        return False

    def test_main_old(self) -> bool:
        leader_port = self.find_leader_port()
        if leader_port == -1:
            return True

        info(f"leader port: {leader_port}")

        kvs = {}
        for i in range(self.num_operations):
            key = random.randrange(10000)
            value = random.randrange(10000)
            if key in kvs:
                continue

            ret, _ = run_client(leader_port, "PUT", key, value)
            kvs[key] = value
            if ret != 0:
                info("client error")
                return True

        # wait for commit (triggered by an empty AppendEntryRPC)
        sleep(5)

        # kill a random server
        killed_server_id = random.randrange(self.num_servers)
        if self.kvs_port_start + killed_server_id == leader_port:
            info(f"kill leader #{killed_server_id}")
        else:
            info(f"kill follower #{killed_server_id}")
        server = self.servers.pop(killed_server_id)
        server.terminate()

        # wait election (if any)
        sleep(5)

        leader_port = self.find_leader_port()
        if leader_port == -1:
            return True

        # PUT more data
        for i in range(self.num_operations):
            key = random.randrange(10000)
            value = random.randrange(10000)
            ret, _ = run_client(leader_port, "PUT", key, value)
            kvs[key] = value
            if ret != 0:
                info("client error")
                return True

        # restart the server
        self.servers.append(
            run_server(self.kvs_port_start + killed_server_id, False,
                       self.raft_ports, killed_server_id, self.debug))
        sleep(5)

        for k, v in kvs.items():
            ret = 0
            for i in range(self.num_servers):
                ret, stdout = run_client(self.kvs_port_start + i, "DIRECT_GET",
                                         k, v)
                if ret != 0:
                    warn(f"client error: ret={ret}")
                    return True
                value = stdout.strip()
                if value != str(v):
                    warn(f"value mismatch: {value}, {v}")
                    return True
        return False


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--num_servers', type=int, default=3, required=False)
    parser.add_argument('--have_leader_at_startup',
                        action="store_true",
                        required=False)
    parser.add_argument('--kvs_port_start',
                        type=int,
                        default=30000,
                        required=False)
    parser.add_argument('--raft_port_start',
                        type=int,
                        default=1025,
                        required=False)
    parser.add_argument('--num_operations',
                        type=int,
                        default=100,
                        required=False)
    parser.add_argument('--seed', type=int, default=1024, required=False)
    parser.add_argument('--debug', action="store_true", required=False)
    args = parser.parse_args()

    tester = Tester(args.num_servers, args.have_leader_at_startup,
                    args.kvs_port_start, args.raft_port_start,
                    args.num_operations, args.seed, args.debug)
    tester.test()
