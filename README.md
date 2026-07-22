# Operating Systems Assignment

## Student Details
* **Name:** Rijin Maharjan
* **Batch:** 37 "A"
* **Faculty:** AI
* **Student ID:** 240434

---

## Project Overview
This repository contains four C programs built for the Operating Systems assignment, each covering a different core OS concept: process/thread management, memory management (paging), file system security, and network programming/IPC. All four share a loose "airport" theme (check-in, baggage, gates, flights) to keep the examples consistent and easy to follow across tasks.

Every program is written in plain C for Linux and compiles with `gcc`. Each `Task_X` folder also has its own short `taskN.md` with any task-specific notes; this top-level README is the one place that explains what every task does and how to build and run all of them.

---

## Repository Structure

```
OperatingSystemsAssignment/
├── Task_1/
│   ├── Task1.c        # process creation, threading, synchronization, scheduling
│   └── task1.md
├── Task_2/
│   ├── task2.c         # paging simulator: FIFO and LRU page replacement
│   └── task2.md
├── Task_3/
│   ├── task3.c          # secure file vault: auth, permissions, encryption, audit log
│   ├── vault/            # created automatically at runtime - holds vault files
│   ├── audit.log          # created automatically at runtime - audit trail
│   └── task3.md
├── Task_4/
│   ├── task4_server.c   # airport control tower TCP server
│   ├── task4_client.c   # airport terminal TCP client
│   └── task4.md
└── README.md            # this file
```

`vault/` and `audit.log` under `Task_3` are generated the first time `task3` is run - they don't need to exist beforehand and are safe to delete if you want a clean slate.

---

## Requirements

* Linux (tested on Ubuntu with `gcc` 13)
* `gcc` - no extra flags or libraries needed; pthreads and sockets link automatically on modern glibc, so a plain `gcc file.c -o output` is enough for every task here

---

## Task 1: Process Management and Threading

**What it does:** an "Airport Check-In & Baggage Handling System" that demonstrates:
- Real OS process creation with `fork()`/`wait()` (a "boarding pass printer" child process)
- A race condition shown both broken (unsynchronized) and fixed (with a mutex)
- A producer-consumer baggage conveyor belt using semaphores
- A Round-Robin CPU scheduler simulation with a Gantt chart
- Deadlock prevention via consistent lock ordering

**Compile:**
```bash
cd Task_1
gcc Task1.c -o task1
```

**Run:**
```bash
./task1
```
No input needed - it runs straight through all parts and prints its own output/logs to the terminal.

---

## Task 2: Memory Management Simulation

**What it does:** an "Airport Passenger Record Cache" paging simulator - passenger booking IDs are treated as virtual pages, and a small number of fast "check-in counter" slots act as physical frames. It implements:
- A configurable page size / paging system (see the `#define`s at the top of the file)
- Two page replacement algorithms: FIFO and LRU
- Page fault/hit tracking with hit and miss ratios
- Detailed step-by-step logging of frame contents after every lookup

**Compile:**
```bash
cd Task_2
gcc task2.c -o task2
```

**Run:**
```bash
./task2
```
No input needed - it runs through several built-in test cases (including a Belady's Anomaly check and a locality-of-reference pattern) and prints a comparison summary for each.

---

## Task 3: File System Operations and Security

**What it does:** an "Airport Secure Document Vault" - an interactive, menu-driven CLI covering:
- File create / read / write / delete
- Username + password login (hashed, not stored/compared as plaintext)
- Unix-style owner/group/other read-write-execute permissions
- XOR-cipher encrypt/decrypt for sensitive files (documented in the code as a teaching example, not production-grade crypto)
- A full audit log (`audit.log`) timestamping every login attempt and file operation, successful or denied

**Compile:**
```bash
cd Task_3
gcc task3.c -o task3
```

**Run:**
```bash
./task3
```
This one is interactive - it will prompt for a username and password. Demo accounts built into the program:

| Username | Password  | Group     |
|----------|-----------|-----------|
| alice    | alice123  | security  |
| bob      | bob123    | staff     |
| carol    | carol123  | staff     |
| guest    | guest123  | guest     |

Log in, then use the numbered menu to create/read/write/delete/encrypt/decrypt files and (as `alice`, who's in the `security` group) view the audit log. A `vault/` folder and `audit.log` file are created automatically on first run in the same directory as the program.

---

## Task 4: Network Programming and IPC

**What it does:** an "Airport Control Tower" client-server system over real TCP sockets - a server that multiple terminal clients can connect to at once for live flight info. Covers:
- A real TCP server (socket/bind/listen/accept) and a matching client
- A simple line-based text protocol (documented at the top of `task4_server.c`)
- One thread per connected client, so multiple terminals can be connected and issuing commands at the same time
- Basic security: login required before any flight command works, role-based restriction on updating flight status, and validation on every incoming command
- Error handling on every socket call and clean disconnect handling on both ends

**Compile (both programs):**
```bash
cd Task_4
gcc task4_server.c -o task4_server
gcc task4_client.c -o task4_client
```

**Run:** start the server first, in one terminal:
```bash
./task4_server
```
Then, in one or more separate terminals, start a client (you can run several at once to demonstrate concurrent connections):
```bash
./task4_client
```
The client defaults to connecting to `127.0.0.1` on port `9090`; you can also run `./task4_client <server_ip> <port>` to point it at a different host.

Demo accounts (same idea as Task 3 - `admin` role can update flight status, everyone else is read-only):

| Username | Password  | Role   |
|----------|-----------|--------|
| alice    | alice123  | admin  |
| bob      | bob123    | staff  |
| guest    | guest123  | guest  |

Once logged in, use the menu to list flights, check a flight's status/gate, or (as `alice`) update a flight's status - open a second client in another terminal at the same time to see the update show up live for everyone connected.

---

## Quick Reference

| Task | Folder  | Compile                                                      | Run                              |
|------|---------|---------------------------------------------------------------|-----------------------------------|
| 1    | Task_1  | `gcc Task1.c -o task1`                                        | `./task1`                         |
| 2    | Task_2  | `gcc task2.c -o task2`                                        | `./task2`                         |
| 3    | Task_3  | `gcc task3.c -o task3`                                        | `./task3`                         |
| 4    | Task_4  | `gcc task4_server.c -o task4_server`<br>`gcc task4_client.c -o task4_client` | `./task4_server` (then, separately) `./task4_client` |