# ThreadCPUAssignment_POC
A small experiment on assigning a processes threads a specific CPU and then blocking it with a high priority thread

Build the project and then provide a PID (I used MsMpEng.exe in testing) to the tool. It will enumerate all threads within the process and then attempt to set each one to a hardcoded (read at the top of the main function) CPU id. A new thread is then started by the tool with a high priority level, also assigned the same CPU, with the idea that it will take priority over the target processes threads and block them from executing. 

This is a super hacky/non-prod-ready tool at all. It locked up the GUI when successful in targeting MsMpEng.exe. Only works on some systems, in a VM with 8 CPU it "worked" but on a host with 32+ CPU it failed to lock MsMpEng.exe out of the CPU. Not sure if there is some fallback mechansim that the threads relied on to execute with how many more CPU available there are.

This work based on these tweets / see these for more info:
https://x.com/sixtyvividtails/status/1970721197617717483
https://x.com/sixtyvividtails/status/1639480276899299328
https://x.com/Octoberfest73/status/1970928105696133157

I didn't use NtSetInformationProcess due to the zero documentation anywhere on using the called-out PROCESSINFO classes and headaches I had trying to make it work.