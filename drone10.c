/* 
 * Final lab for drone messaging
 * File: drone10.c
 * Author: Tristan Langley
 * Date Created: 4/14/22
 * Class: CSE 5462 SP22
 * Instructor: Dave Ogle
 *
 * Program which can both receive messages from the network, and send messages to
 * the IP/port hosts listed in config.txt. These messages are in the format
 * "<v> <loc> <dst port> <src port> <hopct> <msg type> <msg id/seq #> <sendpath> <msg>"
 * for example, "6 8 1818 2020 5 MSG 2 1818,1919 hello world" Message types can be
 * MSG (receive a string), ACK (acknowledge a message), MOV (move to a new location)
 * or LOC (respond with my location). MSG and ACK must be forwarded if I am not the
 * destination. MSGs and ACKs are saved and re-sent at 5s intervals, or when I move.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define STDIN 0
#define MSG_LEN 100
#define MAX_PARTNERS 50
#define STR_LEN 30
#define VERSION 6
#define BUF_LEN 140
#define RANGE 2
#define MAX_HOPS 5
#define MAX_MSG_STORE 20
#define MAX_RESENT_CT 5
#define WAIT_TIME 5

/* Struct definitions for drone partners */
struct _single_partner {
    char ip[STR_LEN];       // IP address
    int port;               // port number
    int loc;                // location on grid
    int send_seq;           // next sequence number to send to this partner
    int last_rcvd_msg_seq;  // last sequence number of a msg received from this partner
    int last_rcvd_ack_seq;  // last sequence number of an ack received from this partner
};
struct _partners {
    int ct_partners;
    struct _single_partner host_info[MAX_PARTNERS];
};

/* Struct definitions for stored messages to be re-sent */
struct _single_msg_sent {
    int is_used;          // whether this entry should be used (if 0, ignore/treat as empty)
    char buf[BUF_LEN];    // the full buffer/string
    int dst;              // destination port number of message
    int src;              // source port number of message
    int seq;              // sequence number/msg id between the given src/dst
    int timestamp;        // when this buffer was last sent/re-sent
    int ct_resent;        // total amount of time this buffer has been re-sent
};
struct _all_msgs_sent {
    int ct_msgs;
    struct _single_msg_sent msg_info[MAX_MSG_STORE];
};

/* Struct definitions for stored messages received out of order */
struct _single_msg_rcvd {
    int is_used;          // whether this entry should be used (if 0, ignore/treat as empty)
    char buf[BUF_LEN];    // the full buffer/string
    int src;              // source port number of message
    int seq;              // sequence number/msg id between the given src/myselfd
};
struct _all_msgs_rcvd {
    int ct_msgs;
    struct _single_msg_rcvd msg_info[MAX_MSG_STORE];
};

/* Enum for message types */
enum msg_types{MSG, ACK, MOV, LOC};

/* Function definitions */
int get_port_num(int, char**);
int get_drone_info(char*, struct _partners*, int, int*, int*, int*);
int store_config(FILE *, struct _partners*, int, int*, int*);
int check_store_host(struct _partners*, int, char[STR_LEN], int, int);
void print_partners(struct _partners);
int find_row(int, int, int);
int find_col(int, int, int);
int get_stdin_msg(int*, char[]);
int within_range(int, int, int, int, int);
int rcvd_already(struct _partners*, int, int, int);
int* next_seq_from_port(struct _partners*, int);
int parse_buf(char*, int*, int*, int*, int*, int*, char*, int*, int*, char*, char*);
int convert_type(char*);
void msg_routine(int, int, int, int, char*, int, int, char*, char*, char[BUF_LEN], int, int,
                struct _partners*, int[MAX_PARTNERS], struct sockaddr_in[MAX_PARTNERS],
                struct _all_msgs_sent*, struct _all_msgs_rcvd*);
void print_msg(int, int, int, int, char*, int, char*, char*);
void send_ack(char[BUF_LEN], int, int, int, int, struct _partners*, struct _all_msgs_sent*,
              int[MAX_PARTNERS], struct sockaddr_in[MAX_PARTNERS]);
void handle_next_msgs(char[BUF_LEN], int, int, int, int*, struct _partners*, struct _all_msgs_sent*,
                      struct _all_msgs_rcvd*, int[MAX_PARTNERS], struct sockaddr_in[MAX_PARTNERS]);
void ack_routine(int, int, int, int, char*, int, int, char*, char*, struct _partners*,
                 struct _all_msgs_sent*);
void update_rcvd_seq(struct _partners*, int, int, int);
int mov_routine(int, int, int, int, char*, int, char*, char*, int*, int*, int*, int, int, 
                int[MAX_PARTNERS], struct sockaddr_in[MAX_PARTNERS], struct _partners*,
                struct _all_msgs_sent*);
void loc_routine(int, int, int, int, char[BUF_LEN], int, int, int[MAX_PARTNERS],
                 struct sockaddr_in[MAX_PARTNERS], struct _partners*);
void forward_routine(int, int, int, int, char*, int, int, char*, int, char*, char[BUF_LEN],
                     struct _partners, int[MAX_PARTNERS], struct sockaddr_in[MAX_PARTNERS],
                     struct _all_msgs_sent*);
int send_buf(char*, int, struct sockaddr_in);
void store_buf_sent(char[BUF_LEN], int, int, int, int, struct _all_msgs_sent*);
void add_single_msg_sent(char[BUF_LEN], int, int, int, int, struct _single_msg_sent*);
void store_buf_rcvd(char[BUF_LEN], int, int, struct _all_msgs_rcvd*);
void add_single_msg_rcvd(char[BUF_LEN], int, int, struct _single_msg_rcvd*);
void print_stored_msgs(struct _all_msgs_sent);
void remove_stored_sent_msg(int, int, int seq, struct _all_msgs_sent*);
void update_loc_in_buf(int, char[BUF_LEN]);
void resend_timeouts(int, int[MAX_PARTNERS], struct sockaddr_in[MAX_PARTNERS],
                     struct _partners*, struct _all_msgs_sent*);
void resend_buf(int[MAX_PARTNERS], struct sockaddr_in[MAX_PARTNERS], struct _partners*,
                struct _single_msg_sent*, int);

/* Main function */
int main(int argc, char **argv)
{
    /* Declare local variables */
    int rc;                            // Return code for functions
    int my_port;                       // Port number/return of get_port_num
    int my_loc, my_row, my_col;        // This program/drone's square, row, and col locations
    char *filename = "config.txt";     // Name of file with server information
    struct _partners drones;           // Contains all partner drones' info (e.g. ip, port, loc)
    int i;                             // Loop variable
    int rows;                          // Number of rows in the location grid
    int cols;                          // Number of cols in the location grid
    struct timeval sel_timeout;        // Time to let select() block before timing out
    //int flag_prompt = 1;               // Whether or not to print "enter message" prompt to user
    int partner_sock_fd[MAX_PARTNERS]; // Partner socket file desciptors
    struct sockaddr_in partner_addr[MAX_PARTNERS];  // Partner address structures
    int my_sock_fd;                    // My socket file desciptor
    struct sockaddr_in my_addr;        // My (server) address structure
    struct sockaddr_in client_addr;    // Client address structure
    int client_addr_len = sizeof(client_addr);  // Needed for function calls
    char msg[MSG_LEN] = {0};           // Stores message to send/receive
    int dst;                           // Destination of message to send
    int *dst_seq_ptr;                  // Message ID/sequence number with destination
    char buffer[BUF_LEN] = {0};        // Stores full protocol string to send/receive
    int rcvd_bytes, rcvd_ver, rcvd_loc, rcvd_dst, rcvd_src, rcvd_hopct, rcvd_seq, rcvd_type_int;
    char rcvd_type_str[3] = {0}, rcvd_sendpath[MSG_LEN];
                                       // Parameter values of message received from network
    struct _all_msgs_sent stored_msgs_sent; // Contains all stored messages' info (e.g. src/dst, seq)
    stored_msgs_sent.ct_msgs = 0;           // 0 messages are stored to begin
    struct _all_msgs_rcvd stored_msgs_rcvd; // Contains all stored messages' info (e.g. src/dst, seq)
    /* Variables for select() */
    fd_set sock_fds;                   // Socket fd SET (not single sock_fd)
    int max_sd;                        // Tells OS num of sockets set (not 64k)

	/* Read command line arguments for port number */
    my_port = get_port_num(argc, argv);
    if (my_port <= 0) {
        printf("Usage is: drone10 <portnumber>\n");
        exit(1);
    }

    /* Parse config file for rows x cols, partner drone info, and my location */
    if (get_drone_info(filename, &drones, my_port, &my_loc, &rows, &cols) < 0)
        return -1;

    /* Determine my square location */
    my_row = find_row(my_loc, rows, cols);
    my_col = find_col(my_loc, rows, cols);
    if (my_row < 0 || my_col < 0)
        printf("I am outside of the %dx%d grid!\n", rows, cols);
    else
        printf("at row/col %d/%d\n", my_row, my_col);

    /* Create datagram socket (file descriptor) for myself */
    if ((my_sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) == 0) {
        perror("Error in socket");
        exit(1);
    }

    /* Properties for address structure */
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;   // Bind to localhost any adapter
    my_addr.sin_port = htons(my_port);      // Port num must be short

    /* Attach my socket to port */
    rc = bind(my_sock_fd, (struct sockaddr *)&my_addr, sizeof(my_addr));
    if (rc < 0) {
        perror("Error in bind");
        exit(1);
    }

    /* Create socket for each server/partner */
    for (i = 0; i < drones.ct_partners; i++) {
        /* Create datagram socket (file descriptor) */
        if ((partner_sock_fd[i] = socket(AF_INET, SOCK_DGRAM, 0)) == 0) {
            perror("Error in socket");
            exit(1);
        }

        /* Properties for address structure */
        partner_addr[i].sin_family = AF_INET;
        partner_addr[i].sin_addr.s_addr = inet_addr(drones.host_info[i].ip);
        partner_addr[i].sin_port = htons(drones.host_info[i].port);
    }

    /* Keep waiting to receive data from stdin and partners */
    while(1) {
        /* Reset msg and buffer to empty */
        fflush(stdout);
        memset(msg, 0, MSG_LEN);
        memset(buffer, 0, BUF_LEN);

        /* Configure sock_fds (SET of file descriptors) for select() */
        FD_ZERO(&sock_fds);            // Zero out structure of sockets
        FD_SET(my_sock_fd, &sock_fds); // Set bit for the initial sock_rd socket
        FD_SET(STDIN, &sock_fds);      // Set bit for stdin (i.e. also look here)

        /* Determine what the max socket descriptor is */
        if (STDIN > my_sock_fd)
            max_sd = STDIN;
        else
            max_sd = my_sock_fd;

        /* Get system time and re-send messages that have waited 5s */
        resend_timeouts(time(NULL), partner_sock_fd, partner_addr, &(drones), &stored_msgs_sent);

        /* Prompt user input from stdin */
        //if (flag_prompt)
        //    printf("Enter <dst port> <msg>:\n");

        /* Select should time out after 2s */
        sel_timeout.tv_sec = 2;
        sel_timeout.tv_usec = 0;
        /* Block until something arrives from either stdin or my_sock_fd, or times out */
        /* Select whether incoming message is from stdin or my_sock_fd */
        rc = select(max_sd + 1, &sock_fds, NULL, NULL, &sel_timeout);
        if (rc < 0) {
            fprintf(stderr, "Error on select(). Exiting\n");
            exit(1);
        } else if (rc == 0) {   // Timed out, move on to next loop iteration
            //flag_prompt = 0;
            continue;
        }
        //flag_prompt = 1;

        /* Depending on if from stdin or network, act accordingly */
        if (FD_ISSET(STDIN, &sock_fds)) {
            /* Read message from user input */
            if (get_stdin_msg(&dst, msg) < 0) {
                printf("Incorrect format, correct e.g. \"1818 Hello world!\" without \"\"\n");
                continue;
            }

            /* Get the sequence number for the destination port */
            if ((dst_seq_ptr = next_seq_from_port(&drones, dst)) == NULL) {
                printf("Destination provided was not given in config file. Ignoring message.\n");
                continue;
            }
            
            /* Put the message into a protocol-following string */
            sprintf(buffer, "%d %d %d %d %d %s %d %d %s", VERSION, my_loc, dst, my_port,
                    MAX_HOPS, "MSG", *dst_seq_ptr, my_port, msg);
            /* Store the message */
            store_buf_sent(buffer, dst, my_port, *dst_seq_ptr, time(NULL), &stored_msgs_sent);
            /* Try to send message into each server socket, serially */
            for (i = 0; i < drones.ct_partners; i++)
                send_buf(buffer, partner_sock_fd[i], partner_addr[i]);
        }

        if (FD_ISSET(my_sock_fd, &sock_fds)) {
            /* Read message using recvfrom() */
            rcvd_bytes = recvfrom(my_sock_fd, buffer, sizeof(buffer), 0,
                                  (struct sockaddr *)&client_addr,
                                  (socklen_t*)&client_addr_len);
            if (rcvd_bytes < 0) {
                fprintf(stderr, "Error on recvfrom(). Exiting\n");
                exit(1);
            }

            /* Parse into received parameter variables */
            rc = parse_buf(buffer, &rcvd_ver, &rcvd_loc, &rcvd_dst, &rcvd_src, &rcvd_hopct,
                           rcvd_type_str, &rcvd_type_int, &rcvd_seq, rcvd_sendpath, msg);
            if (rc < 0) {
                printf("Received a message that violates version %d protocol. Ignoring\n", VERSION);
                continue;
            }

            /* If came from out of range, and a MSG or ACK type, ignore and move on */
            if ((rcvd_type_int == MSG || rcvd_type_int == ACK) && !within_range(rcvd_loc, my_row, my_col, rows, cols)) {
                //flag_prompt = 0;
                continue;
            }

            /* Determine if it's for me; if so perform routine, if not forward msg/ack or ignore mov */
            if (rcvd_dst == my_port) {
                /* Ignore message/ack if it was received already */
                if (rcvd_already(&drones, rcvd_src, rcvd_seq, rcvd_type_int)) {
                    //flag_prompt = 0;
                    continue;
                }

                //printf("\nRECEIVED: %s", buffer);

                switch (rcvd_type_int) {
                    case MSG:
                        msg_routine(rcvd_loc, rcvd_dst, rcvd_src, rcvd_hopct, rcvd_type_str,
                                    rcvd_type_int, rcvd_seq, rcvd_sendpath, msg, buffer, my_loc,
                                    my_port, &drones, partner_sock_fd, partner_addr,
                                    &stored_msgs_sent, &stored_msgs_rcvd);
                        break;
                    case ACK:
                        ack_routine(rcvd_loc, rcvd_dst, rcvd_src, rcvd_hopct, rcvd_type_str,
                                    rcvd_type_int, rcvd_seq, rcvd_sendpath, msg, &drones,
                                    &stored_msgs_sent);
                        break;
                    case MOV:
                        mov_routine(rcvd_loc, rcvd_dst, rcvd_src, rcvd_hopct, rcvd_type_str, rcvd_seq,
                                    rcvd_sendpath, msg, &my_loc, &my_row, &my_col, rows, cols,
                                    partner_sock_fd, partner_addr, &drones, &stored_msgs_sent);
                        break;
                    case LOC:
                        loc_routine(rcvd_dst, rcvd_src, rcvd_hopct, rcvd_seq, buffer, my_loc, my_port,
                                    partner_sock_fd, partner_addr, &drones);
                        break;
                    default:
                        break;
                }
            } else if ((rcvd_type_int == MSG || rcvd_type_int == ACK) && rcvd_hopct > 0) {
                forward_routine(my_loc, rcvd_dst, rcvd_src, rcvd_hopct, rcvd_type_str, rcvd_type_int,
                                rcvd_seq, rcvd_sendpath, my_port, msg, buffer, drones,
                                partner_sock_fd, partner_addr, &stored_msgs_sent);
                //flag_prompt = 0;
            }
        }

    }

	return 0;
}


/* Returns port number to bind to if correctly provided as command line arg;
 * Returns -1 otherwise. */
int get_port_num(int argc, char **argv)
{
    if (argc != 2) return -1;   // Must be 2 args: ./drone9 <portnumber>

    int pn = atoi(argv[1]);     // Get port number from first arg

    if (pn <= 1024 || pn > 64000) {  // 0-1024 reserved, 64k max
        printf("Port number must be between 1025 and 64,000\n");
        return -1;                // Note: atoi gives 0 if not int
    }
                                            
    return pn;
}


/* Top-level function to handle parsing config file to populate struct.
 * Returns 0 if parsed correctly, -1 if was unable to parse */
int get_drone_info(char *filename, struct _partners *partners, int my_port, int *loc_ptr,
                    int *r_ptr, int *c_ptr)
{
    /* Declare local variables */
    FILE *in;     // File stream with server information

    /* Open config.txt for reading */
    in = fopen(filename, "r");
    if (in == NULL) {
        perror("Error in fopen");
        exit(1);
    }

    /* Read config.txt for ip addresses, port numbers, locations of partners */
    *loc_ptr = store_config(in, partners, my_port, r_ptr, c_ptr);
    fclose(in);

    /* Print my location to terminal */
    if (*loc_ptr == 0) {
        printf("My location is not in config.txt! I am lost or otherwise not running.\n");
    } else if (*loc_ptr == -1) {
        fprintf(stderr, "Config file formatted incorrectly");
        return -1;
    } else {
        printf("My location is square %d ", *loc_ptr);
    }

    return 0;
}


/* Reads server IP/ports/locations from input stream and stores them in a struct.
 * Returns the location of this program/drone, based on port number.
 * Returns 0 if no location is provided for this port number/drone.
 * Returns -1 if config file was formatted incorrectly */
int store_config(FILE *in, struct _partners *partners, int my_port, int *r_ptr, int *c_ptr)
{
    int row = 0;                   // Keep track of line number of file
    int idx_partner = 0;           // Keep track of current index of partner within struct
    char line_str[MSG_LEN];        // Temporary string to store entire line
    char temp_ip[STR_LEN] = {0};   // String to temporarily store ip address
    int temp_port;                 // Integer to temporarily store port number
    int temp_loc;                  // Integer to temporarily store location
    int my_loc = 0;                // This drone's location; default is 0 (unknown)

    /* Clear memory in struct for first IP address */
    memset(partners->host_info[0].ip, 0, STR_LEN);

    /* First line of file is <rows> <cols> */
    fgets(line_str, MSG_LEN, in);
    if (sscanf(line_str, "%d %d", r_ptr, c_ptr) < 2)
        return -1;

    /* Populate second entry of struct with IP/port pairs in config.txt */
    /* Read in line by line until reaches max number of partners allowed */
    while (fgets(line_str, MSG_LEN, in) != NULL && idx_partner < MAX_PARTNERS) {
        /* Parse line using sscanf() */
        if (sscanf(line_str, "%s %d %d", temp_ip, &temp_port, &temp_loc) < 3)
            return -1;
        /* Use port num to determine if line describes this drone, or a partner drone */
        if (temp_port == my_port) {
            /* The location on this line is this drone's location */
            my_loc = temp_loc;
        } else {
            /* Store drone info in the struct, if port and location are valid */
            if (check_store_host(partners, idx_partner, temp_ip, temp_port, temp_loc) == 0) {
                /* Stored properly, so move on to next index */
                idx_partner++;
                /* Reset temp_ip to empty */
                memset(temp_ip, 0, STR_LEN);
                /* Clear memory for next IP address */
                if (idx_partner < MAX_PARTNERS)
                    memset(partners->host_info[idx_partner].ip, 0, STR_LEN);
            } else {
                printf("Row %d of config skipped, invalid port or location.\n", row + 1);
            }
        }
        /* Move on to next row */
        row++;
	}

    /* First entry of struct is the amount of rows read from config.txt */
    partners->ct_partners = idx_partner;

    /* Check if read in max allowed partners */
    if (idx_partner == MAX_PARTNERS)
        printf("Read the max number of partners, which is %d.\n", MAX_PARTNERS);

    return my_loc;
}


/* Checks whether port and location fit within pre-defined ranges, and stores in struct.
 * Returns 0 if stored properly; returns -1 if invalid port or location number. */
int check_store_host(struct _partners *partners, int i, char ip[STR_LEN], int port, int loc)
{
    /* Check port number: 0-1024 reserved, 64k max */
    if (port <= 1024 || port > 64000) {
        printf("Invalid port: %d. Must be between 1025 and 64,000\n", port);
        return -1;
    }

    /* Check location: 1-256 */
    if (loc < 1 || loc > 256) {
        printf("Invalid location: %d. Must be between 1 and 256\n", loc);
        return -1;
    }

    /* If code makes it here, port and location are valid. Store info in struct */
    strcpy(partners->host_info[i].ip, ip);
    partners->host_info[i].port = port;
    partners->host_info[i].loc = loc;
    /* All sequence numbers start at 0 */
    partners->host_info[i].send_seq = 0;
    partners->host_info[i].last_rcvd_msg_seq = 0;
    partners->host_info[i].last_rcvd_ack_seq = 0;

    return 0;
}


/* Prints the list of ip, port, locations in the struct */
void print_partners(struct _partners partners)
{
    int i;
    for (i = 0; i < partners.ct_partners; i++) {
        printf("Drone %d: ", i);
        printf("IP address %s, ", partners.host_info[i].ip);
        printf("port %d, ", partners.host_info[i].port);
        printf("location %d\n", partners.host_info[i].loc);
    }
}

/* Returns the row number that a given square index is in. Returns -1 if outside grid. */
int find_row(int i, int rows, int cols)
{
    if (i < 1)
        return -1;

    if (i > rows * cols)
        return -1;

    return (i - 1) / cols + 1;
}


/* Returns the column number that a given square index is in. Returns -1 if outside grid. */
int find_col(int i, int rows, int cols)
{
    if (i < 1)
        return -1;

    if (i > rows * cols)
        return -1;

    return (i - 1) % cols + 1;
}


/* Gets message to send to server from user input. Copies input into msg string. 
 * Returns -1 if unsuccessful, 0 if successful */
int get_stdin_msg(int *dst, char msg[MSG_LEN])
{
    char input[MSG_LEN] = {0};  // Temporary string for user input
    int og_len;                 // Original length of input string
    int n = 0;                  // Number of characters the ver and loc take up in buf
                                // Used to offset input to get just the msg at the end

    /* Check that the input was in the format "<int> <message>" */
    fgets(input, sizeof(input), stdin);
    if (sscanf(input, "%d %n", dst, &n) != 1)
        return -1;

    /* Check dest port number: 0-1024 reserved, 64k max */
    if (*dst <= 1024 || *dst > 64000) {
        printf("Invalid port: %d. Must be between 1025 and 64,000\n", *dst);
        return -1;
    }

    /* Copy user input into msg string */
    strcpy(msg, input + n);

    /* Some extra handling to get rid of newline */
    og_len = strlen(msg);
    if (msg[og_len-1] == '\n')
        msg[og_len-1] = '\0';

    /* Inform user of what will be send to the server */
    printf("You are sending the message '%s' to drone %d\n",
           msg, *dst);

    return 0;
}


/* Determines if a square location is within the range of this drone's location. 
 * Returns 1 for in range, 0 for out of range */
int within_range(int rcvd_loc, int my_r, int my_c, int rows, int cols)
{
    /* Determine the other drone's row/col location */
    int rcvd_r = find_row(rcvd_loc, rows, cols);
    int rcvd_c = find_col(rcvd_loc, rows, cols);

    /* Are points within range of each other? Use pythagorean theorem */
    int dist = sqrt( pow(abs(rcvd_r - my_r), 2) + pow(abs(rcvd_c - my_c), 2) );
    if (my_r < 0 || my_c < 0) {             // Out of range if I am outside grid
        return 0;
    } else if (rcvd_r < 0 || rcvd_c < 0) {  // Out of range if other drone is outside grid
        return 0;
    } else if (dist > RANGE ) {
        return 0;
    }

    return 1;
}


/* Determine if this message/ack has been received already. Returns 1 if yes, 0 if no.
 * Returns 0 for MOV, because not relevant */
int rcvd_already(struct _partners *partners, int port, int seq, int type)
{
    int i;
    for (i = 0; i < partners->ct_partners; i++) {
        if (partners->host_info[i].port == port) {
            /* Compare to proper sequence number stored in struct */
            if (type == MSG) {
                if (partners->host_info[i].last_rcvd_msg_seq >= seq) {
                    return 1;
                }
            } else if (type == ACK) {
                if (partners->host_info[i].last_rcvd_ack_seq >= seq) {
                    return 1;
                }
            }
            return 0;
        }
    }
    return 0;
}


/* Get a pointer to the msg ID/sequence number for the given port
 * Returns null if no such port exists */
int* next_seq_from_port(struct _partners *partners, int port)
{
    int i;
    for (i = 0; i < partners->ct_partners; i++) {
        if (partners->host_info[i].port == port) {
            partners->host_info[i].send_seq++;
            return &(partners->host_info[i].send_seq);
        }
    }

    /* If there is no sequence number matching the port give, return null */
    return NULL;
}


/* Populates parameters based on buffer contents and pre-defined protocol
 * Returns -1 if it does not parse properly, 0 if it did */
int parse_buf(char *buf, int *ver, int *loc, int *dst, int *src, int *hopct, char *type_str,
              int *type_int, int *seq, char *sendpath, char *msg)
{
    /* Declare local variables */
    int og_len;      // Original length of message string
    int param_ct;    // Number of paramters read from sscanf
    int n = 0;       // Number of characters the ver and loc take up in buf
                     // Used to offset buf to get just the msg at the end

    /* Some extra handling to get rid of newline */
    og_len = strlen(buf);
    if (buf[og_len-1] == '\n')
        buf[og_len-1] = '\0';

    /* Read in to parameters */
    param_ct = sscanf(buf, "%d %d %d %d %d %s %d %s %n", ver, loc, dst, src, hopct,
                      type_str, seq, sendpath, &n);
    if (param_ct != 8)
        return -1;
    strcpy(msg, buf + n);

    if (*ver != VERSION)
        return -1;

    /* Decrement hopcount */
    (*hopct)--;

    /* Populate *type_int by converting string to enum of type */
    *type_int = convert_type(type_str);
    if (*type_int < 0)
        return -1;

    return 0;
}


/* Return enum value of message type string; -1 if none of the options */
int convert_type(char *type_str)
{
    if (strlen(type_str) == 3 && type_str[0] == 'M' && type_str[1] == 'S' && type_str[2] == 'G')
        return MSG;
    else if (strlen(type_str) == 3 && type_str[0] == 'A' && type_str[1] == 'C' && type_str[2] == 'K')
        return ACK;
    else if (strlen(type_str) == 3 && type_str[0] == 'M' && type_str[1] == 'O' && type_str[2] == 'V')
        return MOV;
    else if (strlen(type_str) == 3 && type_str[0] == 'L' && type_str[1] == 'O' && type_str[2] == 'C')
        return LOC;
    return -1;
}


/* Routine for a MSG received for me */
void msg_routine(int loc, int dst, int src, int hopct, char *type_str, int type_int, int seq,
                 char *sendpath, char *msg, char buf[BUF_LEN], int my_loc, int my_port,
                 struct _partners *partners, int partner_sock_fd[MAX_PARTNERS],
                 struct sockaddr_in partner_addr[MAX_PARTNERS],
                 struct _all_msgs_sent *stored_msgs_sent, struct _all_msgs_rcvd *stored_msgs_rcvd)
{
    /* Determine if message is in order or out of order */
    int i;
    for (i = 0; i < partners->ct_partners; i++) {
        if (partners->host_info[i].port == src) {
            /* If in order, received seq should be the last seq received + 1 */
            if (seq == partners->host_info[i].last_rcvd_msg_seq + 1) {
                /* Print the received msg */
                print_msg(loc, dst, src, hopct, type_str, seq, sendpath, msg);
                /* Send an ACK */
                send_ack(buf, src, seq, my_loc, my_port, partners, stored_msgs_sent, 
                         partner_sock_fd, partner_addr);
                /* Update last received msg seq # for this partner struct */
                partners->host_info[i].last_rcvd_msg_seq++;
                /* Print other stored messaged received, if next in order */
                handle_next_msgs(buf, src, my_loc, my_port, &(partners->host_info[i].last_rcvd_msg_seq),
                                 partners, stored_msgs_sent, stored_msgs_rcvd, partner_sock_fd,
                                 partner_addr);
                return;
            } else if (seq > partners->host_info[i].last_rcvd_msg_seq + 1) {
                store_buf_rcvd(buf, src, seq, stored_msgs_rcvd);
                return;
            } else {
                /* This should not happen */
                printf("Seq # received was too low: seq %d from %d\n", seq, src);
            }
        }
    }
}


/* Print the message info provided as arguments */
void print_msg(int l, int d, int s, int h, char *t, int q, char *p, char *m)
{
    printf("\nMSG FOR ME:\n");
    printf("    location %d; dest port %d; src port %d; hopcount %d; msg type %s;\n", l, d, s, h, t);
    printf("    msgID/seq# %d; sendpath %s; msg '%s'\n", q, p, m);
}


/* Send an ACK in response to the msg received */
void send_ack(char buf[BUF_LEN], int src, int seq, int my_loc, int my_port,
         struct _partners *partners, struct _all_msgs_sent *stored_msgs_sent, 
         int partner_sock_fd[MAX_PARTNERS], struct sockaddr_in partner_addr[MAX_PARTNERS])
{
    /* Reset buffer to empty */
    fflush(stdout);
    memset(buf, 0, BUF_LEN);

    /* Create ACK to send back to src */
    sprintf(buf, "%d %d %d %d %d %s %d %d %s", VERSION, my_loc, src, my_port,
            MAX_HOPS, "ACK", seq, my_port, "ack");

    /* Store ACK in stored sent messages struct */
    store_buf_sent(buf, src, my_port, seq, time(NULL), stored_msgs_sent);

    /* Try to send ack into each server socket, serially */
    int i;
    for (i = 0; i < partners->ct_partners; i++)
        send_buf(buf, partner_sock_fd[i], partner_addr[i]);
}


/* Look through the received messages stored in the struct. Print and remove them if they are up next.
 * Update the last received msg sequence number in the partners struct */
void handle_next_msgs(char buf[BUF_LEN], int src, int my_loc, int my_port, int *last_seq,
                      struct _partners *partners, struct _all_msgs_sent *stored_msgs_sent,
                      struct _all_msgs_rcvd *stored_msgs_rcvd, int partner_sock_fd[MAX_PARTNERS],
                      struct sockaddr_in partner_addr[MAX_PARTNERS])
{
    /* Declare local variables */
    int i, v, l, d, s, h, q, n;
    char t[3], p[MSG_LEN], m[MSG_LEN];

    /* Look for entries that are "next in line" from that source to be printed */
    int last_msg = stored_msgs_rcvd->ct_msgs;   // Max index for the loop
    for (i = 0; i < last_msg; i++) {

        if (stored_msgs_rcvd->msg_info[i].is_used == 1 && stored_msgs_rcvd->msg_info[i].src == src
            && stored_msgs_rcvd->msg_info[i].seq == *last_seq + 1) {

            /* Read in fields from message buffer and print message */
            sscanf(stored_msgs_rcvd->msg_info[i].buf, "%d %d %d %d %d %s %d %s %n",
                   &v, &l, &d, &s, &h, t, &q, p, &n);
            strcpy(m, stored_msgs_rcvd->msg_info[i].buf + n);
            print_msg(l, d, s, h, t, q, p, m);
            /* Remove this stored message from the struct */
            stored_msgs_rcvd->msg_info[i].is_used = 0;
            /* Increment the last received sequence number that is recorded */
            (*last_seq)++;
            /* Send an ACK for that message */
            send_ack(buf, src, *last_seq, my_loc, my_port, partners, stored_msgs_sent,
                     partner_sock_fd, partner_addr);

        }

    }
}


/* Routine for an ACK received for me */
void ack_routine(int loc, int dst, int src, int hopct, char *type_str, int type_int,
                 int seq, char *sendpath, char *msg, struct _partners *partners,
                 struct _all_msgs_sent *stored_msgs_sent)
{
    /* Print ack message info */
    printf("\nACK FOR ME:\n");
    printf("    location %d; dest port %d; src port %d; hopcount %d; msg type %s;\n",
            loc, dst, src, hopct, type_str);
    printf("    msgID/seq# %d; sendpath %s; msg '%s'\n", seq, sendpath, msg);

    /* Update last received ack seq # for this partner struct */
    update_rcvd_seq(partners, src, seq, type_int);

    /* Remove this message from the stored sent messages struct */
    remove_stored_sent_msg(dst, src, seq, stored_msgs_sent);
}


/* Updated the partners struct with last received seq # */
void update_rcvd_seq(struct _partners *partners, int port, int seq, int type)
{
    int i;
    for (i = 0; i < partners->ct_partners; i++) {
        if (partners->host_info[i].port == port) {
            /* Check whether msg or ack to compare to proper sequence number stored in struct */
            if (type == MSG) {
                partners->host_info[i].last_rcvd_msg_seq = seq;
            } else if (type == ACK) {
                partners->host_info[i].last_rcvd_ack_seq = seq;
            }
        }
    }
}


/* Routine for a MOV received for me */
int mov_routine(int loc, int dst, int src, int hopct, char *type_str, int seq, char *sendpath,
                char *msg, int *my_loc, int *my_row, int *my_col, int rows, int cols,
                int partner_sock_fd[MAX_PARTNERS], struct sockaddr_in partner_addr[MAX_PARTNERS],
                struct _partners *partners, struct _all_msgs_sent *stored_msgs_sent)
{
    /* Print mov message info */
    //printf("\nMOV FOR ME: ");
    //printf("    location %d; dest port %d; src port %d; hopcount %d; msg type %s;\n",
    //        loc, dst, src, hopct, type_str);
    //printf("    msgID/seq# %d; sendpath %s; msg '%s'\n", seq, sendpath, msg);

    /* Get new location from msg parameter */
    int new_loc = atoi(msg);
    if (find_row(new_loc, rows, cols) < 0 || find_col(new_loc, rows, cols) < 0) {
        //printf("new location %d is out of grid bounds. I am staying at %d.\n", new_loc, *my_loc);
        return -1;
    }

    /* Move to new location */
    *my_loc = new_loc;
    *my_row = find_row(*my_loc, rows, cols);
    *my_col = find_col(*my_loc, rows, cols);
    //printf("moved to location %d at row/col = %d/%d\n", *my_loc, *my_row, *my_col);

    /* Resend all my stored messages */
    int i;
    int last_msg = stored_msgs_sent->ct_msgs;   // Max index for the loop
    for (i = 0; i < last_msg; i++) {
        if (stored_msgs_sent->msg_info[i].is_used == 1) {
            update_loc_in_buf(*my_loc, stored_msgs_sent->msg_info[i].buf);
            resend_buf(partner_sock_fd, partner_addr, partners, &(stored_msgs_sent->msg_info[i]), i);
        }
    }

    return 0;
}


/* Routine to respond to a LOC request */
void loc_routine(int dst, int src, int hopct, int seq, char buf[BUF_LEN], int my_loc, int my_port,
                 int partner_sock_fd[MAX_PARTNERS], struct sockaddr_in partner_addr[MAX_PARTNERS],
                 struct _partners *partners)
{
    /* Reset buffer to empty */
    memset(buf, 0, BUF_LEN);

    /* Update buffer as response to request. Note: old src is new destination */
    sprintf(buf, "%d %d %d %d %d %s %d %d %d", VERSION, my_loc, src, dst, hopct, "LOC", 
                                               seq, my_port, my_loc);

    /* Send buffer to all drones TODO all or only dest? */
    int i;
    for (i = 0; i < partners->ct_partners; i++) {
        if (partners->host_info[i].port == src)
            send_buf(buf, partner_sock_fd[i], partner_addr[i]);
    }

}


/* Routine to forward a MSG or ACK */
void forward_routine(int my_loc, int dst, int src, int hopct, char *type_str, int type_int, int seq,
                     char *sendpath, int my_port_num, char *msg, char buf[BUF_LEN],
                     struct _partners drones, int partner_sock_fd[MAX_PARTNERS],
                     struct sockaddr_in partner_addr[MAX_PARTNERS],
                     struct _all_msgs_sent *stored_msgs_sent)
{
    /* Reset buffer to empty */
    fflush(stdout);
    memset(buf, 0, BUF_LEN);

    /* Update sendpath with correct placement of commas */
    char new_sendpath[MSG_LEN] = {0};
    if (*(sendpath + strlen(sendpath) - 1) == ',')
        sprintf(new_sendpath, "%s%d", sendpath, my_port_num);
    else
        sprintf(new_sendpath, "%s,%d", sendpath, my_port_num);

    /* Update buffer with location, sendpath */
    sprintf(buf, "%d %d %d %d %d %s %d %s %s", VERSION, my_loc, dst, src,
            hopct, type_str, seq, new_sendpath, msg);
    //printf("FORWARDING: %s\n", buf);

    /* Store the message */
    if (type_int == MSG) {
        store_buf_sent(buf, dst, src, seq, time(NULL), stored_msgs_sent);
    } else if (type_int == ACK) {
        /* If forwarding an ACK, remove the stored MSG and store the ACK */
        remove_stored_sent_msg(dst, src, seq, stored_msgs_sent);
        store_buf_sent(buf, dst, src, seq, time(NULL), stored_msgs_sent);
    }

    /* Try to send buffer into partner sockets that are not already in the sendpath */
    int i;                          // for loop index
    char curr_port_str[5] = {0};    // current port sending buffer to in loop
    for (i = 0; i < drones.ct_partners; i++) {
        sprintf(curr_port_str, "%d", drones.host_info[i].port);
        /* Only send if the current partner's port is not a substring of the sendpath */
        if (strstr(new_sendpath, curr_port_str) == NULL)
            send_buf(buf, partner_sock_fd[i], partner_addr[i]);
    }
}


/* Sends string message to the partner/server specified. Returns 0 if successful, -1 if not */
int send_buf(char *buf, int sd, struct sockaddr_in serv_addr)
{
    int rc;  // Return code for send function

    /* Try to send message into socket */
    rc = sendto(sd, buf, strlen(buf), 0, (struct sockaddr *)&serv_addr,
                sizeof(serv_addr));
    if (rc < 0) {
        perror("Error in sendto");
        printf("Check IP address provided\n");
        return -1;
    }

    return 0;
}


/* Store the sent buffer and its info in the stored messages struct */
void store_buf_sent(char buf[BUF_LEN], int dst, int src, int seq, int timestamp,
                    struct _all_msgs_sent *stored_msgs_sent)
{
    /* Declare local variables */
    int i;                                      // Loop index variable
    int last_msg = stored_msgs_sent->ct_msgs;   // Max index for the loop

    /* If there is an unused slot in the middle of the struct array, use it for this message */
    for (i = 0; i < last_msg; i++) {
        if (stored_msgs_sent->msg_info[i].is_used == 0) {
            add_single_msg_sent(buf, dst, src, seq, timestamp, &(stored_msgs_sent->msg_info[i]));
            //printf("ADDED ENTRY %d: %s\n", i, stored_msgs_sent->msg_info[i].buf);
            return;
        } else {
            /* Do not store this message if it is already stored; but, update the timestamp */
            if (stored_msgs_sent->msg_info[i].dst == dst && stored_msgs_sent->msg_info[i].src == src
                && stored_msgs_sent->msg_info[i].seq == seq) {
                stored_msgs_sent->msg_info[i].timestamp = timestamp;
                return;
            }
        }
    }

    /* If there was no unused slot, append to the end of the array, if there is room */
    if (last_msg < MAX_MSG_STORE) {
        (stored_msgs_sent->ct_msgs)++;
        add_single_msg_sent(buf, dst, src, seq, timestamp, &(stored_msgs_sent->msg_info[last_msg]));
        //printf("ADDED ENTRY %d: %s\n", i, stored_msgs_sent->msg_info[last_msg].buf);
    }
}


/* Store arguments into a single message info struct */
void add_single_msg_sent(char buf[BUF_LEN], int dst, int src, int seq, int timestamp,
                         struct _single_msg_sent *single_msg_sent)
{
    single_msg_sent->is_used = 1;
    strcpy(single_msg_sent->buf, buf);
    single_msg_sent->dst = dst;
    single_msg_sent->src = src;
    single_msg_sent->seq = seq;
    single_msg_sent->timestamp = timestamp;
    single_msg_sent->ct_resent = 0;
}


/* Store the received buffer and its info in the stored messages struct */
void store_buf_rcvd(char buf[BUF_LEN], int src, int seq, struct _all_msgs_rcvd *stored_msgs_rcvd)
{
    /* Declare local variables */
    int i;                                      // Loop index variable
    int last_msg = stored_msgs_rcvd->ct_msgs;   // Max index for the loop

    /* If there is an unused slot in the middle of the struct array, use it for this message */
    for (i = 0; i < last_msg; i++) {
        if (stored_msgs_rcvd->msg_info[i].is_used == 0) {
            add_single_msg_rcvd(buf, src, seq, &(stored_msgs_rcvd->msg_info[i]));
            return;
        }
    }

    /* If there was no unused slot, append to the end of the array, if there is room */
    if (last_msg < MAX_MSG_STORE) {
        (stored_msgs_rcvd->ct_msgs)++;
        add_single_msg_rcvd(buf, src, seq, &(stored_msgs_rcvd->msg_info[last_msg]));
    }
}


/* Store arguments into a single message info struct */
void add_single_msg_rcvd(char buf[BUF_LEN], int src, int seq, struct _single_msg_rcvd *single_msg_rcvd)
{
    single_msg_rcvd->is_used = 1;
    strcpy(single_msg_rcvd->buf, buf);
    single_msg_rcvd->src = src;
    single_msg_rcvd->seq = seq;
}


/* Print all the stored messages/buffers */
void print_stored_msgs_sent(struct _all_msgs_sent stored_msgs_sent)
{
    /* Declare local variables */
    int i;                                // Loop index variable
    int last_msg = stored_msgs_sent.ct_msgs;   // Max index for the loop

    printf("MY STORED MESSAGES:\n");
    for (i = 0; i < last_msg; i++) {
        if (stored_msgs_sent.msg_info[i].is_used == 1) {
            printf(" %d  buf: %s\n", i, stored_msgs_sent.msg_info[i].buf);
            printf("    dst %d; src %d; seq# %d; timestamp %d; ct_resent %d\n\n",
                   stored_msgs_sent.msg_info[i].dst, stored_msgs_sent.msg_info[i].src,
                   stored_msgs_sent.msg_info[i].seq, stored_msgs_sent.msg_info[i].timestamp,
                   stored_msgs_sent.msg_info[i].ct_resent);
        }
    }
}


/* Remove (i.e. designate as unused) the single struct entry with the given src/dst and seq# */
void remove_stored_sent_msg(int src, int dst, int seq, struct _all_msgs_sent *stored_msgs)
{
    /* Declare local variables */
    int i;                                 // Loop index variable
    int last_msg = stored_msgs->ct_msgs;   // Max index for the loop

    /* Look through all entries for the matching src/dst/seq */
    for (i = 0; i < last_msg; i++) {
        if (stored_msgs->msg_info[i].is_used == 1) {
            /* If entry matches, designate it now as unused */
            if (stored_msgs->msg_info[i].dst == dst && stored_msgs->msg_info[i].src == src
                && stored_msgs->msg_info[i].seq == seq) {
                //printf("REMOVED ENTRY %d acked: %s\n", i, stored_msgs_sent->msg_info[i].buf);
                stored_msgs->msg_info[i].is_used = 0;
                return;
            }
        }
    }
}


/* Replace location in buffer with new location provided */
void update_loc_in_buf(int new_loc, char buf[BUF_LEN])
{
    /* Declare local variables */
    int v, old_loc, d, s, h, q, n;
    char t[3], p[MSG_LEN], m[MSG_LEN];

    sscanf(buf, "%d %d %d %d %d %s %d %s %n", &v, &old_loc, &d, &s, &h, t, &q, p, &n);
    strcpy(m, buf + n);

    sprintf(buf, "%d %d %d %d %d %s %d %s %s", v, new_loc, d, s, h, t, q, p, m);
}


/* Resend the stored messaged whose timestamp is more than 5s older than the current time */
void resend_timeouts(int curr_time, int partner_sock_fd[MAX_PARTNERS],
                     struct sockaddr_in partner_addr[MAX_PARTNERS], struct _partners *partners,
                     struct _all_msgs_sent *stored_msgs_sent)
{
    /* Declare local variables */
    int i;                                 // Loop index variable
    int last_msg = stored_msgs_sent->ct_msgs;   // Max index for the loop

    /* Look through all entries for any that have been waiting over 5s */
    for (i = 0; i < last_msg; i++) {
        if (stored_msgs_sent->msg_info[i].is_used == 1) {
            /* If entry matches, designate it now as unused */
            if (curr_time - stored_msgs_sent->msg_info[i].timestamp >= WAIT_TIME) {
                resend_buf(partner_sock_fd,partner_addr, partners, &(stored_msgs_sent->msg_info[i]), i);
            }
        }
    }
}


/* Resend a single buffer/message */
void resend_buf(int partner_sock_fd[MAX_PARTNERS], struct sockaddr_in partner_addr[MAX_PARTNERS],
                struct _partners *partners, struct _single_msg_sent *single_msg_sent, int idx)
{
    /* Declare local variables */
    int i;                          // for loop index
    char curr_port_str[5] = {0};    // current port sending buffer to in loop

    /* Determine the sendpath to know which ports to not re-send to */
    int v, l, d, s, h, q, n;   // Unused vars
    char t[3], sendpath[MSG_LEN];         // Only care about the sendpath
    sscanf(single_msg_sent->buf, "%d %d %d %d %d %s %d %s %n", &v, &l, &d, &s, &h, t, &q, sendpath, &n);

    /* Update timestamp for most recent sent time */
    single_msg_sent->timestamp = time(NULL);

    /* Try to send buffer into partner sockets that are not already in the sendpath */
    for (i = 0; i < partners->ct_partners; i++) {
        sprintf(curr_port_str, "%d", partners->host_info[i].port);
        /* Only send if the current partner's port is not a substring of the sendpath */
        if (strstr(sendpath, curr_port_str) == NULL)
            send_buf(single_msg_sent->buf, partner_sock_fd[i], partner_addr[i]);
    }

    /* Update count_sent; if it is now MAX_SENT_CT, remove it (mark it as unused) */
    if (++(single_msg_sent->ct_resent) >= MAX_RESENT_CT) {
        //printf("REMOVED ENTRY %d re-sent 5 times: %s\n", idx, single_msg_sent->buf);
        single_msg_sent->is_used = 0;
    }
}

