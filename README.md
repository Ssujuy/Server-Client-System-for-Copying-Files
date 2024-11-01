# Server-Client-System-for-Copying-Files
Client requests an entire directory from Server , and copies it to its own directory file by file and block by block.

## General information / Execution of program :

- I have decided to separate the programs in 2 different directories , because remoteClient will delete all files if it exists in the same directory as dataServer.

- Inside directory Client there is the remoteClient.cpp and a Makefile , which creates the remoteClient objective and executable.
    remoteClient is executed with this command when inside Client directory : ./remoteClient -i <server_ip> -p <server_port> -d <directory>

- Inside directory Server there is the dataServer.cpp and a Makefile , which creates the dataServer objective and executable.
  dataServer is executed with this command when inside Server directory : ./dataServer -p <port> -s <thread_pool_size> -q <queue_size> -b <block_size>

- make all rule needs to be called in order to create objective and executable files.

## Server/dataServer.cpp :

- Structs i have used : 

    - struct worker_thread_data : saves the socket to sock variable and path of file to string filename , this struct will be created when we find
      a file through recursive scan and it will be pushed to the queue.

    - struct worker_thread_argument(typedef Arg) : Is used to pass an argument to the worker_thread through void *argp , variable pthread_mutex_t *smp 
      stores a unigue mutex for each worker_thread and bool *used is a pointer to a bool variable to check if worker_thread is busy.

    - struct used_socket : Is dynamically created after every successful connection and it is stored inside a vector.
    - Variable socket stores the value of the socket for the connection , variable used indicates whether a worker_thread is currently reading/writing on this socket ,
    - Variable pthread_mutex_t client_mtx stores a unique mutex for each connection and variable pthread_cond_t condvar stores a condition variable.

    - std::queue<worker_thread_data> execute_queue : Is used to store worker_thread_data structs , this is the queue used to store file paths.
    - std::vector<used_socket *> socket_vector : Is used to store pointers to struct used_socket , this struct is created after a new connection and is unique per connection
      and we store them in a vector because we can have infinite connections to the server.

  Further usage of those structs will be explained later.

### Functions

- void recursive_scan(char *dirpath, int sock) : Funtion recursive_scan gets the path of directory to be scanned with char *dirpath , opens the directory with opendir and iterates all              contents of directory with readdir function.

- Now new_dirpath is created for the next content of directory ,if d_type is DIT_DIR we call recursive_scan again but with new_dirpath as first argument . If not then we create a                   worker_thread_data struct add file path to w.filename variable and sock value to w.sock then we push the struct to the queue , because we have found a file to be copied , then we call            worker_wakeup function , to unlock a worker mutex.

- void worker_wakeup() : Function worker_wakeup() is called only from recursive_scan when a struct worker_thread_data is pushed to the queue , meaning that a file has been added to the queue       , so we have to unlock a blocked worker.

- Initialize a bool flag variable checking , for loop from 0 to pool_size - 1(checks every worker_thread) , if used[i] = 0 a blocked/non-used worker has been found , call                           pthread_mutex_unlock for &worker_mutex[i] (table of unique mutexes for each worker_thread) and set flag to 1.

Then loop breaks , if there are no available worker_threads then flag remains 0 and function is called again , until a worker is available.

### Main
        
- First of all , we initialize some global variables so that the threads can access them (std::queue<worker_thread_data> execute_queue , std::vector<used_socket *> socket_vector , int              MAX_QUEUE_SIZE , pthread_t *worker_thread , pthread_mutex_t *worker_mutex , bool *used , int pool_size , int block_size ). 
    
- Int pool_size is the number of worker_threads to be created , int block_size is the number of bytes to read and write the file in the socket , int MAX_QUEUE_SIZE is the maximum number of         elements inside the execute_queue .
    
- Variable pthread_t *worker_thread is a table to save all worker_threads to be created(is dynamically allocated in main) , pthread_mutex_t *worker_mutex is a table of mutexes 1 per                worker_thread(again dynamically allocated in main) and bool *used is a table of bool variables 1 for each worker_thread , which is set to 1 if a worker_thread is currently used (again 
  dynamically allocated in main).

- Define some variables needed for the rest of the code , int server_port saves port of the server , remoteClient_sock saves value of socket for Client-Server communication . struct                sockaddr_in dataServer, remoteClient stores Server-Client accordingly. Now main runs a check for all arguments given and stores values to respective variables. Variable pool_size has been        stored so now we dynamically allocate worker_thread table , worker_mutex table and bool used table.
    
- For loop to create struct worker_thread_argument * (Arg*) , initialize 1 mutex for each worker_thread , worker_arg->smp takes the value of &worker_mutex[i] and worker_arg->used takes value       of &used[i]. Then we call function pthread_create top create the worker_thread and pass argument Arg* to worker_thread_f function.

- Call socket function  then call bind function to bind socket sock to the dataServer struct. Server call listen function and waits for a connection to the socket sock. Since connect was           called from remoteClient , now dataServer enters while loop and calls accept , to accept connection from remoteClient (remoteClient_sock now stores the socket for Client-Server                   communication).

- Server dynamically allocates memory and creates a new pointer struct used_socket* . Variable usock->used is set to 0 none reads/writes on the socket at the moment and usock->socket takes         the value of remoteClient_sock . Call pthread_mutex_init to intialize the mutex variable of struct usock(usock->client_mtx) , call pthread_cond_init
  to initialize condition variableof struct pointer usock (usock->condvar). Finally , we insert the pointer struct usock* to the vectorand dataServer wait for the next connection of                remoteClient.

- Reason we use struct is to ensure that only 1 worker thread will be able to write to the socket and all other worker_threads that communicate through the same socket will wait for the            worker_thread currently writing to finish.

Further explanation on worker_thread. 

### Threads

- **communication_thread :**

  - A new communication_thread is created , when a new connection arrives on dataServer. The communication_thread then takes value of socket(remoteClient_sock) as argument and reads                  path of directory from remoteClient through the socket. Then calls recursive_scan function to scan the entire directory

  - **worker_thread :** 

    - First of all , worker_thread saves the argument given upon creation to a Arg* worker_arg variable and because we want the threads to be blocked on creation we lock the worker_arg->               smp mutex twice before the while loop and inside the while loop , otherwise worker_threads will start poping structs that don't exist in the queue. Set *worker_arg->used before                   locking , because worker_thread is not used as of now.
            
    - Then when worker_wakeup function unlocks the mutex *worker_arg->used is set to 1 and worker_thread initializes a worker_thread_data p variable which takes the first element of                    execute_queue with execute_queue.front() function.
            
    - Worker_thread calls fopen with path being the path taken from p.filename , gets the whole size of the file in bytes and stores it in file_size variable. Worker_thread dynamically                 creates a new char* temp variable that stores  -f [path of file] -d [size of file] . Then , defines a used_socket* usock variable and iterates the global vector socket_vector .                   When p.sock is equal to the usock->socket value , usock takes the value of current element of vector and loop stops with break.
            
    - Now we call pthread_mutex_lock on &usock->client_mtx and check if value of usock->used is 1 .If yes call pthread_cond_wait on condition variable &usock->condvar , which means that                another worker_thread is currently using the socket , so this worker_thread has to wait here for the other one to finish.
            
    - Otherwise worker_thread writes cher* temp variable to the socket p.sock , sets variable usock->used to 1 , because socket is being used now and unlocks &usock->client_mtx mutex by                calling pthread_mutex_unlock.
            
    - Worker_thread opens file with path p.filename reads the entire file block by block with while ((size = read(fd, buff, block_size)) and writes every block read to the socket.
            
    - Call pthread_mutex_lock on &usock->client_mtx set usock->used to 0 , because worker_thread has finished writing and empty the buff . Finally ,  worker_thread calls read(p.sock,                   buff, block_size) and expects to read indicator string "ok" from client , meaning that the remoteClienthas finished reading and copying the file , if buff contains "ok" then we use               pthread_cond_signal on &usock->condvar to signal just 1 waiting worker_thread on pthread_cond_wait function call . Unlock &usock->client_mtx with pthread_mutex_unlock , empty buff                and set *worker_arg->used variable to 0 , because worker_thread has finished its task , on next loop worker_thread will be blocked untill mutex unlocks from worker_wakeup function.

    - The reason we use usock struct and condition variable is to ensure that no more than just 1 worker_thread will be reading/writing on the same socket and because we know that 
          pthread_cond_signal will unblock just 1 worker_thread stack on pthread_cond_signal , so there will be no race condition between worker_threads for the same socket.

## Client/remoteClient.cpp :

### Main

- First of all , i define a BUFF_SZ 512 which is the size of buffer to read from the socket.

- Client initializes some variables for  the rest of the program struct sockaddr_in dataServer will be used to store the Server values (Ip address , port , etc).
  int port stores the value of the port given as argument from main and sock stores the value of the socket.

- Run a check if arguments are given correctly and store all values of arguments to their respective variables.

- Call socket function to create a socket for Client-Server communication , call gethostbyaddr function to return entry from host data base and then  call connect function to attempt a             connection to dataServer. Main continues on successfull connection.

- RemoteClient reads -f [path of file] -d [size of file] through the socket , checks if read was correct and splits the buff with strtok to store both the path of file to char *filename            and size of file to int nfile_sz.

- Iterate entire filename , if / is found this is the path of a directory . Create a char* temp variable copy filename until k value and check if directory exists in Client directory ,             if we create the directory with path temp through mkdir function and continue for loop.

- After filename loop ends we check(with function access) if file with path filename exists , if yes we delete it with remove and then create the file with creat function.

- Then , remoteClient checks how many number of loops we need in order to read entire file with block_size being BUFF_SZ .
        
- However , division of nfile_sz / BUFF_S might have a remainder , so we also calculate last_loop as nfile_sz % BUFF_SZ.
    
- RemoteClient now will do a for loop from 0 to value read-loop + 1 (+1 is added because of a possible remainder of the above division) and if this not the last loop read is called with            size_t nbytes BUFF_SZ and remoteClient writes all bytes read to the file , otherwise if this is the last loop  read is called with exactly last_loop number of bytes to read and again             writes bytes read to the file.

- Finally , remoteClient will indicate to the worker_thread that it has finished reading from the socket and writing to the file by 
  writing string "ok" to the socket , loop while continues and when there is nothing to read from the socket client exits.  

## Makefile :

- Server directory has a Makefile which creates a dataServer.o objective file and then creates a dataServer executable , same rule applies for Makefile of Client
    
- These Makefiles are needed because our .cpp files exist in different directories
    
- Makefile now changes current directory to Server and Client and executes their Makefiles , this happens with command make all. make clean command removes all objective and executable files       from Server and Client directories.
