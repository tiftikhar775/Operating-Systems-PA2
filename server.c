#include <math.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// represents a letter and its probability, it is communicated by the client
typedef struct {
    int i;
    char letter;
    double probability;
} LetterMessage;

typedef struct {
    size_t bits;
    size_t n;
} LetterReply;

// prints error message with erroneous system call, and exits program
void error(char const *msg) {
    perror(msg);
    exit(1);
}

// given a double number, returns its n most significant bits as an unsigned integer
LetterReply getMostSignificantBits(double d, size_t n) {
    LetterReply result = {0, n};
    result.bits = 0;
    result.n = n;
    for (size_t i = 1; i <= n; ++i) {
        double p = 1.0 / (1 << i);
        if (d >= p) {
            d -= p;
            result.bits |= 1;
        }
        result.bits <<= 1;
    }
    return result;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Error: no port provided\n");
        exit(1);
    }

    // sets up the socket with information from the cli args
    int sockfd = socket(AF_INET, SOCK_STREAM, 0), portno = atoi(argv[1]),
        clilen = sizeof(struct sockaddr_in);
    if (sockfd < 0) {
        error("error opening socket");
    }
    struct sockaddr_in serv_addr;
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        error("error on socket binding");
    }
    listen(sockfd, 256);

    while (1) {
        struct sockaddr_in cli_addr;
        // accept a client connection
        int newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr,
                               (socklen_t *) &clilen);
        if (newsockfd < 0) {
            error("error on accept");
        }
        // create a new process, corresponding to the client's main thread
        if (fork() == 0) {
            // get the value of m from the client
            int m;
            if (read(newsockfd, &m, sizeof(int)) < 0) {
                error("error reading from socket");
            }
            // shared memory for the processes to communicate
            LetterMessage *messages = (LetterMessage *) mmap(
                NULL, m * sizeof(LetterMessage), PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
            // create `m` processes, each dealing with a single letter from the
            // client
            for (int i = 0; i < m; ++i) {
                if (fork() == 0) {
                    LetterMessage msg;
                    if (read(newsockfd, &msg, sizeof(LetterMessage)) < 0) {
                        error("error reading from socket");
                    }
                    messages[i] = msg;
                    // calculate `F` in the algorithm
                    double F = messages[i].probability / 2;
                    for (size_t j = 0; j < i; ++j) {
                        F += messages[j].probability;
                    }

                    // get the most significant bits of `F`, these bits are the
                    // encoding of the algorithm
                    LetterReply reply = getMostSignificantBits(
                        F, ceil(-log2(messages[i].probability)) + 1.0);
                    // send the encoding to the client
                    if (write(newsockfd, &reply, sizeof(LetterReply)) < 0) {
                        error("error writing to socket");
                    }
                    exit(0);
                }
                wait(NULL);
            }
            exit(0);
        }
    }
    return 0;
}
