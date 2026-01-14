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
#include "utils.h"

void segfault_handler(int sig)
{
  printf("Executor:  Caught segmentation fault (signal %d)\n", sig);
  exit(1);
}

void *exec_thread(void *threadid);

void perror_exit(const char *message)
{
  perror(message);
  exit(EXIT_FAILURE);
}

void *controller_thread(void *arg);
void *worker_thread(void *arg);
void sigchld_handler(int sig);

int pid, _pid;
int bufferSize, threadPoolSize;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER; // mutex
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;    // c.v.

struct shared_mem
{
  Queue q;               // Oura gia ta epomena Jobs
  Job **current_jobs;    // Pinakas me ta Jobs pou trexoun twra
  int current_jobs_size; // Megethos tou pinaka current_jobs
  int job_counter;       // Job ID generator
  int concurrency;       // default
  int exiting;
} *shared_data;

int main(int argc, char *argv[])
{
  signal(SIGPIPE, SIG_IGN);
  signal(SIGSEGV, segfault_handler);
  signal(SIGCHLD, sigchld_handler);

  int fd_shared_mem;
  char *shmpath = "e"; // like key

  shm_unlink(shmpath);
  fd_shared_mem = shm_open(shmpath, O_CREAT | O_EXCL | O_RDWR, 0600);
  if (fd_shared_mem == -1)
  {
    perror("shm_open");
    exit(EXIT_FAILURE);
  }

  if (ftruncate(fd_shared_mem, sizeof(struct shared_mem)) == -1)
  {
    perror("ftruncate");
    exit(EXIT_FAILURE);
  }

  shared_data = mmap(NULL, sizeof(struct shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shared_mem, 0);
  if (shared_data == MAP_FAILED)
  {
    perror("mmap");
    exit(EXIT_FAILURE);
  }

  shared_data->current_jobs_size = 1;
  shared_data->job_counter = 1;
  shared_data->concurrency = 1;
  shared_data->exiting = 0;

  pid = getpid();
  int serverSocket, optval = 1;
  struct addrinfo hints, *res, *rp;
  struct sockaddr_storage client;
  socklen_t clientlen;
  char host[NI_MAXHOST], service[NI_MAXSERV];
  pthread_t tid;

  if (argc != 4)
  {
    fprintf(stderr, "Usage: %s <port> <q_size> <w_threads>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  bufferSize = atoi(argv[2]);
  threadPoolSize = atoi(argv[3]);
  int result;
  pthread_t threads[threadPoolSize];

  for (int i = 0; i < threadPoolSize; i++)
  {
    result = pthread_create(&threads[i], NULL, worker_thread, NULL);
    if (result != 0)
    {
      fprintf(stderr, "Executor:  Error creating thread %d\n", i);
      return 1;
    }
  }

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;       // IPv4
  hints.ai_socktype = SOCK_STREAM; // TCP
  hints.ai_flags = AI_PASSIVE;     // All interfaces

  if (getaddrinfo(NULL, argv[1], &hints, &res) != 0)
  {
    perror_exit("getaddrinfo");
  }

  for (rp = res; rp != NULL; rp = rp->ai_next)
  {
    serverSocket = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (serverSocket == -1)
    {
      continue;
    }

    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
    {
      perror_exit("setsockopt");
    }

    if (bind(serverSocket, rp->ai_addr, rp->ai_addrlen) == 0)
    {
      break;
    }
  }

  if (rp == NULL)
  {
    perror_exit("bind");
  }

  freeaddrinfo(res);


  // Listen for connections
  if (listen(serverSocket, 5) < 0)
  {
    perror_exit("listen");
  }

  // Init current_jobs table
  shared_data->current_jobs = (Job **)malloc(sizeof(Job *));
  shared_data->current_jobs[0] = NULL;

  printf("Executor:  Listening for connections on port %s\n", argv[1]);

  int counter = 0;

  while (1)
  { 
    printf("---------------- %d\n", counter);

    clientlen = sizeof(client);
    int newsock = accept(serverSocket, (struct sockaddr *)&client, &clientlen);
    if (newsock < 0)
    {
      perror("accept");
      continue;
    }

    if (getnameinfo((struct sockaddr *)&client, clientlen, host, sizeof(host), service, sizeof(service), 0) == 0)
    {
      printf("Executor:  Accepted connection from %s:%s\n", host, service);
    }
    else
    {
      perror("getnameinfo");
      close(newsock);
      continue;
    }

    pthread_t threadss;

    //create controller thread
    if (pthread_create(&threadss, NULL, controller_thread, (void *)&newsock) != 0)
    {
      perror("pthread_create");
      close(newsock);
    }
    counter++;
  }
  close(serverSocket);
  return 0;
}

void *controller_thread(void *arg)
{
  signal(SIGSEGV, segfault_handler);

  int client_socket = *((int *)arg);
  char input[1024];

  if (read(client_socket, input, 1024) > 0)
  {
    printf("Executor: Input: %s\n", input);
  }

  char command[1024];
  int i;

  //parse command from Commander
  for (i = 0; input[i] != '\0' && input[i] != ' '; i++)
  {
    command[i] = input[i];
  }

  command[i] = '\0';

  if (strcmp(command, "exit") == 0)
  {
    char response[] = "SERVER TERMINATED BEFORE EXECUTION";
    if (write(client_socket, response, strlen(response) + 1) == -1)
    {
      perror("Executor:  Error writing to socket");
      exit(EXIT_FAILURE);
    }
    printf("Executor:  SERVER TERMINATED BEFORE EXECUTION\n");

    pthread_mutex_lock(&queue_mutex);

    while (current_jobs_count(shared_data->current_jobs, shared_data->current_jobs_size) > 0)
    {
      //wait while queue is full
      pthread_cond_wait(&queue_cond, &queue_mutex);
    }
    pthread_mutex_unlock(&queue_mutex);

    close(client_socket);
    kill(pid, SIGINT); // kill gonio
    pthread_exit(NULL);
  }
  else if (strcmp(command, "issueJob") == 0)
  {
    pthread_mutex_lock(&queue_mutex);
    while (shared_data->q.size >= bufferSize)
    {
      //wait while queue is full
      pthread_cond_wait(&queue_cond, &queue_mutex);
    }

    // Add Job to Queue
    Job *new_job = (Job *)malloc(sizeof(Job));          // Allocate memory for the new Job
    int position = queue_add(&shared_data->q, new_job); // Add the new Job to the Queue and get its position
    printf("position: %d\n", position);
    new_job->job_id = shared_data->job_counter++;       // Assign a unique job ID to the new Job
    new_job->job_name = strdup(input);                  // Set the name of the new Job
    new_job->queue_pos = position;                      // Store the position of the new Job in the Queue
    new_job->client_socket = client_socket;
    printf("%s\n", queue_serialize(&shared_data->q));


    char response[200];
    sprintf(response, "%s SUBMITTED\n", serializeJob(new_job));

    if (write(client_socket, response, strlen(response) + 1) == -1)
    {
      perror("Executor:  Error writing to socket");
      exit(EXIT_FAILURE);
    }

    pthread_cond_broadcast(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);
  }
  else if (strcmp(command, "setConcurrency") == 0)
  {
    int numWords = countWords(input);                   // Count the number of words in the input string
    char **args = getWordsExceptFirst(input, numWords); // Extract the words from the input string without first word
    int new_concurrency = atoi(args[0]);                // Convert the first word to an integer
    printf("Executor:  New concurrency: %d\n", new_concurrency);

    pthread_mutex_lock(&queue_mutex);

    if (new_concurrency > shared_data->concurrency)
    { // increase current_jobs table size
      printf("Executor:  Increasing concurrency\n");
      shared_data->current_jobs_size = new_concurrency;
      Job **temp_jobs = shared_data->current_jobs;                                                // Create a temporary table to hold the current_jobs data
      shared_data->current_jobs = (Job **)malloc(shared_data->current_jobs_size * sizeof(Job *)); // Allocate memory for the new current_jobs table with the updated size
      for (int i = 0; i < shared_data->current_jobs_size; i++)
      { // Antigrafh ta stoixeia toy paliou ston neo pinaka
        if (i < shared_data->concurrency)
          shared_data->current_jobs[i] = temp_jobs[i];
        else
          shared_data->current_jobs[i] = NULL;
      }
      free(temp_jobs); // free old table
    }

    shared_data->concurrency = new_concurrency;
    pthread_mutex_unlock(&queue_mutex);

    char response[1024];
    sprintf(response, "CONCURRENCY SET AT %d", new_concurrency);
    if (write(client_socket, response, strlen(response) + 1) == -1)
    {
      perror("Executor:  Error writing to socket");
      exit(EXIT_FAILURE);
    }
    if (write(client_socket, "end", 4) == -1)
    {
      perror("Executor:  Error writing to socket");
      exit(EXIT_FAILURE);
    }

  }
  else if (strcmp(command, "stop") == 0)
  { // Stop a queued Job
    char tmp[1024];
    int numWords = countWords(input);
    char **args = getWordsExceptFirst(input, numWords);
    strncpy(tmp, args[0] + 4, strlen(args[0])); // skip "job_" prefix
    int jobid = atoi(tmp);                      // Extract the Job ID from the command
    printf("Executor:  Stopping job id: %d\n", jobid);

    pthread_mutex_lock(&queue_mutex);
    char response[200];
    Job *job = queue_remove(&shared_data->q, jobid);
    if (job == NULL)
      sprintf(response, "JOB %d NOT FOUND\n", jobid);
    else
    {
      sprintf(response, "JOB %d REMOVED\n", jobid);
      free(job);
    }
    printf("%s\n", queue_serialize(&shared_data->q));

    pthread_mutex_unlock(&queue_mutex);

    if (write(client_socket, response, strlen(response) + 1) == -1)
    { // response message to the JobCommander
      perror("Executor:  Error writing to socket");
      exit(EXIT_FAILURE);
    }
    if (write(client_socket, "end", 4) == -1)
    {
      perror("Executor:  Error writing to socket");
      exit(EXIT_FAILURE);
    }

  }
  else if (strcmp(command, "poll") == 0)
  { // Poll the queued Jobs
    pthread_mutex_lock(&queue_mutex);
    char *response = queue_serialize(&shared_data->q);
    pthread_mutex_unlock(&queue_mutex);

    if (write(client_socket, response, strlen(response) + 1) == -1)
    { // response message to the JobCommander
      perror("Executor:  Error writing to socket");
      exit(EXIT_FAILURE);
    }
    if (write(client_socket, "end", 4) == -1)
    {
      perror("Executor:  Error writing to socket");
      exit(EXIT_FAILURE);
    }

  }
  else
  {
    char response[] = "Error command!";
    if (write(client_socket, response, strlen(response) + 1) == -1)
    {
      perror("Executor:  Error writing to socket");
      exit(EXIT_FAILURE);
    }
    if (write(client_socket, "end", 4) == -1)
    {
      perror("Executor:  Error writing to socket");
      exit(EXIT_FAILURE);
    }

  }

  pthread_exit(NULL);
}

void *worker_thread(void *args)
{
  signal(SIGSEGV, segfault_handler);

  pthread_t ttid;

  while (1)
  {
    pthread_mutex_lock(&queue_mutex);
    while (shared_data->q.size == 0 || current_jobs_count(shared_data->current_jobs, shared_data->current_jobs_size) >= shared_data->concurrency)
    {
      //wait while queue is empty or current jobs exceed concurrency number
      pthread_cond_wait(&queue_cond, &queue_mutex);
    }

    //get job from queue
    Job *new_job = queue_first(&shared_data->q); 
    queue_sub(&shared_data->q);

    int first_empty = current_jobs_get_first_empty(shared_data->current_jobs, shared_data->current_jobs_size); // find empty slot in current_jobs table

    shared_data->current_jobs[first_empty] = new_job; // Add the new Job to the current_jobs array
    printf("Executor:  new job start %s\n", serializeJob(new_job));
    pthread_cond_broadcast(&queue_cond);
    pthread_mutex_unlock(&queue_mutex);

    // Get Job command: einai h 1h leksh
    int numWords = countWords(new_job->job_name);         // Count the number of words in the job_name
    char *tmp_str = strdup(new_job->job_name);            // antigrapse to job_name gia na diathrhthei to original
    char **args = getWordsExceptFirst(tmp_str, numWords); // Get an array of arguments except the first word

    free(tmp_str);
    pid_t pid1 = fork();
    if (pid1 == -1)
    {
      perror("Executor:  fork");
      exit(EXIT_FAILURE);
    }
    else if (pid1 == 0)
    { // Child process
      char filename[100];
      sprintf(filename, "%d.output", getpid());
      int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
      if (fd < 0)
      {
        perror("open");
        exit(EXIT_FAILURE);
      }

      //redirect standard output to file
      if (dup2(fd, STDOUT_FILENO) < 0)
      {
        perror("dup2 stdout");
        close(fd);
        exit(EXIT_FAILURE);
      }

      close(fd);

      execvp(args[0], args); // Execute the Job (an trejei => will not return)
      execv(args[0], args);  // An den treksei to execvp
      perror("Executor:  Process did not terminate correctly\n");
      exit(1);
    }
    else
    { // Parent process
      int status;
      char filename[100];

      pthread_mutex_lock(&queue_mutex);
      //update jobs table with pid
      shared_data->current_jobs[first_empty]->pid = pid1;
      pthread_mutex_unlock(&queue_mutex);

      waitpid(pid1, &status, 0);

      if (pid1 == -1)
      {
        perror("waitpid");
        exit(0);
      }

      if (WIFEXITED(status))
      {
        printf("Executor:  Child terminated with status %d\n", WEXITSTATUS(status));
      }
      else
      {
        printf("Executor:  Child terminated abnormally\n");
      }

      sprintf(filename, "%d.output", pid1);
      char output_buffer[1024];
      ssize_t bytesRead;
      memset(output_buffer, 0x0, 1024);
      int read_fd = open(filename, O_RDONLY);
      if (read_fd < 0)
      {
        perror("open for reading");
        exit(EXIT_FAILURE);
      }

      pthread_mutex_lock(&queue_mutex);

      // Read the file contents and print them to stdout
      while ((bytesRead = read(read_fd, output_buffer, sizeof(output_buffer))) > 0)
      {
        if (write(new_job->client_socket, output_buffer, bytesRead) != bytesRead)
        {
          perror("write");
          close(read_fd);
          exit(EXIT_FAILURE);
        }
      }

      if (unlink(filename) < 0)
      {
        perror("unlink");
        exit(EXIT_FAILURE);
      }
      close(read_fd);
      printf("Executor:  job ended: %s\n", serializeJob(new_job));

      for (int i = 0; i < shared_data->current_jobs_size; i++)
      {
        if (shared_data->current_jobs[i] != NULL && shared_data->current_jobs[i]->pid == pid1)
        {
          // Afairoume to Job pou teleiose
          free(shared_data->current_jobs[i]);
          shared_data->current_jobs[i] = NULL;
          pthread_cond_broadcast(&queue_cond);

          printf("Executor:  Job with pid=%d removed\n", pid1);
          break;
        }
      }

      write(new_job->client_socket, "end", 4);
      pthread_mutex_unlock(&queue_mutex);
    }
  }

  pthread_exit(NULL);
}

void sigint_handler(int sig)
{
  while (waitpid(-1, NULL, WNOHANG) > 0)
    ;
}

void sigchld_handler(int sig)
{
  int saved_errno = errno;
  while (waitpid(-1, NULL, WNOHANG) > 0)
  {
    // Reap child processes
  }
  errno = saved_errno;
}
