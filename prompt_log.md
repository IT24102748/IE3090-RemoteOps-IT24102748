# Prompt Log — RemoteOps

This document records substantive AI-assisted interactions used during the development of the IE3090 RemoteOps assignment. The AI was used primarily for explanations, debugging assistance, implementation guidance and documentation support. The generated suggestions were reviewed, adapted and tested during development.

| Date | AI Tool | Task / Prompt | How I Used It | What I Changed / Tested |
|---|---|---|---|---|
| 06/10/2026 | ChatGPT | Explain how to implement the RemoteOps TCP Agent and Controller | Used the explanation to understand the client-server architecture and TCP communication process. | Implemented and compiled the Agent and Controller and tested the TCP connection. |
| 06/10/2026 | ChatGPT | Explain TCP command framing and how `recv()` handles data | Used the explanation to understand why one `recv()` call cannot be assumed to contain one complete command. | Implemented line-based command receiving and tested commands through the Controller. |
| 06/10/2026 | ChatGPT | Help implement authentication and session ID handling | Used the explanation to understand the authentication sequence and session identification. | Tested `AUTH OPS-2748` and verified the `OK AUTHENTICATED SID:8472` response. |
| 06/10/2026 | ChatGPT | Explain and implement SYSINFO and LISTPROC | Used the guidance to understand how the Agent can obtain system and process information. | Implemented the commands and tested them from the Controller. |
| 06/10/2026 | ChatGPT | Explain the restricted EXEC command whitelist | Used the explanation to understand why only approved commands should be executed. | Implemented the allowed commands such as DATE, UPTIME, DISKFREE, HOSTNAME and WHOAMI and tested unauthorized commands. |
| 07/10/2026 | ChatGPT | Debug the GET file-transfer implementation | Used the debugging guidance to identify the difference between a normal text response and a file-transfer response containing a header followed by raw bytes. | Added Controller-side file download handling and tested `GET test.txt`. |
| 07/10/2026 | ChatGPT | Explain exact-byte file transfer using PUT and GET | Used the explanation to understand why the file size must be included in the protocol. | Tested a 29-byte `test.txt` file and verified that the correct number of bytes was transferred. |
| 07/10/2026 | ChatGPT | Help implement UDP monitoring | Used the guidance to understand the UDP monitoring start/stop process. | Implemented monitoring and tested `MONITOR START 9500` and `MONITOR STOP`. |
| 07/10/2026 | ChatGPT | Debug Controller termination and Agent connection handling | Used the guidance to test what happens when the Controller terminates without sending QUIT. | Tested abnormal Controller termination and verified that the Agent continued running and accepted a new connection. |
| 07/10/2026 | ChatGPT | Explain Makefile creation and project build process | Used the explanation to create a repeatable build process. | Created `Makefile_748` and tested `make -f Makefile_748`. |
| 07/10/2026 | ChatGPT | Help organize README, design diary and prompt log | Used the guidance to structure the required project documentation. | Created and reviewed the documentation files before the final GitHub submission. |

## How AI Assistance Was Used

AI assistance was used as a development and learning aid rather than as a replacement for testing. Suggestions were reviewed, adapted to the RemoteOps requirements and tested locally. Debugging assistance was particularly useful when dealing with TCP message framing, file-transfer byte counts, unexpected Controller termination and the separation between protocol headers and raw file data.

## Verification

The implemented functionality was tested locally using the RemoteOps Agent and Controller on `127.0.0.1`. Commands, authentication, file transfer, monitoring, logging, disconnection handling and compilation were tested before the final documentation and GitHub submission.
