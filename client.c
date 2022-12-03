#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EPS __DBL_EPSILON__

// prints error message with system call, and exits the program
void error(char const *msg) {
    perror(msg);
    exit(0);
}

// represents letter and its probability, it is communicated to the server
typedef struct {
    int i;
    char letter;
    double probability;
} LetterMessage;

// function used by quick sort to compare two instances of `LetterMessage`
int compareLetters(void const *a, void const *b);

// struct that represents the set of arguments passed to a worker thread
typedef struct {
    size_t i;
    int sockfd;
    struct sockaddr_in *serv_addr;
    LetterMessage const *msg;
} ThreadArg;

// the struct of data communicated by the server to the client, it is also the
// struct returnd by threads and used for printing the binary encodings of letters
typedef struct {
    size_t bits;
    size_t n;
} LetterReply;

// takes in a `LetterMessage` struct and sends it to the server
// waits for the server to reply with a `LetterReply` object, and returns it
// intended to be run in a separate thread
void *encode(void *arg) {
    ThreadArg *targ = (ThreadArg *) arg;
    if (write(targ->sockfd, targ->msg, sizeof(*(targ->msg))) < 0) {
        error("error writing to socket");
    }
    LetterReply *ret = (LetterReply *) calloc(1, sizeof(LetterReply));
    if (read(targ->sockfd, ret, sizeof(*ret)) < 0) {
        error("error reading from socket");
    }
    return ret;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s hostname port\n", argv[0]);
        exit(0);
    }

    // sets up socket with the server with information from the cli args
    int portno = atoi(argv[2]), sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        error("error opening socket");
    }
    struct hostent *server = gethostbyname(argv[1]);
    if (server == NULL) {
        error("no such host");
    }
    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy(server->h_addr, &serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);

    // this block takes input from stdin and constructs a frequence table of the
    // characters, which is then used for calculating the probability
    // distributions, the characters are then sorted based on probability with
    // lexicographic order as a tie-breaker
    char s[1024];
    fgets(s, sizeof(s), stdin);
    int m = 0, len = strlen(s), freq[256] = {0};
    for (int i = 0; i < len; ++i) {
        m += freq[s[i]]++ == 0;
    }
    LetterMessage *letters = (LetterMessage *) calloc(m, sizeof(LetterMessage));
    int j = 0;
    for (int i = 0; i < 256; ++i) {
        if (freq[i] != 0) {
            letters[j++] =
                (LetterMessage){j, (char) i, ((double) freq[i]) / len};
        }
    }
    qsort(letters, m, sizeof(LetterMessage), compareLetters);

    // connect with the socket, with the structs set up above
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) <
        0) {
        error("error connecting");
    }
    // send the the value of m (the number of distinct characters) to the server
    if (write(sockfd, &m, sizeof(int)) < 0) {
        error("error writing to socket");
    }
    // allocate storage for the thread ids and arguments
    pthread_t *tids = (pthread_t *) calloc(m, sizeof(pthread_t));
    ThreadArg *targs = (ThreadArg *) calloc(m, sizeof(ThreadArg));
    puts("SHANNON-FANO-ELIAS Codes:");
    for (size_t i = 0; i < m; ++i) {
        targs[i] = (ThreadArg){i, sockfd, &serv_addr, letters + i};
        // call new pthread_create
        pthread_create(tids + i, NULL, encode, (void *) (targs + i));
        usleep(500);
    }
    // wait for all threads to finish and print the results from the server
    for (size_t i = 0; i < m; ++i) {
        LetterReply *code;
        pthread_join(tids[i], (void **) &code);
        printf("Symbol %c, Code: ", letters[i].letter);
        for (size_t i = code->n; i > 0; --i) {
            printf("%ld", (code->bits >> i) & 1);
        }
        free(code);
        puts("");
    }
    free(targs);
    free(tids);
    free(letters);
}

int compareLetters(void const *a, void const *b) {
    LetterMessage const *lhs = (LetterMessage const *) a,
                        *rhs = (LetterMessage const *) b;
    double diff = rhs->probability - lhs->probability;
    // if the numbers are almost equal (difference is within tolerance), compare based on lexicographic order
    return diff < EPS && -diff < EPS ? lhs->letter - rhs->letter
           : diff > 0                ? 1
                                     : -1;
}
