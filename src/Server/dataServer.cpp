#include <stdio.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <queue>
#include <string>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>

#define MAX_QUEUED_CONNECTIONS 100

typedef struct worker_thread_data // struct worker data is used for element of queue
{

    int sock;             // its variables are the socket , which saves the number of the socket to communicate with client
    std::string filename; // and the path of the file to be copied(filename)

} worker_thread_data;

typedef struct worker_thread_argument // struct worker_thread_argument is used as void* argp argument to be passed on every worker thread
{

    pthread_mutex_t *smp; // every worker thread has its own mutex and a bool* variable to check if worker is occupied or not
    bool *used;

} Arg;

typedef struct used_socket // used socket is a struct for each client connection
{
    int socket;                 // its variables are the socket for client-server communication
    bool used;                  // a bool used variable to see if socket is currently occupied
    pthread_mutex_t client_mtx; // both a mutex and a condition variable
    pthread_cond_t condvar;

} used_socket;

std::queue<worker_thread_data> execute_queue; // queue for file paths and socket to be written as worker_thread_data structs
std::vector<used_socket *> socket_vector;     // vector to save all used_socket* structs(1 for each client-server connection)
int MAX_QUEUE_SIZE;                           // maximum size of queue that contains file paths
pthread_t *worker_thread;                     // pthread_t * worker_thread (basically a table to save all worker threads)
pthread_mutex_t *worker_mutex;                // pthread_mutex_t * (basically a table that contains 1 mutex for every worker thread)
bool *used;                                   // table for bool variable used
int pool_size, block_size;                    // pool_size is number of worker_threads to be created and block_size is the size of block to read/write the files(both are given as arguments in main)

void *communication_thread_f(void *argp);     // function for communication threads
void recursive_scan(char *dirpath, int sock); // function to recursively scan a directory
void *worker_thread_f(void *argp);            // function for worker threads
void worker_wakeup();                         // function that wakes up a worker , when a file is added to thee queue

int main(int argc, char *argv[])
{
    pthread_t communication_thread; // define all variables needed...
    int server_port, sock, remoteClient_sock, errnum;
    struct sockaddr_in dataServer, remoteClient;
    struct sockaddr *pdataServer = (struct sockaddr *)&dataServer;
    struct sockaddr *premoteClient = (struct sockaddr *)&remoteClient;
    dataServer.sin_family = AF_INET;
    dataServer.sin_addr.s_addr = htonl(INADDR_ANY);
    // quick check if all arguments received are correct...
    if (argc != 9) // checks if 9 arguments where given in main
    {

        printf("Wrong arguments given");
        exit(1);
    }

    if (strcmp(argv[7], "-b") == 0)
    {
        block_size = atoi(argv[8]);
    }
    else
    {
        printf("Wrong arguments\n");
        exit(1);
    }
    if (strcmp(argv[1], "-p") == 0)
    {
        server_port = atoi(argv[2]);
    }
    else
    {
        printf("Wrong arguments\n");
        exit(1);
    }

    if (strcmp(argv[3], "-s") == 0)
    {
        pool_size = atoi(argv[4]);
    }
    else
    {
        printf("Wrong arguments\n");
        exit(1);
    }
    if (strcmp(argv[5], "-q") == 0)
    {
        MAX_QUEUE_SIZE = atoi(argv[6]);
    }
    else
    {
        printf("Wrong arguments\n");
        exit(1);
    }

    printf("Server's parameters are: \n"); // print all parameters received...
    printf("port: %d\n", server_port);
    printf("thread_pool_size: %d\n", pool_size);
    printf("queue_size: %d\n", MAX_QUEUE_SIZE);
    printf("Block_size: %d\n", block_size);

    worker_thread = new pthread_t[pool_size]; // dynamically allocate the worker_thread table , the worker_mutex table and the used table
    worker_mutex = new pthread_mutex_t[pool_size];
    used = new bool[pool_size];

    for (int i = 0; i < pool_size; i++) // create exactly pool_size worker_threads
    {
        Arg *worker_arg = (Arg *)malloc(sizeof(*worker_arg)); // create struct Arg , for each worker_thread
        worker_arg->used = (bool *)malloc(sizeof(bool));

        if (pthread_mutex_init(&(worker_mutex[i]), NULL) != 0) // initialize a mutex for every worker_thread(use table worker_mutex)
        {

            perror("mutex init");
            exit(1);
        }
        worker_arg->smp = &worker_mutex[i];
        worker_arg->used = &used[i];
        if (errnum = pthread_create(&worker_thread[i], NULL, worker_thread_f, worker_arg)) // then create worker_thread and pass worker_arg as argument(void* argp)
        {
            fprintf(stderr, "pthread_create %s", strerror(errnum));
            exit(1);
        }
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) // create a socket for client-server communication
    {
        perror("socket error");
        exit(1);
    }

    dataServer.sin_port = htons(server_port); // add port to struct sockaddr_in dataServer

    if (bind(sock, pdataServer, sizeof(dataServer)) < 0) // bind socket sock
    {
        perror("bind error");
        exit(1);
    }
    printf("Server was successfully initialized...\n");

    if (listen(sock, MAX_QUEUED_CONNECTIONS) < 0) // listening for client connections
    {
        perror("listen error");
        exit(1);
    }

    printf("Listening for connections to port %d\n", server_port);
    while (1)
    {

        socklen_t remoteClient_len = sizeof(remoteClient);

        if ((remoteClient_sock = accept(sock, premoteClient, &remoteClient_len)) < 0) // accept connection from client , now server and client can communicate
        {
            perror("accept error");
            exit(1);
        }
        printf("Accepted connection from localhost\n");

        used_socket *usock = (used_socket *)malloc(sizeof(*usock)); // initialize struct used_socket* (1 for each client)

        usock->used = 0;                   // bool variable used is 0 when socket is not used!!
        usock->socket = remoteClient_sock; // variable socket takes the value given by accept (remoteClient_sock)

        if (pthread_mutex_init(&usock->client_mtx, NULL) != 0) // intializing mutex for the socket(each socket-connection has a unique mutex)
        {

            perror("mutex init");
            exit(1);
        }

        pthread_cond_init(&usock->condvar, NULL); // also initialize 1 condition variable for each socket
        socket_vector.push_back(usock);           // add struct usock to a vector

        int *new_sock = new int;
        *new_sock = remoteClient_sock;

        if (errnum = pthread_create(&communication_thread, NULL, communication_thread_f, new_sock)) // create a communication only argument passed is the socket
        {
            fprintf(stderr, "pthread_create %s", strerror(errnum));
            exit(1);
        }
    }
    return 0;
}
// communication thread!!
void *communication_thread_f(void *argp)
{
    char buff[1000]; // static buffer
    int *sock = (int *)argp;
    read(*sock, buff, 1000); // read requested directory path from client , through the socket
    printf("[Thread: %ld]: About to scan directory %s \n", pthread_self(), buff);
    recursive_scan(buff, *sock); // recursively scan entire directory
    return NULL;
}
// worker thread!!
void *worker_thread_f(void *argp)
{
    Arg *worker_arg = (Arg *)argp; // argument passed to worker thread is of type struct Arg...
    int fd, size;
    char *buff = new char[block_size];            // initialize buffer with size block_size
    if (pthread_mutex_lock(worker_arg->smp) != 0) // locks mutex passed as argument from main(mutex is unique to every worker thread!!)
    {
        perror("mutex lock");
        exit(1);
    }
    while (1)
    {
        *worker_arg->used = 0;                        // worker_thread is not used so *used is set to 0
        if (pthread_mutex_lock(worker_arg->smp) != 0) // now worker thread wait for funcion worker_wakeup() , to unlock the muutex(basically unlocks when a file is added)
        {
            perror("mutex lock");
            exit(1);
        }
        *worker_arg->used = 1;                        // worker_thread is not used so *used is set to 0
        worker_thread_data p = execute_queue.front(); // check front of queue , which is always of type struct worker_thread_data
        execute_queue.pop();                          // pops front of queue...
        printf("[Thread: %ld]: Received task : <%s , %d>\n", pthread_self(), p.filename.c_str(), p.sock);
        printf("[Thread: %ld]: About to read file %s\n", pthread_self(), p.filename.c_str());
        FILE *fptr;
        if ((fptr = fopen(p.filename.c_str(), "r")) == NULL) // open file in path taken from queue
        {

            perror("fopen");
            exit(1);
        }
        fseek(fptr, 0, SEEK_END); // here we take the entire size of the file
        int file_size = ftell(fptr);
        fseek(fptr, 0, SEEK_SET);
        std::string file_size_string = std::to_string(file_size);                                                               // initialize a string and copy the path to file_size_string
        char *temp = new char[strlen("-f") + strlen(p.filename.c_str()) + strlen("-d") + strlen(file_size_string.c_str()) + 4]; // dynamically allocate string ,then copy -f filepath -d size
        strcpy(temp, "-f");
        char *temp2 = new char[strlen(p.filename.c_str()) + 1];
        strcpy(temp2, p.filename.c_str());
        strcat(temp, " ");
        strcat(temp, temp2);
        delete[] temp2;
        strcat(temp, " ");
        strcat(temp, "-d");
        strcat(temp, " ");
        strcat(temp, file_size_string.c_str());
        temp[strlen(temp)] = '\0';
        used_socket *usock;
        for (auto i = socket_vector.begin(); i != socket_vector.end(); ++i) // iterate globally initialized vector
        {
            usock = (used_socket *)*i; // i is of type struct used_socket *

            if (usock->socket = p.sock)
            {

                break;
            }
        }
        int errnum;

        if (errnum = pthread_mutex_lock(&usock->client_mtx)) // we now lock the mutex which is stored in usock struct
        {
            fprintf(stderr, "%s: %s\n", "pthread_mutex_lock", strerror(errnum));
            exit(1);
        }
        if (usock->used == 1) // checks if socket is being used by another worker
        {
            pthread_cond_wait(&usock->condvar, &usock->client_mtx); // if yes use pthread_cond_wait function , basically wait for other worker thread to stop using the socket
        }                                                           // if no pthread_cond_wait won't be called , thread continues ...
        write(p.sock, temp, strlen(temp));                          // write file path and size to client
        usock->used = 1;                                            // socket is now being used!! Value changes to 1
        if (errnum = pthread_mutex_unlock(&usock->client_mtx))      // we now unlock the mutex which is stored in usock struct
        {
            fprintf(stderr, "%s: %s\n", "pthread_mutex_unlock", strerror(errnum));
            exit(1);
        }

        delete[] temp;
        fclose(fptr);

        if ((fd = open(p.filename.c_str(), O_RDONLY)) < 0) // open file with read only permsF
        {

            perror("open");
            exit(1);
        }
        while ((size = read(fd, buff, block_size)) > 0) // read entire file per block with size block_size
        {
            write(p.sock, buff, strlen(buff)); // write contents of file to the sock , so that the client can read
            memset(buff, 0, block_size);
        }
        if (errnum = pthread_mutex_lock(&usock->client_mtx)) // we now lock the mutex which is stored in usock struct
        {
            fprintf(stderr, "%s: %s\n", "pthread_mutex_lock", strerror(errnum));
            exit(1);
        }
        usock->used = 0;             // socket is no longer used by the worker , used var is set to 0
        memset(buff, 0, block_size); // empty buff
        char check_ok[3];
        if ((size = read(p.sock, check_ok, 3)) < 0) // worker thread reads from client(only that client can write is ok)
        {
            perror("read");
            exit(1);
        }
        check_ok[2] = '\0';
        if (strcmp(check_ok, "ok") == 0) // if "ok" was successfully received , client has read and written the whole file
        {

            pthread_cond_signal(&usock->condvar); // now we signal the next thread that will operate on the same socket
        }

        close(fd);                                             // close file
        if (errnum = pthread_mutex_unlock(&usock->client_mtx)) // we now unlock the mutex which is stored in usock struct
        {
            fprintf(stderr, "%s: %s\n", "pthread_mutex_lock", strerror(errnum));
            exit(1);
        }
        memset(buff, 0, block_size); // empty buff memory
        *worker_arg->used = 0;       // worker thread is no longer used , *used variable is set to 0
    }
    return NULL;
}

void recursive_scan(char *dirpath, int sock) // function to recursively scan all contents of directory with path dirpath
{
    struct dirent *temp;
    DIR *dir_to_scan = opendir(dirpath); // open directory with function opendir() and return pointer to directory on dir_to_scan
    if (dir_to_scan == NULL)             // checks if folder could not be opened
    {
        fprintf(stderr, "Can't open dir %s: %s", dirpath, strerror(errno));
        exit(1);
    }
    while ((temp = readdir(dir_to_scan)) != NULL) // while loop for all contents of directory!!
    {

        if (temp->d_name[0] != '.')
        {
            char *new_dirpath = new char[strlen(dirpath) + strlen(temp->d_name) + 2]; // dynamically allocate string to create new path dirpath + d->name
            strcpy(new_dirpath, dirpath);
            strcat(new_dirpath, "/");
            strcat(new_dirpath, temp->d_name);
            if (temp->d_type == DT_DIR) // checks if temp is a directory and calls recursive_scan() with new directory path (new_dirpath)
            {
                recursive_scan(new_dirpath, sock); // recursion...
            }
            else
            {
                // while (MAX_QUEUE_SIZE == execute_queue.size()) // waiting on while loop if queue has reached maximum size (MAX_QUEUE_SIZE is given as argument)
                // {
                // }
                std::string new_dirpath_str(new_dirpath);
                worker_thread_data w;         // create new struct of type worker_thread_data to push it to the queue
                w.filename = new_dirpath_str; // give path of file to the struct variable(w.filename)
                w.sock = sock;                // also give socket value to the struct variable(w.sock)
                printf("[Thread: %ld]: Adding file %s to the queue...\n", pthread_self(), new_dirpath);
                execute_queue.push(w); // push struct of type worker_thread_data to the queue
                worker_wakeup();       // now that a file has been pushed , wake up a non-occupied worker!!
            }
            delete[] new_dirpath; // delete dynnamically allocated memory
        }
    }
}
// function to wake up workers...
void worker_wakeup()
{
    bool flag = 0; // initiate a flag variable to check if a worker has woken up and is managing the file...

    for (int i = 0; i < pool_size; i++) // iterate bool used table
    {

        if (used[i] == 0) // stop on first non-used worker thread
        {

            if (pthread_mutex_unlock(&worker_mutex[i]) != 0) // unlock mutex that blocks worker thread (basically wakes up worker because file was pushed to queue!!)
            {
                perror("mutex unlock");
                exit(1);
            }
            flag = 1; // flag is set to 1 , because a worker has been woken up
            used[i] = 1;
            break; // break on first non-used worker!!
        }
    }

    if (flag == 0) // if all workers are currently used , call function again , until a blocked worker is found!!
    {
        worker_wakeup(); // call function again
    }
}