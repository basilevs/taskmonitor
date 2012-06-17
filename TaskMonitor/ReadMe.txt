Monitors windows tasks creation, deletion and memory consumption changes.

Running:
TaskMonitor.exe [[ProcessName [ProcessName ...]] logFile]

Note, that ProcessName parameters are case sensitive and require stict match.
If there is no ProcessName arguments all processes are logged.

Output format:
Note: console and file encoding are taken from system locale (and may be different).

Each line of log have following format:
ProcessName Status ProcessId WorkingSetSize

