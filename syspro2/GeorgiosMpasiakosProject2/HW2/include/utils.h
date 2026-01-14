#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <pthread.h>

typedef struct
{
    int job_id;
    char *job_name;
    int queue_pos;
    pid_t pid;
    int client_socket; //++
} Job;

char *serializeJob(Job *job);

typedef struct Node
{
    Job *pointer_job_n; // pointer to job
    struct Node *next;
} Node;

typedef struct
{
    Node *first; // Pointer to the first node in the queue
    Node *last;
    int size; // # of nodes
} Queue;

Queue *createQueue()
{
    Queue *q = (Queue *)malloc(sizeof(Queue));
    q->first = NULL;
    q->last = NULL;
    q->size = 0;
    return q;
}

int queue_add(Queue *q, Job *pointer_job_q_a)
{ // add job into queue
    Node *newNode = (Node *)malloc(sizeof(Node));
    newNode->pointer_job_n = pointer_job_q_a;
    newNode->next = NULL;

    if (q->last == NULL)
    { // elegxos an h queue einai adeia
        q->first = newNode;
        q->last = newNode;
    }
    else
    {
        q->last->next = newNode;
        q->last = newNode;
    }
    q->size++;
    return q->size; // thesh ths job sthn queue
}

void queue_print(Queue *q)
{
    Node *n = q->first; // Start from the first node
    while (n != NULL)   // Iterate through the nodes
    {
        printf("Queue \n\tid: %d\n", n->pointer_job_n->job_id);
        printf("\tname: %s\n", n->pointer_job_n->job_name);
        printf("\tposition: %d\n", n->pointer_job_n->queue_pos);
        n = n->next; // go to next node
    }
}

char *queue_serialize(Queue *q)
{ // Serialize jobs to string
    char *concatenated;
    int total_length = 0;

    Node *n = q->first; // Start from the first node
    while (n != NULL)
    {                                             // Iterate through the nodes
        char *s = serializeJob(n->pointer_job_n); // Serialize each
        total_length += strlen(s);                // synoliko length
        n = n->next;
    }

    concatenated = (char *)malloc(total_length + q->size + 1); // Allocate memory for the concatenated string
    if (concatenated == NULL)
    {
        printf("Memory allocation failed\n");
        return NULL;
    }

    concatenated[0] = '\0';
    int found = 0; // flag gia to an yparxei job
    n = q->first;
    while (n != NULL)
    {
        char *s = serializeJob(n->pointer_job_n); // Serialize each job
        strcat(concatenated, s);                  // Concatenate the serialized job
        strcat(concatenated, "\n");               // Add a newline character
        found = 1;
        free(s); //++
        n = n->next;
    }

    if (found == 0)
    {
        strcat(concatenated, "No jobs in queue");
    }

    return concatenated; // Return the concatenated string
}

void queue_sub(Queue *q)
{ // Afairesi tou mprostinou Job
    if (q->first == NULL)
    {
        printf("Executor:  Empty queue 1!\n");
    }
    else
    {                              //++
        q->first = q->first->next; // afou afaireitai to new first einai to epomeno

        if (q->first == NULL)
        {
            q->last = NULL;
        }

        q->size--;
    }
}

Job *queue_remove(Queue *q, int jobid)
{ // Afairesi sygkekrimenou job ID
    if (q->first == NULL)
    {
        printf("Executor:  Empty queue 2!\n");
        return NULL;
    }

    printf("Executor:  Not empty queue!\n");

    if(q->first->pointer_job_n->job_id == jobid)
    {
      if(q->first == q->last)
      {//ony one entry
        Node *temp = q->first; 
        q->first = NULL;
        q->last = NULL;
        q->size--;
        return temp->pointer_job_n; 
      }
      else
      {
        Node *temp = q->first; 
        q->first = q->first->next;
        q->size--;
        return temp->pointer_job_n; 
      }
    } 

    Node *n = q->first; // Start from the first node
    Node *prev = NULL;
    Job *j = NULL;      // Pointer to the removed job
    while (n != NULL && n->pointer_job_n->job_id != jobid)
    {
      prev = n;
      n = n->next;
    }

    if(n == NULL)
      return NULL;


    j = n->pointer_job_n; // Set the removed job pointer
    prev->next = n->next;

    if(prev->next == NULL)
      q->last = prev;

    if (q->first == NULL) // an einai adeia
    {
        q->last = NULL; // these ton last node pointer NULL
    }

    return j;
}

Job *queue_first(Queue *q)
{ // return deikth sto 1o stoixeio
    if (q->first == NULL)
    {
        printf("Executor:  Empty queue 3!\n");
        return NULL;
    }
    return q->first->pointer_job_n;
}

Job *queue_last(Queue *q)
{ // return deikth sto teleytaio stoixeio
    if (q->last == NULL)
    {
        printf("Executor:  H ουρά είναι άδεια\n");
        return NULL;
    }
    return q->last->pointer_job_n;
}

int size(Queue *q)
{
    return q->size;
}

char *serializeJob(Job *job)
{
    char *tmp = (char *)malloc(strlen(job->job_name) + 1);

    if (tmp == NULL)
    {
        printf("Memory allocation failed\n"); // Print error message if memory allocation fails
        return NULL;                          // Return NULL on failure
    }

    strncpy(tmp, job->job_name + 9, strlen(job->job_name) - 8); // Afairoume ta prwta 9 grammata --> "issueJob "

    int length = snprintf(NULL, 0, "<job_%d, %s, %d>", job->job_id, tmp, job->queue_pos); // Calculate serialized string length

    char *serialized = (char *)malloc(length + 1); // Allocate memory
    if (serialized == NULL)
    {
        printf("Memory allocation failed\n");
        free(tmp); //++
        return NULL;
    }

    snprintf(serialized, length + 1, "<job_%d, %s, %d>", job->job_id, tmp, job->queue_pos); // Serialize Job into string format

    return serialized;
}

int current_jobs_count(Job **jobs, int current_jobs_size)
{ // To synolo twn Jobs pou trexoun (pou einai ston current_jobs table)
    int total = 0;
    for (int i = 0; i < current_jobs_size; i++)
    { // Iterate through the jobs array
        if (jobs[i] != NULL)
        {
            total++;
        }
    }
    return total;
}

int current_jobs_get_first_empty(Job **jobs, int current_jobs_size)
{ // Vres to prwto adeio slot ston pinaka current_jobs
    for (int i = 0; i < current_jobs_size; i++)
    { // Iterate through the jobs array
        if (jobs[i] == NULL)
            return i; // Return the index of the first empty slot
    }
    return -1;
}

int countWords(char *str)
{ // Count the number of words in a string
    int count = 0;
    int isWord = 0; // flag

    while (*str)
    { // Iterate through the string
        if (*str != ' ' && !isWord)
        { // eimaste mesa se leksi
            isWord = 1;
            count++;
        }
        else if (*str == ' ' && isWord)
        { // Den eimaste pia se leksi
            isWord = 0;
        }
        str++;
    }
    return count;
}

char **getWordsExceptFirst(char *str, int numWords)
{                                                                // Epistrefei pinaka me oles tis lekseis ektos apo thn prwti
    char **words = (char **)malloc((numWords) * sizeof(char *)); // Allocate memory for words array
    if (words == NULL)
    {
        printf("Memory allocation failed\n");
        exit(1);
    }

    char *word;        // Pointer to hold each word
    int wordIndex = 0; // Index for the words array

    word = strtok(str, " ");  // Get the first word
    word = strtok(NULL, " "); // Skip first word
    while (word != NULL)
    {                                    // Iterate through remaining words
        words[wordIndex] = strdup(word); // Duplicate the word and store it in the array
        if (words[wordIndex] == NULL)
        {
            printf("Memory allocation failed\n");
            exit(1);
        }
        wordIndex++;
        word = strtok(NULL, " ");
    }

    words[numWords - 1] = NULL; // prosthetoume NULL teleutaia eggrafi (gia to execv/execvp)
    return words;               // Return the array of words
}
