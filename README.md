# Drone Messaging via Datagram Sockets
* Author: Tristan Langley
* Date Created: 4/14/22
* Class: CSE 5462 SP22

## Overview
Lab 10 includes a single client-server combined program which communicates via
datagram (UDP) sockets. Messages can be entered into stdin, which are 
then sent to the IP/port identities specified in `config.txt`. The stdin messages
entries are concatenated with other parameters like so:
`<version> <last location> <destination> <source> <hopcount> <message type>
<message ID/sequence number> <sendpath> <message>`, for
example `6 8 1818 2020 5 MSG 2 1818,1919 hello world`. Messages can also be
received from the network, as long as they are in that same format. Message type
can be MSG (receive a string message), ACK (acknowledge a MSG), MOV (move to a new
location provided in the <message> field), or LOC (respond with my location as the
<message> field). MSGs or ACKs received that have a different destination are
forwarded. MSGs and ACKs are stored and re-sent at 5s intervals, and when I move.
Grid dimensions and initial drone IPs/ports/grid locations are specified in `config.txt`.
A sample`config.txt` is provided in the lab 10 GitHub repo.

## Compilation
To compile the drone10 (drone10.c) program, enter 'make' in the terminal. Or, the
compile command is:
* `gcc -g -Wall -lm -o drone10 drone10.c`

## Usage
1. Ensure there is a `config.txt` file in the same directory as the drone10
executable.
2. Enter `./drone10 <portnumber>` in the terminal. Make sure this portnumber is
listed in `config.txt` in order for the program to know it's location. If my location
is unspecified, location defaults to `0`.
3. Wait for messages that are sent from other drones to be received, and/or:
4. Enter destination port numbers and messages you want to send in the terminal.

## Cleaning
To remove output file(s), enter `make clean` in the terminal.

