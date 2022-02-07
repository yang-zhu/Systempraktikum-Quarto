```
    A    B    C    D         
  +---------------------+     
4 | 1001 0111 1100 0011 | 4       ___    _____  _____    __       _______    _________    ____ 
  |                     |       .'   '. |_   _||_   _|  /  \     |_   __ \  |  _   _  | .'    `.
3 | 0101 **** 0110 **** | 3    /  .-.  \  | |    | |   / /\ \      | |__) | |_/ | | \_|/  .--.  \
  |                     |      | |   | |  | '    ' |  / ____ \     |  __ /      | |    | |    | |
2 | **** 1010 **** 0100 | 2    \  `-'  \_  \ `--' / _/ /    \ \_  _| |  \ \_   _| |_   \  `--'  /
  |                     |       `.___.\__|  `.__.' |____|  |____||____| |___| |_____|   `.____.'
1 | 0000 0001 1101 **** | 1                                                                            
  +---------------------+   
    A    B    C    D       
```

# Systempraktikum LMU WS21/22 - Quarto

## Background
This is a project for the class Systempraktikum in LMU WS21/22. The requirement is to implemente a client program connecting to the [MNM server](http://sysprak.priv.lab.nm.ifi.lmu.de/) playing the game [Quarto](https://en.wikipedia.org/wiki/Quarto_(board_game)). It can play against either a human player or another client program.

## Implementation [^1]
After starting a new game, a game ID is generated. The client uses this ID to join the game. The program consists of two processes. One is the connector, in charge of connecting to and communicating with the server. The other is the thinker, whose job is to compute the next move. If any error arises during the game, both processes clean up the resources and safely disconnect from the server.

## Usage
Use `make` to build the project. After it is successfully built, it can be run as follows:
```
./sysprak-client -g game_id [-p player_no] [-c config_file] [-v]
```
- `-g` requires a valid 13-character game ID.
- `-p` allows the user to choose a player number. Available player numbers are 1 and 2.
- `-c` takes a config file as argument. In the config file, the user can specify the hostname, the port and the game kind.
- `-v` enables verbose mode that prints the communication with the server.

An alternative way to build and run the project is `make play`. This requires the environment variables `GAME_ID` and `PLAYER` to be set.

---

[^1]: Since the main purpose of the project is to practice the skills we have learned in the theoretical part of the lecture, there are some "jumping through hoops" tasks, such as creating shared memory segments that fit exactly to the needed size, using different methods for the communciation between the connector and the thinker. Therefore the implementation does not aim for great efficiency or practicality.