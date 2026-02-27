# assignment-1
This assignment is implemented on Linux.
Repo Link:

https://github.com/eadal23/assignment-1.git

## Building & Running
`````
 gcc -o shell-ish shellish-skeleton.c
./shell-ish   or   ./rlwrap --always-readline ./shell-ish
``````
## Built-in Commands

### `cd <directory>`
Change the current working directory.
```
shellish$ cd /home/user/documents
```
### `exit`
Exit the shell.
```
shellish$ exit
```
### `cut`
Extract fields from text, similar to Unix `cut`. Supports `-d` (delimiter) and `-f` (fields).Both flags accept a separate argument or compact form.
```
shellish$ cut -d , -f 1,3 file.csv
shellish$ echo "a:b:c" | cut -d : -f 2
```
### `chatroom <roomname> <username>`
Join or create a named chatroom on different terminals. Multiple users can connect to the same room by using the same room name. Messages are sent and received in real time via named pipes (FIFO). Type `exit` inside the chatroom to leave.
```
shellish$ chatroom room1 alice
shellish$ chatroom room1 bob
`````

---

## Piping

Chain commands together with `|`. Multiple command piping supported.
```
shellish$ ls | grep .c
shellish$ cat file.txt | cut -d , -f 1 | sort
```

## I/O Redirection

| Operator | Description |
|----------|-------------|
| `< file` | Read input from file |
| `> file` | Write output to file (overwrite) |
| `>> file` | Append output to file |

```
shellish$ sort <input.txt >output.txt
shellish$ echo "hello" >>log.txt
```

---
## Background Processes

Append `&` to run a command in the background.
```
shellish$ sleep 10 &
```
## Custom Command: `remind`

Set timed reminders that fire in the background while you keep using the shell. Each reminder is identified by a unique key so you can manage multiple reminders at once.

### Set a reminder
```
shellish$ remind <key> <seconds> <message>
```
```
shellish$ remind standup 300 Join the standup meeting!
shellish$ remind lunch 3600 Go eat lunch
shellish$ remind coffee 600 Time for a coffee break
```

### Cancel a reminder
```
shellish$ remind cancel <key>
```
```
shellish$ remind cancel standup
```

### Notes
- Keys must be unique — you cannot have two active reminders with the same key.
- Once a reminder fires or is cancelled, its key is free to reuse.
- Up to 64 reminders can be active at the same time.
- The reminder message prints directly to your terminal when the timer expires.
### Example session
```
shellish$ remind meeting 10 Team sync in 10 seconds
Reminder 'meeting' set for 10 seconds.
shellish$ remind cancel meeting
reminder 'meeting' is cancelled.
shellish$ remind meeting 5 Rescheduled sync
Reminder 'meeting' set for 5 seconds.
shellish$
!!! REMINDER !!! -> Rescheduled sync
```
