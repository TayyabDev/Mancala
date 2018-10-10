#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int port = 3000;
int listenfd;
struct player *head = NULL;// the first player in the linked list
int num_players;
fd_set fds;

#define MAXNAME 80  /* maximum permitted name size, not including \0 */
#define MAXMESSAGE MAXNAME + 50 /* max permitted message size */
#define NPITS 6  /* number of pits on a side, not including the end pit */

struct player {
	int listenfd;
	int pits[NPITS + 1];
	char * name;
	struct player *next;
	int turn;
} *playerlist = NULL;

extern void parseargs(int argc, char **argv);
extern void makelistener();
extern int compute_average_pebbles();
extern int game_is_over();  /* boolean */
extern void broadcast(char  * s, char * avoid);
extern struct player * add_player(struct player * p, int player_fd);
extern int checkName(char * name);
extern void broadcastBoard();
extern int getchoice(int player_fd);

int main(int argc, char **argv) {
	while (1) {
		struct player *p = playerlist; // p is pointer to last player joined in the game
		char msg[MAXMESSAGE];

		parseargs(argc, argv);
		makelistener();

		int max_fd = listenfd;
		int sret; // return value for select
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(listenfd, &fds);

		struct player * currentMove = NULL;
		num_players = 0;

		char yourMove [MAXMESSAGE] = "Your move?\r\n";
		char * otherMoveMessage  = "It is ";
		int choice = -1;

		while (!game_is_over()) {
			fd_set listen_fds = fds;
			sret = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);

			if (sret == -1) {
				perror("select");
				exit(-1);
			}

			// in the case that a new player joins
			if (FD_ISSET(listenfd, &listen_fds)) {
				int player_fd = accept(listenfd, NULL, NULL);
				if (player_fd > max_fd)
					max_fd = player_fd;
				FD_SET(player_fd, &fds);

				p = add_player(p, player_fd);
				num_players++;
				if (num_players == 1) {
					currentMove = head;
					currentMove->turn = 1;
				} else {
					otherMoveMessage = strdup("It is ");
					strcat(otherMoveMessage, currentMove->name);
					strcat(otherMoveMessage, "'s move.\r\n");
					write(p->listenfd, otherMoveMessage, strlen(otherMoveMessage));
				}
			}

			if (currentMove != NULL && currentMove->turn && num_players > 0) {
				if (choice == -1  && FD_ISSET(currentMove->listenfd, &listen_fds) && currentMove->turn) {
					char buf[MAXMESSAGE + 1];
					int num_read = read(currentMove->listenfd, &buf, MAXMESSAGE);
					buf[num_read] = '\0';
					if (num_read > 0) {
						choice = atoi(buf);
					} else { // player disconnected
						FD_CLR(currentMove->listenfd, &fds);
						for (struct player * q = head; q->next;  q = q->next) {
							if (q->next->listenfd == currentMove->listenfd) {
								if (q->next->next) {
									q->next = q->next->next;
								} else {
									q->next = NULL;
								}
							}
						}


						printf("%s has left the game.\n", currentMove->name);
						char deletionNotification [MAXMESSAGE];
						snprintf(deletionNotification, MAXMESSAGE + 2, "%s has left the game.\r\n", p->name);
						broadcast(deletionNotification, NULL);
						num_players--;
					}
				}
				else {
					printf("The current player is: %s\n", currentMove->name);
					// check if current move has entered number
					//broadcast the board
					broadcastBoard();

					otherMoveMessage = strdup("It is ");
					strcat(otherMoveMessage, currentMove->name);
					strcat(otherMoveMessage, "'s move.\r\n");
					broadcast(otherMoveMessage, currentMove->name);

					write(currentMove->listenfd, yourMove, sizeof(yourMove));

					if (choice != -1) {
						int pebblesLeft = currentMove->pits[choice];
						currentMove->pits[choice] = 0;

						int pitIndex = choice + 1;
						struct player * x = currentMove;
						while ( pebblesLeft > 0) {
							x->pits[pitIndex]++;
							pebblesLeft--;
							pitIndex++;
							if (pitIndex == NPITS) {
								if (x->next) {
									x = x->next;
								} else {
									x = head;
								}
								pitIndex = 0;
							}
						}
					}

					if (currentMove->next) {
						currentMove-> turn = 0;
						currentMove = currentMove->next;
						currentMove->turn = 1;
					} else {
						currentMove->turn = 0;
						currentMove = head;
						currentMove->turn = 1;

					}
					choice = -1;

				}
			}
		}


		printf("Game over!\n");
		for (p = playerlist; p; p = p->next) {
			int points, i;
			for (points = i = 0; i <= NPITS; i++)
				points += p->pits[i];
			printf("%s has %d points\r\n", p->name, points);
			snprintf(msg, sizeof msg, "%s has %d points", p->name, points);
			broadcast(msg, NULL);
		}


	}
	return 1;

}

struct player * add_player(struct player * p, int player_fd) {
	// send message to player asking for name
	char greeting [MAXMESSAGE] = "Welcome to Mancala. What is your name?\r\n";
	write(player_fd, greeting, sizeof(greeting));

	// get player name's name
	char player_name[MAXNAME + 1];
	int validNameFound = 0;
	int num_read;
	while (validNameFound != 1) {

		num_read = read(player_fd, player_name, MAXNAME - 2);
		player_name[num_read - 1] = '\0';
		validNameFound = checkName(player_name);

		// check if name is in use, if it is then ask again
		if (validNameFound == 0) {
			char tryAgain [MAXMESSAGE] = "That name is already in use, try something else\r\n";
			write(player_fd, tryAgain, sizeof(tryAgain));
		}

	}

	// if player is first player, initailize with 4 pebbles
	if (p == NULL) {
		p = (struct player *) malloc(sizeof(struct player));
		p->listenfd = player_fd;
		p->name = malloc(sizeof(player_name));
		strcpy(p->name, player_name);
		p->turn = 1;
		// set the pits
		for (int i = 0; i < NPITS; i ++) {
			p->pits[i] = 4;
		}
		p->pits[NPITS] = 0;
		head = p;
	} else {  // if player is not first player, compute average pebbles
		int average = compute_average_pebbles();
		p->next = (struct player *) malloc(sizeof(struct player));
		p->next->listenfd = player_fd;
		p->next->name = malloc(sizeof(player_name));
		strcpy(p->next->name, player_name);
		p = p->next;
		p->turn = 0;
		// set the pits
		for (int i = 0; i < NPITS; i++) {
			p->pits[i] = average;
		}
		p->pits[NPITS] = 0;
	}

	// broadcast new player message to server
	printf("%s has joined the game.\n", p->name);
	char * notification = malloc(MAXNAME + 1);
	snprintf(notification, MAXMESSAGE + 2, "%s has joined the game.\r\n", p->name);
	broadcast(notification, NULL);

	return p;
}




int checkName(char * name) {
	if (head == NULL) {
		return 1;
	} else if (strcmp(name, "") == 0) {
		return 0;
	} else {
		struct player * p;
		for (p = head; p; p = p->next) {
			if (strcmp(name, p->name) == 0) {
				return 0;
			}
		}
		return 1;
	}
}


void broadcast(char * s, char * avoid) {
	if (head == NULL) {
		printf("%s\n", "No players at the moment");
	}
	struct player * p;
	if (avoid == NULL) {
		for (p = head; p; p = p->next) {
			// send message to user
			write(p->listenfd, s, strlen(s));
		}
	} else {
		for (p = head; p; p = p->next) {
			// send message if the player isnt meant to be avoided with the message
			if (strcmp(p->name, avoid) != 0) {
				write(p->listenfd, s, strlen(s));
			}
		}
	}

}

void broadcastBoard() {
	if (head) {
		struct player * p;
		for (p = head; p; p = p->next) {
			// create the board
			char * board = strdup("");
			char pitNumber[20];
			char pitValue[20];
			strcat(board, p->name); strcat(board, ":");
			for (int i = 0; i < NPITS; i++) {
				strcat(board, " [");
				sprintf(pitNumber, "%d", i);
				strcat(board, pitNumber); strcat(board, "]");
				sprintf(pitValue, "%d", p->pits[i]);
				strcat(board, pitValue);
			}
			// handle end pit value
			strcat(board, " [end pit]");
			sprintf(pitValue, "%d", p->pits[NPITS]);
			strcat(board, pitValue);
			strcat(board, "\r\n");
			broadcast(board, NULL);
		}

	}
}


void parseargs(int argc, char **argv)
{
	int c, status = 0;
	while ((c = getopt(argc, argv, "p:")) != EOF) {
		switch (c) {
		case 'p':
			port = atoi(optarg);
			break;
		default:
			status++;
		}
	}
	if (status || optind != argc) {
		fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
		exit(1);
	}
}


void makelistener()
{
	struct sockaddr_in r;

	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(1);
	}

	memset(&r, '\0', sizeof r);
	r.sin_family = AF_INET;
	r.sin_addr.s_addr = INADDR_ANY;
	r.sin_port = htons(port);
	if (bind(listenfd, (struct sockaddr *)&r, sizeof r)) {
		perror("bind");
		exit(1);
	};

	if (listen(listenfd, 5)) {
		perror("listen");
		exit(1);
	}

}




int compute_average_pebbles()  /* call this BEFORE linking the new player in to the list */
{
	struct player *p;
	int i;

	if (head == NULL)
		return (4);

	int nplayers = 0, npebbles = 0;
	for (p = head; p; p = p->next) {
		nplayers++;
		for (i = 0; i < NPITS; i++)
			npebbles += p->pits[i];
	}
	return ((npebbles - 1) / nplayers / NPITS + 1); /* round up */
}


int game_is_over() /* boolean */
{
	struct player *p;
	int i;
	if (head == NULL)
		return (0); /* we haven't even started yet! */
	for (p = head; p; p = p->next) {
		int is_all_empty = 1;
		for (i = 0; i < NPITS; i++)
			if (p->pits[i])
				is_all_empty = 0;
		if (is_all_empty)
			return (1);
	}
	return (0);
}
