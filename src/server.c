#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syscall.h>
#include <sys/un.h>

#define DIM_MSG 2048   // dimensione dei messaggi tra client e server
#define MAX_DIM_LEN 1024 // grandezza massima del contenuto di un file
#define UNIX_PATH_MAX 108 /* man 7 unix */
#define SOCKET_NAME "./serverSocket.sk"  // nome di default per il socket
#define LOG_NAME "./log.txt"    // nome di default per il file di log
#define TRUE 1
#define FALSE 0
#define SOFT_END 2
#define HARD_END 1

#ifdef SYS_gettid
#define gettid() ((pid_t)syscall(SYS_gettid))
#else
#define gettid() (pthread_self())
#endif

//creata per evitare di riscrivere sempre lo stesso pezzo di codice
#define SYSCALL_EXITF(sc, str) \
                         if((sc) == -1){ \
                         perror(str);    \
                         exit(EXIT_FAILURE); \
                         }
FILE* fileLog;
pthread_mutex_t logLock = PTHREAD_MUTEX_INITIALIZER;

typedef struct scnode{
    size_t client;
    struct scnode* next;
    struct scnode* prec;
} clNode;

typedef struct sclist{
    clNode* head;
    clNode* tail;
} cList;

typedef struct sfifonode{
    char* path;
    size_t frequency;
    struct timespec timeUsage;
    struct sfifonode* next;
    struct sfifonode* prec;
}FIFOnode;

typedef struct sfile{
    char* path;
    char* data;
    cList* openClient;
    size_t lockOwner;
    size_t op; //indica se il file è aperto o meno
    FIFOnode* FIFOfile; //usato per efficenza
    struct sfile* next;
    struct sfile* prec;
} file;

typedef struct sfilelst{
    file* head;
    file* tail;
    size_t size;
    pthread_mutex_t mtx;
}fileList;

typedef struct sfifo{
    FIFOnode* head;
    FIFOnode* tail;
    size_t dim;
} fifo;

typedef struct shash{
    fileList** buckets;
    size_t numBuckets;
}hash;

// variabili per lo storage  //
static size_t numThread;    // numero di thread worker del server
static size_t maxDIm;    // dimensione massima dello storage
static size_t numFileMax;      // numero massimo di files nello storage
static size_t currDim = 0;   // dimensione corrente dello storage
static size_t currNumFile = 0;     // numero corrente di files nello storage

static hash* storage = NULL;    // storage

static fifo* queue = NULL;      // struttura per il rimpiazzamento FIFO e LFU
pthread_mutex_t lockQueue = PTHREAD_MUTEX_INITIALIZER;

static cList* clientList = NULL;     // struttura dati di tipo coda FIFO per la comunicazione Master/Worker (uso improprio della nomenclatura "client")
pthread_mutex_t lockClientList = PTHREAD_MUTEX_INITIALIZER; // mutex per mutua esclusione sugli accessi alla coda
pthread_cond_t notEmpty = PTHREAD_COND_INITIALIZER;

volatile sig_atomic_t end= 0; // variabile settata dal signal handler

//  VARIABILI PER LE STATISTICHE //
pthread_mutex_t lockStats = PTHREAD_MUTEX_INITIALIZER; // mutex per mutua esclusione sugli accessi alle var. per stats.

static size_t numLists;
static size_t maxDimReached = 0;    // dimensione massima dello storage raggiunta
static size_t maxDimReachedMB = 0;    // dimensione massima dello storage raggiunta in MB
static size_t maxDimReachedKB = 0; //dimensione massima in KB
static size_t numFileMaxReached = 0;      // numero massimo di files nello storage reggiunto
static size_t replacement = 0;   // numero di rimpiazzamenti per file terminati con successo
static size_t numReplaceAlgo = 0;   // numero di volte in cui l'algoritmo di rimpiazzamento è stato attivato
static size_t numRead = 0;  // numero di operazioni read terminate con successo
static size_t dimRead = 0;  // dimensione totale delle letture terminate con successo
static size_t dimReadMedia = 0;  // dimensione media delle letture terminate con successo
static size_t numWrite = 0; // numero di operazioni write terminate con successo
static size_t dimWrite = 0; // dimensione totale delle scritture terminate con successo
static size_t dimWriteMedia = 0; // dimensione media delle scritture terminate con successo
static size_t numLock = 0; // numero di operazioni lock terminate con successo
static size_t numUnlock = 0; // numero di operazioni unlock terminate con successo
static size_t numOpenLock = 0; // numero di operazioni open con flag O_LOCK == 1 terminate con successo
static size_t numClose = 0; // numero di operazioni close avvenute con successo
static size_t maxNumConnections = 0;   // numero massimo di connessioni contemporanee raggiunto
static size_t numConnectionsCurr = 0;   // numero di connessioni correnti

//    Versione vista a lezione   //
static void Pthread_mutex_lock (pthread_mutex_t* mtx){
    int err;
    if ((err=pthread_mutex_lock(mtx)) != 0 ){
        errno = err;
        perror("lock");
        pthread_exit((void*)errno);
    }
}
static void Pthread_mutex_unlock (pthread_mutex_t* mtx){
    int err;
    if ((err=pthread_mutex_unlock(mtx)) != 0 ){
        errno = err;
        perror("unlock");
        pthread_exit((void*)errno);
    }
}
static clNode* clNodeCreate (size_t client){
    if (client == 0){
        errno = EINVAL;
        return NULL;
    }

    clNode* tmp = malloc(sizeof(clNode));
    if (tmp == NULL){
        free(tmp);
        errno = ENOMEM;
        return NULL;
    }

    tmp -> next = NULL;
    tmp -> prec = NULL;
    tmp -> client = client;

    return tmp;
}

static cList* cListCreate (){
    cList* tmp = malloc(sizeof(cList));
    if (tmp == NULL){
        errno = ENOMEM;
        free(tmp);
        return NULL;
    }
    tmp->head = NULL;
    tmp->tail = NULL;

    return tmp;
}

static void cListFree (cList* lst){
    if (lst == NULL){
        errno = EINVAL;
        return;
    }
    clNode* tmp = lst -> head;
    while (tmp != NULL){
        lst->head = lst->head->next;
        free(tmp);
        tmp = lst->head;
    }

    free(lst);
}

static int cListContains (cList* lst, size_t client){
    if (lst == NULL || client == 0){
        errno = EINVAL;
        return -1;
    }

    clNode* cursor = lst->head;
    while (cursor != NULL){
        if (cursor->client == client)
            return 1;
        cursor = cursor->next;
    }
    //errno = ENOENT;
    return 0;
}

static int cListAddHead (cList* lst, size_t client){
    if (lst == NULL || client == 0){
        errno = EINVAL;
        return -1;
    }

    clNode* tmp = clNodeCreate(client);
    if (lst->head == NULL){
        lst->head = tmp;
        lst->tail = tmp;
    }
    else{
        lst->head->prec = tmp;
        tmp->next = lst->head;
        lst->head = tmp;
    }
    return 0;
}

static int ClistPopWorker (cList* lst){
    if (lst == NULL){
        errno = EINVAL;
        return -2;
    }
    while (lst -> head == NULL){
        pthread_cond_wait(&notEmpty,&lockClientList); // attesa del segnale inviato dal thread main
    }
    size_t out = lst -> tail -> client;

    if (lst->head == lst->tail){
        free(lst->tail);
        lst->tail = NULL;
        lst->head = NULL;
        return (int)out;
    }
    else{
        clNode* tmp = lst->tail;
        lst -> tail = lst -> tail -> prec;
        lst -> tail -> next = NULL;
        free(tmp);
        return (int)out;
    }

}

static int cListRemoveNode (cList* lst, size_t client){
    if (lst == NULL || client == 0){
        errno = EINVAL;
        return -1;
    }

    if (lst->head == NULL){
        return 0;
    }

    clNode* cursor = lst->head;
    // eliminazione di un nodo in testa
    if ( lst->head != NULL){
        if (client == lst->head->client) {
            if (lst->head->next != NULL){
                lst->head->next->prec = NULL;
                lst->head = lst->head->next;

                free(cursor);
                return 0;
            }
            else{
                lst->head = NULL;
                lst->tail = NULL;

                free(cursor);
                return 0;
            }
        }
        cursor = lst->head->next;
    }

    while (cursor != NULL){
        if (cursor->client == client){
            if (lst->tail->client == client) {
                lst->tail->prec->next = NULL;
                lst->tail = lst->tail->prec;
                free(cursor);
                return 0;
            }
            else{
                cursor->prec->next = cursor->next;
                cursor->next->prec = cursor->prec;
                free(cursor);
                return 0;
            }
        }
        cursor = cursor->next;
    }

    return -1;
}


static file* fileCreate (char* path, char* data, size_t lockClient){
    if (path == NULL || data == NULL){
        errno = EINVAL;
        return NULL;
    }

    size_t pathLen = UNIX_PATH_MAX;
    size_t dataDim = strlen(data);
    if (pathLen > UNIX_PATH_MAX){
        errno = ENAMETOOLONG;
        return NULL;
    }
    if (dataDim > MAX_DIM_LEN){
        errno = ENAMETOOLONG;
        return NULL;
    }

    file* tmp = malloc(sizeof(file));
    if (tmp == NULL){
        errno = ENOMEM;
        free(tmp);
        return NULL;
    }

    tmp -> path = malloc(sizeof(char)*UNIX_PATH_MAX);
    if(tmp -> path == NULL){
        errno = ENOMEM;
        free(tmp -> path);
        return NULL;
    }
    strcpy(tmp->path,path);
    tmp -> data = malloc(sizeof(char)*MAX_DIM_LEN);
    if(tmp -> data == NULL){
        errno = ENOMEM;
        free(tmp -> data);
        return NULL;
    }
    strcpy(tmp->data,data);

    tmp -> lockOwner = lockClient;
    tmp -> op = 0;
    tmp -> openClient = cListCreate();
    if (tmp -> openClient == NULL)
        return NULL;

    tmp -> next = NULL;
    tmp -> prec = NULL;

    return tmp;
}

static void freeFile (file* file1){
    if (file1 == NULL)  
        return;
    cListFree(file1 -> openClient);
    free(file1 -> path);
    free(file1 -> data);
    free(file1);
}

static file* cloneFile (file* file1){
    if (file1 == NULL)
        return NULL;

    file* copy = malloc(sizeof(file));
    if (copy == NULL){
        errno = ENOMEM;
        free(copy);
        return NULL;
    }
    size_t pathLen = strnlen(file1 -> path, UNIX_PATH_MAX);
    size_t dataDim = strlen(file1 -> data);
    copy->next = NULL;
    copy->prec = NULL;
    copy->openClient = NULL;
    copy->lockOwner = file1->lockOwner;
    copy->path = malloc(sizeof(char)*(pathLen+1));
    if (copy->path == NULL){
        errno = ENOMEM;
        free(copy->path);
        return NULL;
    }
    strcpy(copy->path,file1->path);

    copy->data = malloc(sizeof(char)*(dataDim+1));
    if (copy->data == NULL){
        errno = ENOMEM;
        free(copy->data);
        return NULL;
    }
    strcpy(copy->data,file1->data);

    return copy;
}

static FIFOnode* FIFOnodeCreate (char* path){
    FIFOnode* tmp = malloc(sizeof(FIFOnode));
    if (tmp == NULL){
        errno = ENOMEM;
        free(tmp);
        return NULL;
    }
    tmp->next = NULL;
    tmp->prec = NULL;
    tmp -> frequency = 0;
    clock_gettime(CLOCK_REALTIME, &(tmp -> timeUsage));
    tmp->path = malloc(sizeof(char)*UNIX_PATH_MAX);
    if(tmp->path == NULL){
        errno = ENOMEM;
        return NULL;
    }
    strcpy(tmp->path, path);
    return tmp;
}

static void FIFOnodeFree (FIFOnode* node1){
    if (node1 == NULL) 
        return;
    free(node1 -> path);
    free(node1);
}
static fifo* FIFOcreate (){
    fifo* tmp = malloc(sizeof(fifo));
    if (tmp == NULL){
        errno = ENOMEM;
        free(tmp);
        return NULL;
    }
    tmp->head = NULL;
    tmp->tail = NULL;
    tmp -> dim = 0;

    return tmp;
}
static void FIFOFree (fifo* lst){
    if (lst == NULL){
        errno = EINVAL;
        return;
    }
    FIFOnode* tmp = lst->head;
    while (tmp != NULL){
        lst->head = lst->head->next;
        FIFOnodeFree(tmp);
        tmp = lst->head;
    }
    lst->tail = NULL;
    free(lst);
}

static int FIFOAdd (fifo* lst, FIFOnode* file1){
    Pthread_mutex_lock(&lockQueue);

    if (lst == NULL || file1 == NULL){
        errno = EINVAL;
        Pthread_mutex_unlock(&lockQueue);
        return -1;
    }

    if (lst->head == NULL){
        lst->head = file1;
        lst->tail = file1;
    }
    else{
        lst->head->prec = file1;
        file1->next = lst->head;
        lst->head = file1;
    }
    lst -> dim++;
    Pthread_mutex_unlock(&lockQueue);

    return 0;
}

static int FIFORemove (fifo* lst, char* path){
    Pthread_mutex_lock(&lockQueue);

    if (lst == NULL || path == NULL){
        errno = EINVAL;
        Pthread_mutex_unlock(&lockQueue);
        return -1;
    }

    if (lst->head == NULL){
        Pthread_mutex_unlock(&lockQueue);
        return 0;
    }
    FIFOnode * cursor = lst->head;
    // eliminazione di un nodo in testa
    if (!strncmp(lst->head->path, path,UNIX_PATH_MAX)) {
        if(lst->head->next != NULL){
            lst->head->next->prec = NULL;
            lst->head = lst->head->next;
            FIFOnodeFree(cursor);
            lst -> dim--;
            Pthread_mutex_unlock(&lockQueue);
            return 0;
        }
        else{
            lst->head = NULL;
            lst->tail = NULL;

            FIFOnodeFree(cursor);
            lst -> dim--;
            Pthread_mutex_unlock(&lockQueue);
            return 0;
        }
    }

    cursor = lst->head->next;
    while (cursor != NULL){
        if (!strncmp(cursor->path,path,UNIX_PATH_MAX)){
            if (!strncmp(lst->tail->path, path,UNIX_PATH_MAX)) {
                lst->tail->prec->next = NULL;
                lst->tail = lst->tail->prec;
                FIFOnodeFree(cursor);
                lst -> dim--;
                Pthread_mutex_unlock(&lockQueue);
                return 0;
            }
            else{
                cursor->prec->next = cursor->next;
                cursor->next->prec = cursor->prec;
                FIFOnodeFree(cursor);
                lst -> dim--;
                Pthread_mutex_unlock(&lockQueue);
                return 0;
            }
        }
        cursor = cursor->next;
    }
    
    Pthread_mutex_unlock(&lockQueue);
    return -1;
}

static fileList* fileListCreate (){
    fileList* tmp = malloc(sizeof(fileList));
    if (tmp == NULL){
        errno = ENOMEM;
        free(tmp);
        return NULL;
    }
    tmp->head = NULL;
    tmp->tail = NULL;
    tmp->size = 0;
    tmp->mtx = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;

    return tmp;
}

static void fileListFree (fileList* lst){
    if (lst == NULL){
        errno = EINVAL;
        return;
    }

    file* tmp = lst->head;
    while (tmp != NULL){
        lst->head = lst->head->next;
        freeFile(tmp);
        tmp = lst->head;
    }

    lst->tail = NULL;
    free(lst);
}

static void fileListPrint (fileList* lst){
    if (lst == NULL){
        printf("NULL\n");
        return;
    }

    printf("%ld // ", lst->size);

    file* cursor = lst->head;

    while (cursor != NULL){
        printf("%s <-> ", cursor->path);
        cursor = cursor->next;
    }
    printf("FINE\n");
}

static file* fileListGetFile (fileList* lst, char* path){
    if (lst == NULL || path == NULL ){
        errno = EINVAL;
        return NULL;
    }

    file* cursor = lst->head;
    while (cursor != NULL){
        if (!strncmp(cursor->path,path,UNIX_PATH_MAX)) return cursor;

        cursor = cursor->next;
    }

    return NULL;
}

static int fileListContains (fileList* lst, char* path){
    if (lst == NULL || path == NULL){
        errno = EINVAL;
        return -1;
    }

    file* cursor = lst->head;
    while (cursor != NULL){
        if (!strncmp(cursor->path,path,UNIX_PATH_MAX))
            return 1;
        cursor = cursor->next;
    }
    return 0;
}

static int fileListAddHead (fileList* lst, file* file1){
    if (lst == NULL || file1 == NULL){
        errno = EINVAL;
        return -1;
    }
    int isInList = fileListContains(lst,file1->path);
    if (isInList == -1)
        return -1;

    if (isInList) {
        errno = EEXIST;
        return -1;
    }

    if (lst->head == NULL){
        lst->head = file1;
        lst->tail = file1;
    }
    else{
        lst->head->prec = file1;
        file1->next = lst->head;
        lst->head = file1;
    }

    lst->size++;
    return 0;
}

static file* fileListRemove (fileList* lst, char* path){
    if (lst == NULL || path == NULL){
        errno = EINVAL;
        return NULL;
    }

    if (lst->head == NULL){
        return NULL;
    }
    file* cursor = lst->head;
    // eliminazione di un nodo in testa
    if (!strncmp(lst->head->path, path, UNIX_PATH_MAX)) {
        if(lst->head->next != NULL){
            lst->head->next->prec = NULL;
            lst->head = lst->head->next;
            file* out = cloneFile(cursor);
            freeFile(cursor);
            lst->size--;
            return out;
        }
        else{
            lst->head = NULL;
            lst->tail = NULL;
            file* out = cloneFile(cursor);
            freeFile(cursor);
            lst->size--;
            return out;
        }
    }

    cursor = lst->head->next;
    while (cursor != NULL){
        if (!strncmp(cursor->path,path,UNIX_PATH_MAX)){
            if (!strncmp(lst->tail->path, path,UNIX_PATH_MAX)) {
                lst->tail->prec->next = NULL;
                lst->tail = lst->tail->prec;
                file* out = cloneFile(cursor);
                freeFile(cursor);
                lst->size--;
                return out;
            }
            else{
                cursor->prec->next = cursor->next;
                cursor->next->prec = cursor->prec;
                file* out = cloneFile(cursor);
                freeFile(cursor);
                lst->size--;
                return out;
            }
        }
        cursor = cursor->next;
    }
    return NULL;
}


static hash* hashCreate (size_t numBuckets){
    if (numBuckets == 0){
        errno = EINVAL;
        return NULL;
    }

    hash* tmp = malloc(sizeof(hash));
    if (tmp == NULL){
        errno = ENOMEM;
        free(tmp);
        return NULL;
    }
    tmp->numBuckets = numBuckets;
    tmp->buckets = malloc(sizeof(fileList*)*numBuckets);
    if (tmp->buckets == NULL){
        errno = ENOMEM;
        free(tmp->buckets);
        return NULL;
    }

    int i;
    for (i = 0;i < numBuckets; i++){
        tmp->buckets[i] = fileListCreate();
        if(tmp->buckets[i] == NULL){
            errno = ENOMEM;
            return NULL;
        }
    }

    return tmp;
}

static long long hashFunction (const char* string){
    if (string == NULL){
        errno = EINVAL;
        return -1;
    }
    const int p = 47;
    const int m = (int) 1e9 + 9;
    long long h_v = 0;
    long long p_pow = 1;

    int i = 0;
    while(string[i] != '\0')
    {
        h_v = (h_v + (string[i] - 'a' + 1) * p_pow) % m;
        p_pow = (p_pow * p) % m;
        i++;
    }
    return h_v;
}

static FIFOnode* hashAdd (hash* tbl, file* file1){
    if (tbl == NULL || file1 == NULL){
        errno = EINVAL;
        return NULL;
    }

    size_t hsh = hashFunction(file1->path) % tbl->numBuckets;
    if(hsh != -1){
        FIFOnode* newFile = FIFOnodeCreate(file1->path);

        if((fileListAddHead(tbl -> buckets[hsh], file1)) != 0){
            return NULL;
        }
        tbl -> buckets[hsh] -> size++;

        if ((FIFOAdd(queue, newFile)) == 0){
            Pthread_mutex_lock(&lockStats);
            currNumFile++;
            currDim = currDim + strlen(file1->data);
            if(currDim > maxDimReached) 
                maxDimReached = currDim;
            Pthread_mutex_unlock(&lockStats);
            return newFile;
        }
    }

   return NULL;
}

static file* hashRemove (hash* tbl, char* path){
    if (tbl == NULL || path == NULL){
        errno = EINVAL;
        return NULL;
    }

    size_t hsh = hashFunction(path) % tbl->numBuckets;

    if(hsh != -1 ){
        file* tmp = fileListRemove(tbl->buckets[hsh],path);
        if (tmp != NULL && (FIFORemove(queue,tmp->path)) == 0){
            Pthread_mutex_lock(&lockStats);
            currNumFile--;
            currDim = currDim - strlen(tmp->data);
            Pthread_mutex_unlock(&lockStats);
            return tmp;
        }
    }
    return NULL;
}

static file* hashGetFile (hash* tbl, char* path){
    if (tbl == NULL || path == NULL){
        errno = EINVAL;
        return NULL;
    }

    size_t hsh = hashFunction(path) % tbl->numBuckets;

    if(hsh != -1){
        return fileListGetFile(tbl->buckets[hsh], path);
    }
    return NULL;
}

static fileList* hashGetList (hash* tbl, char* path){
    if (tbl == NULL || path == NULL){
        errno = EINVAL;
        return NULL;
    }

    size_t hsh = hashFunction(path) % tbl->numBuckets;

    if(hsh != -1){
        return tbl->buckets[hsh];
    }
    return NULL;
}

static void hashFree (hash* tbl){
    if (tbl == NULL) return;

    int i;
    for (i = 0; i < tbl->numBuckets; i++){
        fileListFree(tbl->buckets[i]);
    }
    free(tbl->buckets);
    free(tbl);
}

static void hashPrint (hash* tbl){
    if(tbl == NULL){
        printf("NULL\n");
        return;
    }

    int i;
    for(i = 0; i<tbl->numBuckets; i++){
        if(tbl->buckets[i] != NULL)
        fileListPrint(tbl->buckets[i]);
    }

    printf("FINE\n");
}

static int hashContains (hash* tbl, char* path){
    if (tbl == NULL || path == NULL){
        errno = EINVAL;
        return -1;
    }

    size_t hsh = hashFunction(path) % tbl->numBuckets;

    if(hsh != -1){
        return fileListContains(tbl->buckets[hsh], path);
    }
    return -1;
}

void swap ( size_t* a, size_t* b, char* aa, char* bb){
    char* tmp = malloc(sizeof(char) * UNIX_PATH_MAX);
    strncpy(tmp, aa, UNIX_PATH_MAX);
    strncpy(aa, bb, UNIX_PATH_MAX);
    strncpy(bb, tmp, UNIX_PATH_MAX);
    size_t t = *a;
    *a = *b;
    *b = t;
}



FIFOnode* partition( FIFOnode* l, FIFOnode* h){
    size_t x = h->frequency;
    FIFOnode* i = l->prec;

    FIFOnode* j;
    for (j = l; j != h; j = j->next){
        if (j->frequency >= x){
            i = (i == NULL) ? l : i->next;

            swap(&(i->frequency), &(j->frequency), i->path, j->path);
        }
    }
    i = (i == NULL) ? l : i->next; // Similar to i++
    swap(&(i->frequency), &(h->frequency), i->path, h->path);
    return i;
}

void FquickSort(FIFOnode* l, FIFOnode* h){
    if (h != NULL && l != h && l != h->next){
        FIFOnode* p = partition(l, h);
        FquickSort(l, p->prec);
        FquickSort(p->next, h);
    }
}

void quickSortLFU(fifo* lst){
    FIFOnode* h = lst -> tail;

    FquickSort(lst->head, h);
}

FIFOnode* copy(FIFOnode* cp){
    if(cp == NULL){
        errno = EINVAL;
        return NULL;
    }
    FIFOnode* tmp = FIFOnodeCreate(cp -> path);
    tmp -> frequency = cp -> frequency;
    tmp -> timeUsage = cp -> timeUsage;

    return tmp;
}

static void lockStorage(hash* tbl){
    if(tbl == NULL){
        errno = EINVAL;
        return;
    }

    int i;
    for(i = 0; i < tbl->numBuckets; i++)
        Pthread_mutex_lock(&(tbl->buckets[i]->mtx));
}

static void unlockStorage(hash* tbl){
    if(tbl == NULL){
        errno = EINVAL;
        return;
    }

    int i;
    for(i = 0; i < tbl->numBuckets; i++)
        Pthread_mutex_unlock(&(tbl->buckets[i]->mtx));
}

static fileList* hashReplaceLFU(hash* tbl, const char* path, size_t client){
    if(tbl == NULL || path == NULL || client < 0){
        errno = EINVAL;
        return NULL;
    }
    lockStorage(storage);
    if(errno != 0)
        return NULL;
    fileList* replaced = fileListCreate(); // inizializzazione della lista output
    if (replaced == NULL){
        errno = ENOMEM;
        unlockStorage(storage);
        fileListFree(replaced);
        return NULL;
    }

    int bool = 0; // flag per evidenziare l'avvio o meno dell'algoritmo per i rimpiazzamenti
    FIFOnode* killedFile = NULL;
    fifo* queueCopy = FIFOcreate();
    if (queueCopy == NULL){
        unlockStorage(storage);
        fileListFree(replaced);
        errno = ENOMEM;
        return NULL;
    }
    Pthread_mutex_lock(&lockStats);
    if(currDim > maxDIm){ //ordino solo se necessario
        FIFOnode* tmp = queue -> head;
        while(tmp){
            int out = FIFOAdd(queueCopy, copy(tmp));
            if(out == -1) {
                fileListFree(replaced);
                return NULL;
            }
            tmp = tmp -> next;
        }
        quickSortLFU(queueCopy);
    }
    Pthread_mutex_unlock(&lockStats);



    while (currDim > maxDIm){ // finchè la dimensione corrente è maggiore della massima
        bool = 1; // la flag viene impostata  per indicare che l'algoritmo di rimpiazzamento è di fatto partito
        if (killedFile == NULL)
            killedFile = queueCopy->tail;
        if (killedFile == NULL){
            errno = EFBIG;
            unlockStorage(storage);
            fileListFree(replaced);
            return NULL;
        }

        file* removedFile = hashGetFile(storage, killedFile->path); // il punatatore al file individuato con politica FIFO viene ottenuto
        if (removedFile == NULL){
            unlockStorage(storage);
            fileListFree(replaced);
            return NULL;
        }

        while ((removedFile->lockOwner != 0 && removedFile->lockOwner != client) || strcmp(killedFile->path, path) == 0){
            killedFile = killedFile->prec;
            if (killedFile == NULL){
                errno = EFBIG;
                unlockStorage(storage);
                fileListFree(replaced);
                return NULL;
            }
            removedFile = hashGetFile(storage,killedFile->path);
            if (removedFile == NULL){
                unlockStorage(storage);
                fileListFree(replaced);
                return NULL;
            }
        }

        file* copy = cloneFile(removedFile); // il file rimovibile può essere rimosso
        if (copy == NULL) {
            unlockStorage(storage);
            fileListFree(replaced);
            return NULL;
        }
        if (hashRemove(storage, removedFile ->path) == NULL) {
            unlockStorage(storage);
            fileListFree(replaced);
            return NULL;
        }
        killedFile = killedFile->prec;
        if (fileListAddHead(replaced,copy) == -1) {
            unlockStorage(storage);
            fileListFree(replaced);
            return NULL;
        }
        Pthread_mutex_lock(&lockStats);
        replacement++;   // le statistiche vengono aggiornate
        Pthread_mutex_unlock(&lockStats);

    }
    Pthread_mutex_lock(&lockStats);
    if (bool == 1)
        numReplaceAlgo++;// le statistiche vengono aggiornate
    if (currDim > maxDimReached)
        maxDimReached = currDim;
    Pthread_mutex_unlock(&lockStats);
    unlockStorage(storage);
    FIFOFree(queueCopy);
    return replaced;
}

static fileList* hashReplaceFIFO (hash* tbl, const char* path, size_t client){
    if (tbl == NULL || client == 0 || path == NULL){
        errno = EINVAL;
        return NULL;
    }
    lockStorage(storage); //mi assicuro di bloccare tutto lo storage
    if(errno != 0)
        return NULL;
    fileList* replaced = fileListCreate(); // inizializzazione della lista output
    if (replaced == NULL){
        errno = ENOMEM;
        unlockStorage(storage);
        Pthread_mutex_unlock(&lockStats);
        fileListFree(replaced);
        return NULL;
    }

    int bool = 0; // flag per evidenziare l'avvio o meno dell'algoritmo per i rimpiazzamenti
    FIFOnode* killedFile = NULL;


    while (currDim > maxDIm){
        bool = 1; // la flag viene impostata  per indicare che l'algoritmo di rimpiazzamento è partito
        if (killedFile == NULL)
            killedFile = queue->tail; // la coda fifo verrà effettivamente percorsa dal primo elemento inserito verso l'ultimo
        if (killedFile == NULL){
            errno = EFBIG;
            unlockStorage(storage);
            fileListFree(replaced);
            return NULL;
        }

        file* removedFile = hashGetFile(storage, killedFile->path); // il punatatore al file individuato con politica FIFO viene ottenuto
        if (removedFile == NULL){
            unlockStorage(storage);
            fileListFree(replaced);
            return NULL;
        }

        while ((removedFile->lockOwner != 0 && removedFile->lockOwner != client) || strcmp(killedFile->path, path) == 0){ // fin quando il file rimovibile individuato non rispetta le caratteristiche per poter essere rimosso esso viene ignorato
            // il file non deve essere lockato da altri client e inoltre non deve essere quello causa del superamento dei limiti
            killedFile = killedFile->prec;
            if (killedFile == NULL){
                errno = EFBIG;
                unlockStorage(storage);
                fileListFree(replaced);
                return NULL;
            }

            removedFile = hashGetFile(storage,killedFile->path);
            if (removedFile == NULL){
                unlockStorage(storage);
                fileListFree(replaced);
                return NULL;
            }
        }

        file* copy = cloneFile(removedFile); // il file rimovibile può essere rimosso
        if (copy == NULL) {
            unlockStorage(storage);
            fileListFree(replaced);
            return NULL;
        }
        killedFile = killedFile->prec;
        if (hashRemove(storage, removedFile ->path) == NULL) {
            unlockStorage(storage);
            fileListFree(replaced);
            return NULL; // il file rimovibile viene rimosso
        }
        if (fileListAddHead(replaced,copy) == -1) {
            unlockStorage(storage);
            fileListFree(replaced);
            return NULL;
        }
        replacement++;   // le statistiche vengono aggiornate
    }
    Pthread_mutex_lock(&lockStats);
    if (bool == 1)
        numReplaceAlgo++;// le statistiche vengono aggiornate
    if (currDim > maxDimReached) 
        maxDimReached = currDim;
    unlockStorage(storage);
    Pthread_mutex_unlock(&lockStats);
    return replaced;
}

//puntatore alla funzione di politica di rimpiazzamento
fileList* (*hashReplace) (hash*, const char*, size_t) = hashReplaceFIFO; //default politica FIFO
//una volta terminata la connessione vengono settati a 0 tutti i file lockat dal client disconnesso
static void resetLockOwner (hash* tbl, size_t clientFD){
    if (tbl == NULL)
        return;

    int i;
    file* cursor;

    for(i=0; i<tbl->numBuckets; i++){
        Pthread_mutex_lock(&(tbl->buckets[i]->mtx));
        cursor = tbl->buckets[i]->head;

        while(cursor != NULL){
            if (cursor->lockOwner == clientFD) 
                cursor->lockOwner = 0;
            cursor = cursor->next;
        }
        Pthread_mutex_unlock(&(tbl->buckets[i]->mtx));
    }
}

//    FUNZIONI PER IL SERVER   //
static int isNumber(const char* s, long* n){
    if (s == NULL) return 0;
    if (strlen(s) == 0) return 0;
    char* e = NULL;
    errno = 0;
    long val = strtol(s, &e, 10);
    if (errno == ERANGE)
        return 2;    // overflow
    if (e != NULL && *e == (char)0) {
        *n = val;
        return 1;   // successo
    }
    return 0;   // non e' un numero
}
//gestore dei segnali SIGHUP, SIGINT, SIGQUIT
static void sigHandler (int sgnl){
    if (sgnl == SIGINT || sgnl == SIGQUIT)
        end = HARD_END; //terminazione bruta
    else if (sgnl == SIGHUP)
        end = SOFT_END; //terminazione soft
}
//aggiorna il fd massimo(utile per la select)
static int updateMax (fd_set set, int fdmax){
    int i;
    for(i = (fdmax-1); i >= 0; i--)
        if (FD_ISSET(i, &set))
            return i;
    return -1;
}

int readn(long fd, void *buf, size_t size) {
    int readn = 0;
    int r = 0;

    while ( readn < size ){

        if ( (r = (int)read((int)fd, buf, size)) == -1 ){
            if( errno == EINTR )
                // se la read è stata interrotta da un segnale riprende
                continue;
            else{
                perror("Readn");
                return -1;
            }
        }
        if ( r == 0 )
            return readn; // Nessun byte da leggere rimasto

        readn += r;
    }

    return readn;
}

int writen(long fd, const void *buf, size_t nbyte){
    int writen = 0;
    int w = 0;

    while ( writen < nbyte ){
        if ( (w = (int)write((int)fd, buf, nbyte) ) == -1 ){
            /* se la write è stata interrotta da un segnale riprende */
            if ( errno == EINTR )
                continue;
            else if ( errno == EPIPE )
                break;
            else{
                perror("Writen");
                return -1;
            }
        }
        if( w == 0 )
            return writen;

        writen += w;
    }

    return writen;
}


// Le seguenti operazioni svolgono le richieste delle funzioni nella API //
/* s_openFile : FLAGS
 * 0 -> 00 -> O_CREATE = 0 && O_LOCK = 0
 * 1 -> 01 -> O_CREATE = 0 && O_LOCK = 1
 * 2 -> 10 -> O_CREATE = 1 && O_LOCK = 0
 * 3 -> 11 -> O_CREATE = 1 && O_LOCK = 1
 */
static int s_openFile (char* path, int flags, size_t client){
    if (path == NULL || flags < 0 || flags > 3){
        errno = EINVAL;
        return -1;
    }

    int ex = hashContains(storage, path);
    if (ex == -1)
        return -1;

    switch (flags){
        case 0 : {
            if (!ex) {
                errno = ENOENT;
                return -1;
            }

            file *tmp = hashGetFile(storage, path);
            if (tmp == NULL)
                return -1;
            fileList *tmpList = hashGetList(storage, path);
            if (tmpList == NULL)
                return -1;

            Pthread_mutex_lock(&(tmpList->mtx));

            if (tmp->lockOwner == 0 || tmp->lockOwner == client){
                if (cListAddHead(tmp->openClient, client) == -1){
                    Pthread_mutex_unlock(&(tmpList->mtx));
                    return -1;
                }
                tmp->op = 1;
                Pthread_mutex_unlock(&(tmpList->mtx));
                return 0;
            }
            Pthread_mutex_unlock(&(tmpList->mtx));
            errno = EPERM;
            return -1;
        }

        case 1 :{
            if (!ex){
                errno = ENOENT;
                return -1;
            }

            file* tmp = hashGetFile(storage,path);
            if(tmp == NULL)
                return -1;

            fileList* tmpList = hashGetList(storage,path);
            if(tmpList == NULL)
                return -1;

            Pthread_mutex_lock(&(tmpList->mtx));
            Pthread_mutex_lock(&lockStats);
            numOpenLock++;
            numLock++;
            Pthread_mutex_unlock(&lockStats);

            if( tmp->lockOwner == 0 || tmp->lockOwner == client){
                tmp->lockOwner = client;
                if (cListAddHead(tmp->openClient, client) == -1) {
                    Pthread_mutex_unlock(&(tmpList->mtx));
                    return -1;
                }
                tmp->op = 1;

                Pthread_mutex_unlock(&(tmpList->mtx));
                return 0;
            }
            Pthread_mutex_unlock(&(tmpList->mtx));
            errno = EPERM;
            return -1;

        }

        case 2 :{
            if (ex){
                errno = EEXIST;
                return -1;
            }

            int cond = 0;
            Pthread_mutex_lock(&lockStats);
            if (currNumFile < numFileMax)
                cond = 1;
            Pthread_mutex_unlock(&lockStats);

            if (cond){
                file *tmp = fileCreate(path, "", 0);
                if (tmp == NULL)
                    return -1;

                tmp->op = 1;
                if(cListAddHead(tmp->openClient,client) == -1)
                    return -1;

                fileList* tmpList = hashGetList(storage, path);
                if (tmpList == NULL)
                    return -1;

                Pthread_mutex_lock(&tmpList->mtx);
                FIFOnode* tmpp = hashAdd(storage, tmp);
                if (tmpp == NULL){
                    Pthread_mutex_unlock(&tmpList->mtx);
                    return -1;
                }
                tmp -> FIFOfile = tmpp;
                Pthread_mutex_lock(&lockStats);
                if (currNumFile>numFileMaxReached)
                    numFileMaxReached = currNumFile;
                Pthread_mutex_unlock(&lockStats);

                Pthread_mutex_unlock(&tmpList->mtx);
                return 0;
            }
            errno = ENFILE;
            return -1;
        }

        case 3 :{
            if (ex){
                errno = EEXIST;
                return -1;
            }

            int cond = 0;
            Pthread_mutex_lock(&lockStats);
            if (currNumFile < numFileMax)
                cond = 1;
            Pthread_mutex_unlock(&lockStats);

            if (cond){
                file *tmp = fileCreate(path, "", client);
                if (tmp == NULL)
                    return -1;

                fileList *tmpList = hashGetList(storage,path);
                if (tmpList == NULL)
                    return -1;

                Pthread_mutex_lock(&(tmpList->mtx));

                tmp->op = 1;
                if(cListAddHead(tmp->openClient,client) == -1){
                    Pthread_mutex_unlock(&(tmpList->mtx));
                    return -1;
                }

                FIFOnode* tmpp = hashAdd(storage, tmp);
                if (tmpp == NULL){
                    Pthread_mutex_unlock(&tmpList->mtx);
                    return -1;
                }
                tmp -> FIFOfile = tmpp;

                Pthread_mutex_lock(&lockStats);
                numOpenLock++;
                numLock++;
                if (currNumFile>numFileMaxReached)
                    numFileMaxReached = currNumFile;
                Pthread_mutex_unlock(&lockStats);

                Pthread_mutex_unlock(&(tmpList->mtx));
                return 0;
            }

            errno = ENFILE;
            return -1;
         }

        default :{
             errno = EINVAL;
             return -1;
         }
    }
}
static int s_readFile (char* path, char* buf, size_t* size, size_t client){
    if (path == NULL){
        errno = EINVAL;
        return -1;
    }

    file* tmp = hashGetFile(storage,path);
    if (tmp == NULL)
        return -1;
    fileList* tmpList = hashGetList(storage,path);
    if (tmp == NULL)
        return -1;

    Pthread_mutex_lock(&tmpList->mtx);

    if (hashContains(storage,path)){
        if (tmp->lockOwner == 0 || tmp->lockOwner == client){
            *size = strlen((char*)tmp->data);

            strcpy(buf, tmp->data);

            Pthread_mutex_lock(&lockStats);
            numRead++;
            dimRead = dimRead + (*size);
            Pthread_mutex_unlock(&lockStats);

            tmp -> FIFOfile -> frequency++;
            clock_gettime(CLOCK_REALTIME, &(tmp -> FIFOfile -> timeUsage));
            Pthread_mutex_unlock(&tmpList->mtx);
            return 0;
        }
        else{
            errno = EPERM;
            Pthread_mutex_unlock(&tmpList->mtx);
            return -1;
        }
    }

    errno = ENOENT;
    Pthread_mutex_unlock(&tmpList->mtx);
    return -1;
}

static fileList* s_readNFile (int N, int* count, size_t client){
    if ((*count) != 0){
        errno = EINVAL;
        return NULL;
    }

    int i;
    int readNum = 0;
    size_t total = 0;
    fileList* out = fileListCreate();
    if (out == NULL){
        return NULL;
    }

    if (N <= 0){
        for (i = 0; i<storage->numBuckets; i++){
            file* cursor = storage->buckets[i]->head;
            Pthread_mutex_lock(&storage->buckets[i]->mtx);

            while(cursor != NULL){
                //anche i files chiusi possno essere letti
                if(cursor->lockOwner == 0 || cursor->lockOwner == client){
                    file* copy = cloneFile(cursor);
                    if(copy == NULL)
                        return NULL;
                    total = total + strlen(copy->data);
                    if(fileListAddHead(out, copy) == -1)
                        return NULL;
                    readNum++;
                }
                cursor = cursor->next;
            }

            Pthread_mutex_unlock(&storage->buckets[i]->mtx);
        }
    }
    else{
        int pkd = 0;
        i = 0;
        while(i<storage->numBuckets && pkd<N){
            file* cursor = storage->buckets[i]->head;
            Pthread_mutex_lock(&storage->buckets[i]->mtx);

            while(cursor != NULL && pkd<N){//anche i files chiusi possno essere letti
                if (cursor->lockOwner == 0 || cursor->lockOwner == client){
                    file* copy = cloneFile(cursor);
                    if(copy == NULL){
                        Pthread_mutex_lock(&storage->buckets[i]->mtx);
                        return NULL;
                    }
                    total = total + strlen(copy->data);
                    if(fileListAddHead(out, copy) == -1){
                        Pthread_mutex_lock(&storage->buckets[i]->mtx);
                        return NULL;
                    }

                    pkd++;
                    readNum++;
                    total = total + strlen(copy->data);
                }
                cursor = cursor->next;
            }

            Pthread_mutex_unlock(&storage->buckets[i]->mtx);
            i++;
        }
    }

    Pthread_mutex_lock(&lockStats);
    dimRead = dimRead + total;
    numRead = numRead + readNum;
    Pthread_mutex_unlock(&lockStats);
    (*count) = readNum;
    return out;
}
static fileList* s_writeFile (char* path, char* data, size_t client){
    if (path == NULL || data == NULL){
        errno = EINVAL;
        return NULL;
    }

    file* tmp = hashGetFile(storage,path);
    if (tmp == NULL)
        return NULL;
    fileList* tmpList = hashGetList(storage,path);
    if (tmp == NULL)
        return NULL;

    Pthread_mutex_lock(&(tmpList->mtx));

    if (!tmp->op){
        errno = EPERM;
        Pthread_mutex_unlock(&(tmpList->mtx));
        return NULL;
    }
    if (tmp->lockOwner == 0 || tmp->lockOwner != client){
        errno = EPERM;
        Pthread_mutex_unlock(&(tmpList->mtx));
        return NULL;
    }

    int opPrev = cListContains(tmp->openClient, client);
    if (opPrev == -1){
        Pthread_mutex_unlock(&(tmpList->mtx));
        return NULL;
    }
    if (opPrev == 0){
        errno = EPERM;
        Pthread_mutex_unlock(&(tmpList->mtx));
        return NULL;
    }

    size_t dimOld = strlen(tmp->data);
    free(tmp->data);
    size_t dimData = strlen(data);
    tmp->data = malloc(sizeof(char)*MAX_DIM_LEN);
    if (strcpy(tmp->data,data) == NULL){
        Pthread_mutex_unlock(&(tmpList->mtx));
        return NULL;
    }
    Pthread_mutex_lock(&lockStats);
    currDim = currDim + dimData - dimOld;
    numWrite++;
    dimWrite = dimWrite + dimData;
    Pthread_mutex_unlock(&lockStats);

    tmp -> FIFOfile -> frequency++;
    clock_gettime(CLOCK_REALTIME, &(tmp -> FIFOfile -> timeUsage));
    Pthread_mutex_unlock(&(tmpList->mtx));

    fileList* out = hashReplace(storage, path, client);

    return out;

}
static fileList* s_appendToFile (char* path, char* data, size_t client){
    if (path == NULL || data == NULL){
        errno = EINVAL;
        return NULL;
    }

    file* tmp = hashGetFile(storage,path);
    if(tmp == NULL) 
        return NULL;
    fileList* tmpList = hashGetList(storage,path);
    if(tmpList == NULL) 
        return NULL;

    Pthread_mutex_lock(&(tmpList->mtx));

    if (tmp == NULL){
        Pthread_mutex_unlock(&(tmpList->mtx));
        return NULL;
    }
    if (!tmp->op){
        errno = EPERM;
        Pthread_mutex_unlock(&(tmpList->mtx));
        return NULL;
    }
    if (tmp->lockOwner == 0 || tmp->lockOwner != client){
        errno = EPERM;
        Pthread_mutex_unlock(&(tmpList->mtx));
        return NULL;
    }

    int opPrev = cListContains(tmp->openClient, client);
    if (opPrev == -1){
        Pthread_mutex_unlock(&(tmpList->mtx));
        return NULL;
    }
    if (opPrev == 0){
        errno = EPERM;
        Pthread_mutex_unlock(&(tmpList->mtx));
        return NULL;
    }

    size_t dimOld = strlen(tmp->data);
    size_t dimData = strlen(data);

    if (strncat(tmp->data,data,dimOld+dimData) == NULL){
        Pthread_mutex_unlock(&(tmpList->mtx));
        return NULL;
    }

    Pthread_mutex_lock(&lockStats);
    currDim = currDim + dimData;
    numWrite++;
    dimWrite = dimWrite + dimData;
    Pthread_mutex_unlock(&lockStats);
    tmp -> FIFOfile -> frequency++;
    clock_gettime(CLOCK_REALTIME, &(tmp -> FIFOfile -> timeUsage));
    Pthread_mutex_unlock(&(tmpList->mtx));

    fileList* out = hashReplace(storage,path,client);
    return out;
}

static int s_lockFile (char* path, size_t client){
    if (path == NULL){
        errno = EINVAL;
        return -1;
    }

    file* tmp = hashGetFile(storage,path);
    if(tmp == NULL) 
        return -1;

    fileList* tmpList = hashGetList(storage,path);
    if(tmpList == NULL) 
        return -1;

    Pthread_mutex_lock(&(tmpList->mtx));
    if(tmp->lockOwner == 0 || tmp->lockOwner == client){
        tmp->lockOwner = client;

        Pthread_mutex_lock(&lockStats);
        numLock++;
        Pthread_mutex_unlock(&lockStats);

        tmp -> FIFOfile -> frequency++;
        clock_gettime(CLOCK_REALTIME, &(tmp -> FIFOfile -> timeUsage));
        Pthread_mutex_unlock(&(tmpList->mtx));
        return 0;
    }
    Pthread_mutex_unlock(&(tmpList->mtx));
    errno = EPERM;
    return -1;
}
static int s_unlockFile (char* path, size_t client){
    if (path == NULL){
        errno = EINVAL;
        return -1;
    }

    file* tmp = hashGetFile(storage,path);
    if (tmp == NULL)
        return -1;

    fileList* tmpList = hashGetList(storage,path);
    if (tmpList == NULL)
        return -1;

    if (tmp->lockOwner == client){
        Pthread_mutex_lock(&(tmpList->mtx));
        tmp->lockOwner = 0;

        Pthread_mutex_lock(&lockStats);
        numUnlock++;
        Pthread_mutex_unlock(&lockStats);

        tmp -> FIFOfile -> frequency++;
        clock_gettime(CLOCK_REALTIME, &(tmp -> FIFOfile -> timeUsage));
        Pthread_mutex_unlock(&(tmpList->mtx));
        return 0;
    }
    else{
        errno = EPERM;
        return -1;
    }

}
static int s_closeFile (char* path, size_t client){
    if (path == NULL){
        errno = EINVAL;
        return -1;
    }

    file* tmp = hashGetFile(storage,path);
    if (tmp == NULL)
        return -1;
    fileList* tmpList = hashGetList(storage,path);
    if (tmpList == NULL)
        return -1;

    Pthread_mutex_lock(&(tmpList->mtx));

    if (tmp->op && (tmp->lockOwner == client || tmp->lockOwner == 0)){
        tmp->op = 0;

        Pthread_mutex_lock(&lockStats);
        numClose++;
        Pthread_mutex_unlock(&lockStats);

        if(cListRemoveNode(tmp->openClient,client) == -1){
            Pthread_mutex_unlock(&(tmpList->mtx));
            return -1;
        }
        tmp -> FIFOfile -> frequency++;
        clock_gettime(CLOCK_REALTIME, &(tmp -> FIFOfile -> timeUsage));
        Pthread_mutex_unlock(&(tmpList->mtx));
        return 0;
    }
    else{
        errno = EPERM;
        Pthread_mutex_unlock(&(tmpList->mtx));
        return -1;
    }

}
static int s_removeFile (char* path, size_t client){
    if (path == NULL){
        errno = EINVAL;
        return -1;
    }

    file* tmp = hashGetFile(storage,path);
    if (tmp == NULL)
        return -1;
    fileList* tmpList = hashGetList(storage,path);
    if (tmpList == NULL)
        return -1;

    Pthread_mutex_lock(&(tmpList->mtx));

    if (tmp->lockOwner == client){
        file* dummy = hashRemove(storage,path);
        if (dummy == NULL){
            Pthread_mutex_unlock(&(tmpList->mtx));
            return -1;
        }
        Pthread_mutex_unlock(&(tmpList->mtx));
        freeFile(dummy);
        return 0;
    }
    else{
        errno = EPERM;
        Pthread_mutex_unlock(&(tmpList->mtx));
        return -1;
    }
}

//funzione chiamata dai thread worker, gestisce la comunicazione col client tramite API
static void job (char* quest, int clientFD, int pipeFD, int* endJob){
    if (quest == NULL || clientFD < 1 || pipeFD < 1){
        errno = EINVAL;
        return;
    }

    char out[DIM_MSG];
    memset(out,0,DIM_MSG);
    char* token = NULL;
    char* save = NULL;

    token = strtok_r(quest,";",&save);
    if (token == NULL){ // tutte le richieste sono state esaudite
        resetLockOwner(storage,clientFD);
        *endJob = 1; //può chiudere la connessione col client
        SYSCALL_EXITF(write(pipeFD, &clientFD, sizeof(clientFD)), "Worker: scrittura pipe")
        SYSCALL_EXITF(write(pipeFD, endJob, sizeof(*endJob)), "Worker: scrittura pipe")
    }
    if (strcmp(token,"openFile") == 0){
        // tokenizzazione degli argomenti
        token = strtok_r(NULL,";",&save);
        char path[UNIX_PATH_MAX];
        strcpy(path,token);
        token = strtok_r(NULL,";",&save);
        int flags = (int) strtol(token,NULL,10);

        // esecuzione della richiesta
        int res;// valore resituito in output dalla openFile del server
        res = s_openFile(path,flags,clientFD);
        if (res == -1){
            sprintf(out,"-1;%d;", errno);
        }
        else{
            sprintf(out,"0");
        }
        if (writen(clientFD, out, DIM_MSG) == -1){
            perror("Worker : scrittura nel socket");
            *endJob = 1;
            SYSCALL_EXITF(write(pipeFD, &clientFD, sizeof(clientFD)), "Worker : scrittura pipe");
            SYSCALL_EXITF(write(pipeFD, endJob, sizeof(*endJob)),"Worker : scrittura pipe");
            return;
        }

        // UPDATE DEL FILE DI LOG
        Pthread_mutex_lock(&logLock);
        fprintf(fileLog,"thread %lu openFile %d %s\n", gettid(), res, path);
        Pthread_mutex_unlock(&logLock);
    }
    else
    if (strcmp(token,"closeFile") == 0){
        // tokenizzazione degli argomenti
        token = strtok_r(NULL,";",&save);
        char path[UNIX_PATH_MAX];
        strcpy(path,token);

        // esecuzione della richiesta
        int res;
        res = s_closeFile(path,clientFD);
        if (res == -1){
            sprintf(out,"-1;%d;",errno);
        }
        else{
            sprintf(out,"0");
        }
        if (writen(clientFD,out,DIM_MSG) == -1){
            *endJob = 1;
            SYSCALL_EXITF(write(pipeFD, &clientFD, sizeof(clientFD)), "Worker : scrittura pipe");
            SYSCALL_EXITF(write(pipeFD, endJob, sizeof(*endJob)),"Worker : scrittura pipe");
            return;
        }

        Pthread_mutex_lock(&logLock);
        fprintf(fileLog,"thread %lu closeFile %d %s\n",gettid(), res, path);
        Pthread_mutex_unlock(&logLock);
    }
    else
    if (strcmp(token,"lockFile") == 0){
        // tokenizzazione degli argomenti
        token = strtok_r(NULL,";",&save);
        char path[UNIX_PATH_MAX];
        strcpy(path,token);

        // esecuzione della richiesta
        int res;
        res = s_lockFile(path, clientFD);
        while (res == -1 && errno == EPERM){
            res = s_lockFile(path, clientFD);
        }
        if(res == -1 && errno != EPERM)
            sprintf(out,"-1;%d;", errno);
        else{
            sprintf(out,"0");
        }
        if (writen(clientFD,out,DIM_MSG) == -1){
            perror("Worker : scrittura nel socket");
            *endJob = 1;
            SYSCALL_EXITF(write(pipeFD, &clientFD, sizeof(clientFD)), "Worker : scrittura pipe");
            SYSCALL_EXITF(write(pipeFD, endJob, sizeof(*endJob)),"Worker : scrittura pipe");
            return;
        }
        Pthread_mutex_lock(&logLock);
        fprintf(fileLog,"thread %lu lockFile %d %s\n",gettid(), res, path);
        Pthread_mutex_unlock(&logLock);
    }
    else
    if (strcmp(token,"unlockFile") == 0){
        // tokenizzazione degli argomenti
        token = strtok_r(NULL,";",&save);
        char path[UNIX_PATH_MAX];
        strcpy(path,token);

        // esecuzione della richiesta
        int res;
        res = s_unlockFile(path,clientFD);
        if (res == -1){
            sprintf(out,"-1;%d;",errno);
        }
        else{
            sprintf(out,"0");
        }
        if (writen(clientFD,out,DIM_MSG) == -1){
            perror("Worker : scrittura nel socket");
            *endJob = 1;
            SYSCALL_EXITF(write(pipeFD, &clientFD, sizeof(clientFD)), "Worker : scrittura pipe");
            SYSCALL_EXITF(write(pipeFD, endJob, sizeof(*endJob)),"Worker : scrittura pipe");
            return;
        }
        Pthread_mutex_lock(&logLock);
        fprintf(fileLog,"thread %lu unlockFile %d %s\n", gettid(), res, path);
        Pthread_mutex_unlock(&logLock);
    }
    else
    if (strcmp(token,"removeFile") == 0){
        // tokenizzazione degli argomenti
        token = strtok_r(NULL,";",&save);
        char path[UNIX_PATH_MAX];
        strcpy(path,token);

        // esecuzione della richiesta
        int res = s_removeFile(path,clientFD);

        if (res == -1){
            sprintf(out,"-1;%d;",errno);
        }
        else{
            sprintf(out,"0");
        }

        if (writen(clientFD,out,DIM_MSG) == -1){
            perror("Worker : scrittura nel socket");
            *endJob = 1;
            SYSCALL_EXITF(write(pipeFD, &clientFD, sizeof(clientFD)), "Worker : scrittura pipe");
            SYSCALL_EXITF(write(pipeFD, endJob, sizeof(*endJob)),"Worker : scrittura pipe");
            return;
        }

        Pthread_mutex_lock(&logLock);
        fprintf(fileLog,"thread %lu removeFile %d %s\n", gettid(), res, path);
        Pthread_mutex_unlock(&logLock);
    }
    else
    if (strcmp(token,"writeFile") == 0){
        // tokenizzazione degli argomenti
        token = strtok_r(NULL,";",&save);
        char path[UNIX_PATH_MAX];
        strcpy(path,token);
        token = strtok_r(NULL,";",&save);
        char data[DIM_MSG];
        strcpy(data,token);
        size_t dimData = strnlen(data,DIM_MSG);

        // esecuzione della richiesta
        errno = 0;
        fileList* tmp = s_writeFile(path, data, clientFD);
        int logResult;
        if (errno != 0){
            logResult = -1;
            sprintf(out,"-1;%d;",errno);
        }
        else{
            logResult = 0;
            printf("dimensione %lu\n", tmp->size);
            printf("%lu\n", tmp -> size);
            sprintf(out,"%lu;", (tmp->size));
        }

        if (writen(clientFD,out,DIM_MSG) == -1){
            perror("Worker : scrittura nel socket");
            *endJob = 1;
            SYSCALL_EXITF(write(pipeFD, &clientFD, sizeof(clientFD)), "Worker : scrittura pipe");
            SYSCALL_EXITF(write(pipeFD, endJob, sizeof(*endJob)),"Worker : scrittura pipe");
            fileListFree(tmp);
            return;
        }
        file* cursor = NULL;
        int rpl = 0;
        if(tmp != NULL){
            cursor = tmp->head;
            if (tmp->size > 0)
                rpl = 1;
            else
                rpl = 0;
        }
        size_t replaceDim = 0;
        size_t numReplace = 0;


        while (cursor != NULL){
            sprintf(out,"%s;%s",cursor->path,cursor->data);
            replaceDim = replaceDim + strnlen(cursor->data,DIM_MSG);
            numReplace++;

            if (writen(clientFD,out,DIM_MSG) == -1){
                perror("Worker : scrittura nel socket");
                *endJob = 1;
                SYSCALL_EXITF(write(pipeFD, &clientFD, sizeof(clientFD)), "Worker : scrittura pipe");
                SYSCALL_EXITF(write(pipeFD, endJob, sizeof(*endJob)),"Worker : scrittura pipe");
                fileListFree(tmp);
                return;
            }

            cursor = cursor->next;
        }
        Pthread_mutex_lock(&logLock);
        fprintf(fileLog,"thread %lu writeFile %d %s %lu %d %lu %lu\n",gettid(), logResult, path, dimData, rpl, numReplace, replaceDim);
        Pthread_mutex_unlock(&logLock);
        fileListFree(tmp);
    }
    else
    if (strcmp(token,"appendToFile") == 0){
        // tokenizzazione degli argomenti
        token = strtok_r(NULL,";",&save);
        char path[UNIX_PATH_MAX];
        strcpy(path,token);
        token = strtok_r(NULL,";",&save);
        char data[DIM_MSG];
        strcpy(data,token);
        size_t dimData = strnlen(data,DIM_MSG);

        // esecuzione della richiesta
        errno = 0;
        fileList* tmp = s_appendToFile(path,data,clientFD);
        int logResult;

        if (tmp == NULL && errno != 0){
            logResult = -1;
            sprintf(out,"-1;%d;",errno);
        }
        else{
            logResult = 0;
            sprintf(out,"%lu;",(tmp->size));
        }

        if (writen(clientFD,out,DIM_MSG) == -1){
            perror("Worker : scrittura nel socket");
            *endJob = 1;
            SYSCALL_EXITF(write(pipeFD, &clientFD, sizeof(clientFD)), "Worker : scrittura pipe");
            SYSCALL_EXITF(write(pipeFD, endJob, sizeof(*endJob)),"Worker : scrittura pipe");
            fileListFree(tmp);
            return;
        }

        file* cursor = tmp->head;
        size_t replaceDim = 0;
        size_t numReplace = 0;
        int rpl;
        if (tmp->size > 0) rpl = 1;
        else rpl = 0;

        while (cursor != NULL){
            sprintf(out,"%s;%s",cursor->path,cursor->data);
            replaceDim = replaceDim + strnlen(cursor->data,DIM_MSG);
            numReplace++;

            if (writen(clientFD,out,DIM_MSG) == -1)
            {
                perror("Worker : scrittura nel socket");
                *endJob = 1;
                SYSCALL_EXITF(write(pipeFD, &clientFD, sizeof(clientFD)), "Worker : scrittura pipe");
                SYSCALL_EXITF(write(pipeFD, endJob, sizeof(*endJob)),"Worker : scrittura pipe");
                fileListFree(tmp);
                return;
            }
            cursor = cursor->next;
        }

        Pthread_mutex_lock(&logLock);
        fprintf(fileLog,"thread %lu appendToFile %d %s %lu %d %lu %lu\n",gettid(), logResult, path, dimData, rpl, numReplace, replaceDim);
        Pthread_mutex_unlock(&logLock);
        fileListFree(tmp);
    }
    else
    if (strcmp(token,"readFile") == 0){

        // tokenizzazione degli argomenti
        token = strtok_r(NULL,";",&save);
        char path[UNIX_PATH_MAX];
        strcpy(path,token);

        char* buf = malloc(sizeof(char)*MAX_DIM_LEN);
        if (buf == NULL){
            errno = ENOMEM;
            free(buf);
            return;
        }
        size_t size;
        // esecuzione della richiesta
        int res = s_readFile(path,buf,&size,clientFD);
        int logResult;

        if (res == -1){
            logResult = 0;
            sprintf(out,"-1;%d;",errno);
        }
        else{
            logResult = 1;
            sprintf(out,"%s;%ld",buf,size);
        }

        if (writen(clientFD,out,DIM_MSG) == -1){
            perror("Worker : scrittura nel socket");
            *endJob = 1;
            SYSCALL_EXITF(write(pipeFD, &clientFD, sizeof(clientFD)), "Worker : scrittura pipe");
            SYSCALL_EXITF(write(pipeFD, endJob, sizeof(*endJob)),"Worker : scrittura pipe");
            free(buf);
            return;
        }
        Pthread_mutex_lock(&logLock);
        fprintf(fileLog,"thread %lu readFile %d %s %ld\n",gettid(),logResult,path,size);
        Pthread_mutex_unlock(&logLock);
        free(buf);
    }
    else
    if (strcmp(token,"readNFiles")==0){
        // tokenizzazione degli argomenti
        token = strtok_r(NULL,";",&save);
        int N = (int)strtol(token, NULL, 10);

        int count = 0;

        errno = 0;
        int logResult;
        fileList* tmp = s_readNFile(N,&count,clientFD);

        if (errno != 0){
            logResult = -1;
            sprintf(out,"-1;%d;",errno);
        }
        else{
            logResult = 0;
            sprintf(out,"%d;",count);
        }

        if (writen(clientFD,out,DIM_MSG) == -1){
            perror("Worker : scrittura nel socket");
            *endJob = 1;
            SYSCALL_EXITF(write(pipeFD, &clientFD, sizeof(clientFD)), "Worker : scrittura pipe");
            SYSCALL_EXITF(write(pipeFD, endJob, sizeof(*endJob)),"Worker : scrittura pipe");
            fileListFree(tmp);
            return;
        }

        file* cursor = tmp->head;

        while (cursor != NULL){
            char readN_out[UNIX_PATH_MAX + MAX_DIM_LEN + 1];
            memset(readN_out,0,UNIX_PATH_MAX + MAX_DIM_LEN + 1);
            sprintf(readN_out,"%s;%s",cursor->path,cursor->data);
            if (writen(clientFD,readN_out,UNIX_PATH_MAX + MAX_DIM_LEN + 1) == -1)
            {
                perror("Worker : scrittura nel socket");
                *endJob = 1;
                SYSCALL_EXITF(write(pipeFD, &clientFD, sizeof(clientFD)), "Worker : scrittura pipe");
                SYSCALL_EXITF(write(pipeFD, endJob, sizeof(*endJob)),"Worker : scrittura pipe");
                fileListFree(tmp);
                return;
            }

            cursor = cursor->next;
        }

        Pthread_mutex_lock(&logLock);
        fprintf(fileLog,"thread %lu readNFile %d %d ",gettid(),logResult,count);

        file* curs = tmp->head;
        size_t tot_size = 0;

        while(curs != NULL){
            tot_size = tot_size + strlen(curs->data);
            curs = curs->next;
        }

        fprintf(fileLog,"%lu\n",tot_size);
        Pthread_mutex_unlock(&logLock);
        fileListFree(tmp);
    }
    else{
        // funzione non implementata
        sprintf(out,"-1;%d",ENOSYS);
        if (writen(clientFD,out,DIM_MSG) == -1){
            perror("Worker : scrittura nel socket");
            return;
        }
    }

     *endJob = 0; //comunico al manager che ho eseguito la richiesta del client
    SYSCALL_EXITF(write(pipeFD, &clientFD, sizeof(clientFD)), "Worker : scrittura pipe");
    SYSCALL_EXITF(write(pipeFD, endJob, sizeof(*endJob)),"Worker : scrittura pipe");

}
static void* worker (void* arg){
    int pipeFD = *((int*)arg);
    int clientFD;

    while (TRUE){
        int endJob; //valore indicante la terminazione del client o della richiesta
        //un client viene espulso dalla coda secondo la politica FIFO
        Pthread_mutex_lock(&lockClientList);
        clientFD = ClistPopWorker(clientList);
        Pthread_mutex_unlock(&lockClientList);

        if (clientFD == -1){
            return (void*) 0; //indica la terminazione del server
        }

        char quest [DIM_MSG];
        memset(quest,0,DIM_MSG);
        int len = readn(clientFD, quest, DIM_MSG);

        if (len == -1 || len == 0){// il client è disconnesso
            endJob = 1;
            SYSCALL_EXITF(write(pipeFD, &clientFD, sizeof(clientFD)), "Worker: pipe error")
            SYSCALL_EXITF(write(pipeFD, &endJob, sizeof(endJob)), "Worker: pipe error")
            }
        else{// richiesta del client ricevuta correttamente
            job(quest, clientFD, pipeFD, &endJob);
        }
    }
    return (void*) 0;
}

int main(int argc, char* argv[]){
    clientList = cListCreate();
    if(clientList == NULL){
        exit(EXIT_FAILURE);
    }

    int softEnd = FALSE;
    char socket_name[100];
    //valori di default
    numFileMax = 20;
    maxDIm = 1024 * 1024;
    numThread = 4;
    numLists = numFileMax/2; //dimensione tabella di default
    strcpy(socket_name, SOCKET_NAME);

    char *path_config = NULL;
    if (argc == 3) {
        if (strcmp(argv[1], "-cnfg") == 0) {
            path_config = argv[2];
        }
    }
    // parsing file config.txt
    if (path_config != NULL){
        char string[200];
        FILE *fp;
        fp = fopen(path_config, "r");
        if (fp == NULL){
            perror("Errore nell'apertura del file di configurazione");
            exit(EXIT_FAILURE);
        }

        char campo[100]; // campo da configurare = arg
        char valore[100]; // valore del campo = val
        while (fgets(string, 200, fp) != NULL){
            if (string[0] != '\n'){
                int nf;
                nf = sscanf(string, "%[^=]=%s", campo, valore);
                if (nf != 2){
                    printf("Errore di Configurazione : formato non corretto\n");
                    printf("Il Server è stato avviato con parametri DEFAULT\n");
                    break;
                }
                if (strcmp(campo, "numThread") == 0){
                    long n;
                    int out = isNumber(valore, &n);
                    if (out == 2 || out == 0 || n <= 0){
                        printf("Errore di Configurazione : numThread deve essere >= 0\n");
                        printf("Il Server è stato avviato con parametri DEFAULT\n");
                        break;
                    }
                    else numThread = (size_t) n;
                }
                else
                    if (strcmp(campo, "numFileMax") == 0){
                        long n;
                        int out = isNumber(valore, &n);
                        if (out == 2 || out == 0 || n <= 0){
                            printf("Errore di Configurazione : numFileMax deve essere >= 0\n");
                            printf("Il Server è stato avviato con parametri DEFAULT\n");
                            break;
                        }
                        else numFileMax = (size_t) n;
                    }
                    else
                        if (strcmp(campo, "maxDim") == 0){
                            long n;
                            int out = isNumber(valore, &n);
                            if (out == 2 || out == 0 || n <= 0){
                                printf("Errore di Configurazione : maxDIm deve essere >= 0\n");
                                printf("Il Server è stato avviato con parametri DEFAULT\n");
                                break;
                            }
                            else maxDIm = (size_t) n;
                        }
                        else
                            if(strcmp(campo, "replace") == 0) {
                                if (strcmp(valore, "LFU") == 0) {
                                    hashReplace = hashReplaceLFU;
                                    printf("politica rimpiazzamento LFU\n");
                                }
                                else {
                                    printf("Valore di default: politica FIFO\n");
                                }
                            }
                            else
                                if(strcmp(campo, "hashDim") == 0){
                                    long n;
                                    int out = isNumber(valore, &n);
                                    if (out == 2 || out == 0 || n <= 0){
                                        printf("Errore di Configurazione : maxDIm deve essere >= 0\n");
                                        printf("Il Server è stato avviato con parametri DEFAULT\n");
                                        break;
                                    }
                                    else  numLists = (size_t) n;
                                }
            }
        }
        fclose(fp);
        }
    else
        printf("Server avviato con parametri di DEFAULT\n");

    printf("Server INFO: socket_name:%s / num_thread:%lu / max_files:%lu / maxDIm:%lu\n", socket_name, numThread, numFileMax, maxDIm);

    sigset_t set;
    SYSCALL_EXITF(sigfillset(&set), "sigfillsett errror")
    SYSCALL_EXITF(pthread_sigmask(SIG_SETMASK, &set, NULL), "pthread sigmask error")

    struct sigaction s;
    memset(&s, 0, sizeof(s));
    s.sa_handler = sigHandler;
    SYSCALL_EXITF(sigaction(SIGINT, &s, NULL), "sigaction SIGINT error")
    SYSCALL_EXITF(sigaction(SIGQUIT, &s, NULL), "sigaction SIGQUIT error")
    SYSCALL_EXITF(sigaction(SIGHUP, &s, NULL), "sigaction SIGHUP error")

    s.sa_handler = SIG_IGN;
    SYSCALL_EXITF(sigaction(SIGPIPE, &s, NULL), "sigaction SIGPIPE error")

    SYSCALL_EXITF(sigemptyset(&set), "sigemptyset error")
    SYSCALL_EXITF(pthread_sigmask(SIG_SETMASK, &set, NULL), "pthread-signmask error")

    storage = hashCreate(numLists);
    if (storage == NULL){
        exit(EXIT_FAILURE);
    }
    queue = FIFOcreate();
    if (queue == NULL){
        exit(EXIT_FAILURE);
    }

    fileLog = fopen(LOG_NAME,"w");
    if(fileLog == NULL){
        exit(EXIT_FAILURE);
    }

    fprintf(fileLog,"INIZIO\n");

    int pip[2];
    SYSCALL_EXITF(pipe(pip), "creazione pipe error")

    // thread pool
    pthread_t* threadPool = malloc(sizeof(pthread_t)*numThread);
    if (threadPool == NULL){
        errno = ENOMEM;
        perror("inizializzazione thread_pool");
        exit(EXIT_FAILURE);
    }
    int i;
    for (i = 0; i < numThread; i++){
        SYSCALL_EXITF(pthread_create(&threadPool[i],NULL, worker,(void*)(&pip[1])), "creazione thread error")
    }

    int socketFD;
    int clientFD;
    int numFD = 0;
    int fd;
    fd_set sset;
    fd_set rdSet;
    struct sockaddr_un sa;
    strncpy(sa.sun_path, socket_name, UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;

    SYSCALL_EXITF(socketFD = socket(AF_UNIX,SOCK_STREAM,0), "creazione socket error")
    SYSCALL_EXITF(bind(socketFD,(struct sockaddr*)&sa,sizeof(sa)), "bind socket error")
    SYSCALL_EXITF(listen(socketFD, SOMAXCONN), "listen socket error")

    if (socketFD > numFD)
        numFD = socketFD;
    FD_ZERO(&sset);
    FD_SET(socketFD,&sset);

    if (pip[0] > numFD)
        numFD = pip[0];
    FD_SET(pip[0],&sset);

    printf("Attesa dei Clients\n");

    while (TRUE){
        rdSet = sset;//ripristino il set di partenza
        if (select(numFD+1,&rdSet,NULL,NULL,NULL) == -1){//gestione errore
            if (end == HARD_END) break;//chiusura bruta
            else if (end == SOFT_END) { //chiusura soft
                if (numConnectionsCurr == 0)
                    break;
                else {
                    FD_CLR(socketFD,&sset);//rimozione del fd del socket dal set
                    if (socketFD == numFD)
                        numFD = updateMax(sset,numFD);//aggiorno l'indice massimo
                    close(socketFD);//chiusura del socket
                    rdSet = sset;
                    int output = select(numFD+1,&rdSet,NULL,NULL,NULL);
                    if (output == -1){
                        perror("select");
                        break;
                    }
                }
            }else {//fallimento select
                perror("select");
                break;
            }
        }
        //controlliamo tutti i file descriptors
        for (fd = 0; fd <= numFD; fd++) {
            if (FD_ISSET(fd,&rdSet)){
                if (fd == socketFD){ //il socket è pronto per accettare una nuova richiesta di connessine
                    if ((clientFD = accept(socketFD,NULL,0)) == -1){
                        if (end == HARD_END)
                            break;
                        else if (end == SOFT_END) {//terminazione soft
                            if (numConnectionsCurr == 0)
                                break;
                        }else {
                            perror("Errore dell' accept");
                        }
                    }
                    FD_SET(clientFD,&sset);//il file del client è pronto in lettura
                    if (clientFD > numFD)
                        numFD = clientFD;
                    numConnectionsCurr++;
                    if(numConnectionsCurr > maxNumConnections)
                        maxNumConnections = numConnectionsCurr;
                    printf ("SERVER : Client Connesso\n");
                }
                else
                    if (fd == pip[0]){
                        int clientFD1;
                        int len;
                        int flag;
                        if ((len = (int)read(pip[0],&clientFD1,sizeof(clientFD1))) > 0){
                            SYSCALL_EXITF((int)read(pip[0],&flag,sizeof(flag)), "dialogo Master-Worker error")
                            if (flag == 1){
                                printf("Chiusura della connessione col client\n");
                                FD_CLR(clientFD1,&sset);
                                if (clientFD1 == numFD)
                                    numFD = updateMax(sset,numFD);
                                numConnectionsCurr--;
                                close(clientFD1);
                                if (end == SOFT_END && numConnectionsCurr == 0){
                                    printf("Terminazione Soft\n");
                                    softEnd = TRUE;
                                    break;
                                }
                            }
                            else{
                                FD_SET(clientFD1,&sset);
                                if (clientFD1 > numFD)
                                    numFD = clientFD1;
                            }
                        }
                        else
                            if (len == -1){
                                perror("errore nel dialogo Master/Worker");
                                exit(EXIT_FAILURE);
                            }
                    }
                    else{
                        Pthread_mutex_lock(&lockClientList);
                        cListAddHead(clientList,fd);
                        pthread_cond_signal(&notEmpty);
                        Pthread_mutex_unlock(&lockClientList);
                        FD_CLR(fd,&sset);
                    }
            }
        }
        if (softEnd == TRUE)
            break;
    }
        printf("\nChiusura del Server...\n");

        Pthread_mutex_lock(&lockClientList);
        for (i = 0; i < numThread; i++){
            cListAddHead(clientList,-1); //comunico ai worker di terminare
            pthread_cond_signal(&notEmpty);
        }
        Pthread_mutex_unlock(&lockClientList);

        for (i = 0; i < numThread; i++){
            if (pthread_join(threadPool[i],NULL) != 0){
                perror("Errore in thread join");
                exit(EXIT_FAILURE);
            }
        }
        free(threadPool);
        remove(socket_name);

    size_t defaultNumRead = 1;
    size_t defaultNumWrite = 1;
    if (numRead != 0)
        defaultNumRead = numRead;
    if (numWrite != 0)
        defaultNumWrite = numWrite;
    dimReadMedia = dimRead/defaultNumRead;
    dimWriteMedia =  dimWrite/defaultNumWrite;
    maxDimReachedMB =  maxDimReached/(1024 * 1024);
    maxDimReachedKB = maxDimReached / 1024;


    fprintf(fileLog,"RIASSUNTO STATISTICHE:\n");
    fprintf(fileLog,"-Numero di read: %lu;\n-Size media delle letture in bytes: %lu;\n-Numero di write: %lu;\n-Size media delle scritture in bytes: %lu;\n-Numero di lock: %lu;\n-Numero di openlock: %lu;\n-Numero di unlock: %lu;\n-Numero di close: %lu;\n-Dimensione massima dello storage in MB: %lu;\n-Dimensione massima dello storage in numero di files: %lu;\n-Numero di replace per selezionare un file vittima: %lu;\n-Massimo numero di connessioni contemporanee: %lu;\n",numRead,dimReadMedia,numWrite,dimWriteMedia,numLock,numOpenLock,numUnlock,numClose,maxDimReachedMB,numFileMaxReached,replacement,maxNumConnections);
    // il numero di richieste soddisfatte da ogni thread è lasciato al parsing in statistiche.sh


    printf("SERVER STATISTICHE:\n");
    printf("Numero Massimo di files raggiunto: %lu\n", numFileMaxReached);
    printf("Dimensione Massima raggiunta dallo storage in Byte: %lu\n", maxDimReached);
    printf("Dimensione Massima raggiunta dallo storage in KByte: %lu\n", maxDimReachedKB);
    printf("Dimensione Massima raggiunta dallo storage in MByte: %lu\n", maxDimReachedMB);
    printf("Numero di volte in cui è stato avviato l'algoritmo di rimpiazzamento: %lu\n", numReplaceAlgo);
    printf("Stato dello Storage al momento della chiusura\n");
    hashPrint(storage);

    hashFree(storage);
    FIFOFree(queue);
    cListFree(clientList);
    fclose(fileLog);

    return 0;
}
