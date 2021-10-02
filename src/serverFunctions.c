#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/un.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <libgen.h>
#include "../includes/api.h"

#define DIM_MSG 2048
#define MAX_DIM_LEN 1024 //grandezza massima contenuto file
#define UNIX_PATH_MAX 108 /* man 7 unix */

int connesso = 0;//indica se il client è connesso
size_t last_w_size = 0;
size_t last_rN_size = 0;
int fdSocket; //fd del socket
char socketName[UNIX_PATH_MAX]; //nome del socket
char request[DIM_MSG]; //stringa usata per la comunicazione server-client

//funzioni utili
size_t get_last_w_size (){
    return last_w_size;
}
size_t get_last_rN_size (){
    return last_rN_size;
}
//analogo di mkdir -p
int mkdir_p(const char *path) {
    const size_t len = strlen(path);
    char _path[PATH_MAX];
    char *p;
    errno = 0;
    if (len > sizeof(_path)-1) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(_path, path);
    for (p = _path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(_path, S_IRWXU) != 0) {
                if (errno != EEXIST)
                    return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(_path, S_IRWXU) != 0) {
        if (errno != EEXIST)
            return -1;
    }
    return 0;
}


int msSleep(long time){
    if(time < 0){
        errno = EINVAL;
    }
    int res;
    struct timespec t;
    t.tv_sec = time/1000;
    t.tv_nsec = (time % 1000) * 1000000;

    do {
        res = nanosleep(&t, &t);
    }while(res && errno == EINTR);

    return res;
}
int compareTime(struct timespec a, struct timespec b){
    clock_gettime(CLOCK_REALTIME, &a);

    if(a.tv_sec == b.tv_sec){
        if(a.tv_nsec > b.tv_nsec)
            return 1;
        else if(a.tv_nsec == b.tv_nsec)
            return 0;
        else
            return -1;
    } else if(a.tv_sec > b.tv_sec)
        return 1;
    else
        return -1;
}

int readn(long fd, void *buf, size_t size) {
    int readn = 0;
    int r = 0;

    while ( readn < size ){

        if ( (r = read(fd, buf, size)) == -1 ){
            if( errno == EINTR )
                // se la read è stata interrotta da un segnale riprende
                continue;
            else{
                perror("Standard ERROR: Readn");
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
        if ( (w = write(fd, buf, nbyte) ) == -1 ){
            /* se la write è stata interrotta da un segnale riprende */
            if ( errno == EINTR )
                continue;
            else if ( errno == EPIPE )
                break;
            else{
                perror("Standard ERROR: Writen");
                return -1;
            }
        }
        if( w == 0 )
            return writen;

        writen += w;
    }
    return writen;
}

int openConnection(const char* sockname, int msec, const struct timespec abstime) {

    // sockname -> nome del socket a cui il client vuole connettersi
    // msec -> ogni quanto si riprova la connessione
    // abstime -> tempo massimo per la connessione

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    strncpy(sa.sun_path, sockname, UNIX_PATH_MAX);
    sa.sun_family = AF_UNIX;

    if ((fdSocket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1){// otteniamo il file descriptor del socket
        errno = EINVAL;
        perror("Standard ERROR: socket");
        return -1;
    }

    struct timespec time;
    while (connect(fdSocket,(struct sockaddr*)&sa,sizeof(sa)) == -1 && compareTime(time, abstime) == -1){// quando la connessione fallisce e siamo ancora entro il tempo massimo facciamo un' attesa di msec secondi
        msSleep(msec);
    }

    if (compareTime(time, abstime) > 0){// se la connessione non è riuscita entro il tempo massimo abbiamo un Standard ERROR di timeout
        errno = ETIMEDOUT;
        perror("Standard ERROR: timeout connessione");
        return -1;
    }

    connesso = 1;// la flag di connessione viene settata ad 1
    strcpy(socketName, sockname);// il nome del socket viene memorizzato in una variabile globale
    return 0;
}
int closeConnection(const char* sockname){
    if (connesso == 0) {
        errno = EPERM;
        return -1;
    }

    if (strcmp(socketName,sockname) == 0){
        char buffer [DIM_MSG];
        memset(buffer,0,DIM_MSG);
        snprintf(buffer, DIM_MSG,"closeConnection");// il comando viene scritto sulla stringa buffer

        if(writen(fdSocket, buffer, DIM_MSG) == -1){// il comando viene scritto nel canale con il server
            errno = EREMOTEIO;
            return -1;
        }
        if (close(fdSocket) == -1) {// la connessione viene effettivamente chiusa
            errno = EREMOTEIO;
            return -1;
        }
        return 0;
    }
    else{// il socket parametro non è valido
        errno = EINVAL;
        return -1;
    }
}
int openFile(const char* path, int flags) {

    if (connesso == 0){
        errno = ENOTCONN;
        return -1;
    }

    char* save = NULL;
    char buffer [DIM_MSG];
    memset(buffer,0,DIM_MSG);
    snprintf(buffer, DIM_MSG,"openFile;%s;%d;",path, flags);// il comando viene scritto sulla stringa buffer

    if(writen(fdSocket, buffer, DIM_MSG) == -1){// il comando viene scritto nel canale con il server
        errno = EREMOTEIO;
        return -1;
    }
    if(readn(fdSocket, request, DIM_MSG) == -1){// lettura risposta server
        errno = EREMOTEIO;
        return -1;
    }

    char* token;
    token = strtok_r(request,";", &save);

    if (strcmp(token, "-1") == 0){ //fallimento
        token = strtok_r(NULL,";", &save);
        errno = (int)strtol(token, NULL, 10);
        return -1;
    }
    else{ //successo operazione
        return 0;
    }
}
int closeFile(const char* path) {
    if (connesso == 0) {
        errno = ENOTCONN;
        return -1;
    }

    char buffer[DIM_MSG];
    memset(buffer,0,DIM_MSG);
    sprintf(buffer, "closeFile;%s;", path);
    fflush(stdout);
    if(writen(fdSocket, buffer, DIM_MSG) == -1){
        errno = EREMOTEIO;
        return -1;
    }
    if(readn(fdSocket, request, DIM_MSG) == -1){
        errno = EREMOTEIO;
        return -1;
    }

    char* token;
    char* save = NULL;
    token = strtok_r(request, ";", &save);

    if (strcmp(token, "-1") == 0) {//fallimento operazione
        token = strtok_r(NULL,";", &save);
        errno = (int)strtol(token, NULL, 10);
        return -1;
    }
    else {// successo operazione
        return 0;
    }
}
int removeFile(const char* path) {

    if (connesso == 0) {
        errno = ENOTCONN;
        return -1;
    }

    char buffer [DIM_MSG];
    memset(buffer,0,DIM_MSG);
    sprintf(buffer, "removeFile;%s;", path);

    if(writen(fdSocket, buffer, DIM_MSG) == -1){
        errno = EREMOTEIO;
        return -1;
    }
    if(readn(fdSocket, request, DIM_MSG) == -1){
        errno = EREMOTEIO;
        return -1;
    }

    char* token;
    char* save = NULL;
    token = strtok_r(request, ";", &save);

    if (strcmp(token, "-1") == 0){ // operazione fallita
        token = strtok_r(NULL,";",&save);
        errno = (int)strtol(token, NULL, 10);
        return -1;
    }
    else{// operazione successo
        return 0;
    }
}
int lockFile(const char* path){
    if (connesso == 0){
        errno = ENOTCONN;
        return -1;
    }

    char buffer [DIM_MSG];
    memset(buffer,0,DIM_MSG);
    sprintf(buffer, "lockFile;%s;", path);

    if(writen(fdSocket, buffer, DIM_MSG) == -1){
        errno = EREMOTEIO;
        return -1;
    }
    if(readn(fdSocket, request, DIM_MSG) == -1){
        errno = EREMOTEIO;
        return -1;
    }

    char* token;
    char* save = NULL;
    token = strtok_r(request, ";", &save);

    if (strcmp(token, "-1") == 0){ //operazione fallita
        token = strtok_r(NULL,";",&save);
        errno = (int)strtol(token, NULL, 10);
        return -1;
    }
    else{ // operazione successo
        return 0;
    }
}
int unlockFile(const char* path){
    if (connesso == 0) {
        errno = ENOTCONN;
        return -1;
    }

    char buffer [DIM_MSG];
    memset(buffer,0,DIM_MSG);
    sprintf(buffer, "unlockFile;%s", path);

    if(writen(fdSocket, buffer, DIM_MSG) == -1){
        errno = EREMOTEIO;
        return -1;
    }
    if(readn(fdSocket, request, DIM_MSG) == -1){
        errno = EREMOTEIO;
        return -1;
    }

    char* token;
    char* save = NULL;
    token = strtok_r(request, ";", &save);

    if (strcmp(token, "-1") == 0){ //operazione fallita
        token = strtok_r(NULL,";", &save);
        errno = (int)strtol(token, NULL, 10);
        return -1;
    }
    else{ // operazione successo
        return 0;
    }
}
int writeFile(const char* path, const char* dir){
    if (connesso == 0){
        errno = ENOTCONN;
        return -1;
    }

    if (dir != NULL){// se la directory non esiste viene creata
        if (mkdir_p(dir) == -1){
            if (errno != EEXIST) return -1;
        }
    }

    FILE *fp;
    if ((fp = fopen(path, "r")) == NULL) {
        errno = ENOENT;
        return -1;
    }

    char cnt[MAX_DIM_LEN];
    cnt[0] = '\0';
    char raw[MAX_DIM_LEN];
    while (fgets(raw, MAX_DIM_LEN, fp)) {
        // leggiamo riga per riga il contenuto del file e lo mettiamo in cnt(contenuto)
        strcat(cnt, raw);
    }
    fclose(fp);
    last_w_size = strnlen(cnt, MAX_DIM_LEN);
    // preparo il comando da mandare al server
    char buffer[DIM_MSG];
    memset(buffer, 0, DIM_MSG);
    sprintf(buffer, "writeFile;%s;%s;", path, cnt);

    if (writen(fdSocket, buffer, DIM_MSG) == -1) {
        errno = EREMOTEIO;
        return -1;
    }
    if (readn(fdSocket, request, DIM_MSG) == -1) {
        errno = EREMOTEIO;
        return -1;
    }

    char *token;
    char *save = NULL;
    token = strtok_r(request, ";", &save);

    if (strcmp(token, "-1") == 0) { //fallimento
        token = strtok_r(NULL, ";", &save);
        errno = (int) strtol(token, NULL, 10);
        return -1;
    }
    else{
        int remain = (int) strtol(token, NULL, 10);
        int i = 0;

        while (i < remain){
            if (readn(fdSocket, request, DIM_MSG) == -1){
                errno = EREMOTEIO;
                return -1;
            }
            char* save1 = NULL;
            char* absPath = strtok_r(request,";",&save1); // path assoluto del file
            char* fileCnt = strtok_r(NULL,";",&save1); // contenuto file
            // fileName contiene il nome del file e basta
            char* fileName = basename(absPath);

            if (dir!=NULL){   //salvataggio del file nella cartella specificata
                char pathReal[UNIX_PATH_MAX];
                memset(pathReal,0,UNIX_PATH_MAX);
                sprintf(pathReal,"%s/%s", dir, fileName);

                //se il file non esiste esso viene creato
                FILE *ofp;
                ofp = fopen(pathReal, "w");

                if (ofp == NULL){
                    printf("Standard ERROR nell'apertura del file\n");
                    return -1;
                } else {
                    fprintf(ofp, "%s", fileCnt);
                    fclose(ofp);
                }
            }
            i++;
        }
        return 0;
    }
}
int appendToFile(const char* path, void* buf, size_t size, const char* dir){
    if (connesso == 0){
        errno = ENOTCONN;
        return -1;
    }

    if (dir != NULL){// se la directory non esiste viene creata
        if (mkdir_p(dir) == -1){
            if (errno != EEXIST) return -1;
        }
    }
    char cnt[MAX_DIM_LEN];
    cnt[0] = '\0';
    strncat(cnt,(char*)buf,size);

    // comando da mandare al server
    char buffer[DIM_MSG];
    memset(buffer, 0, DIM_MSG);
    sprintf(buffer, "appendFile;%s;%s;", path, cnt);

    if (writen(fdSocket, buffer, DIM_MSG) == -1) {
        errno = EREMOTEIO;
        return -1;
    }
    if (readn(fdSocket, request, DIM_MSG) == -1) {
        errno = EREMOTEIO;
        return -1;
    }
    char *token;
    char *save = NULL;
    token = strtok_r(request, ";", &save);

    if (strcmp(token, "-1") == 0){ //fallimento
        token = strtok_r(NULL, ";", &save);
        errno = (int) strtol(token, NULL, 10);
        return -1;
    }
    else{
        int remain = (int) strtol(token, NULL, 10);
        int i = 0;
        while (i < remain){
            if (readn(fdSocket, request, DIM_MSG) == -1){
                errno = EREMOTEIO;
                return -1;
            }

            char* save1 = NULL;
            char* absPath = strtok_r(request,";",&save1); // path assoluto del file
            char* fileCnt = strtok_r(NULL,";",&save1); // contenuto del file
            // fileName contiene il nome del file
            char* fileName = basename(absPath);

            if (dir!=NULL){   //salvataggio del file nella cartella specificata
                char pathReal[UNIX_PATH_MAX];
                memset(pathReal,0,UNIX_PATH_MAX);
                sprintf(pathReal,"%s/%s",dir,fileName);

                //se il file non esiste esso viene creato
                FILE *ofp;
                ofp = fopen(pathReal, "w");

                if (ofp == NULL){
                    printf("Standard ERROR nell'apertura del file\n");
                    return -1;
                } else {
                    fprintf(ofp, "%s", fileCnt);
                    fclose(ofp);
                }
            }
            i++;
        }
        return 0;
    }
}
int readFile(const char* path, void** buf, size_t* size){
    if (connesso == 0){
        errno = ENOTCONN;
        return -1;
    }

    char buffer [DIM_MSG];
    memset(buffer,0,DIM_MSG);
    sprintf(buffer, "readFile;%s;",path);

    if(writen(fdSocket, buffer, DIM_MSG) == -1){
        errno = EREMOTEIO;
        return -1;
    }
    if(readn(fdSocket, request, DIM_MSG) == -1){
        errno = EREMOTEIO;
        return -1;
    }

    char* token;
    char* save = NULL;
    token = strtok_r(request, ";", &save);

    if (strcmp(token, "-1") == 0){
        token = strtok_r(NULL, ";",&save);
        errno = (int)strtol(token, NULL, 10);
        return -1;
    }else{
        char* cnt = malloc(sizeof(char)*MAX_DIM_LEN);
        if (cnt == NULL){
            free(cnt);
            errno = ENOMEM;
            return -1;
        }
        strcpy(cnt, token);
        token = strtok_r(NULL, ";", &save);
        size_t sizeFile = (int) strtol(token, NULL, 10);
        *size = sizeFile;
        *buf = (void*) cnt;
        return 0;
    }
}
int readNFiles(int N, const char* dir){
    if (connesso==0){
        errno=ENOTCONN;
        return -1;
    }

    if (dir != NULL){// se la directory non esiste viene creata
        if (mkdir_p(dir) == -1){
            if (errno != EEXIST) return -1;
        }
    }

    char cmd [DIM_MSG];
    memset(cmd,0,DIM_MSG);
    sprintf(cmd, "readNFiles;%d;",N);
    if (writen(fdSocket,cmd,DIM_MSG) == -1){
        errno = EREMOTEIO;
        return -1;
    }

    char request1 [DIM_MSG];
    memset(request1,0,DIM_MSG);
    if(readn(fdSocket,request1,DIM_MSG)==-1){
        errno = EREMOTEIO;
        return -1;
    }
    char* save = NULL;
    char* token = strtok_r(request1,";",&save);
    if (strcmp(token,"-1") == 0){ //operazione fallita
        token = strtok_r(NULL,";",&save);
        errno = (int)strtol(token, NULL, 10);
        return -1;
    }

    int numFile = (int)strtol(token, NULL, 10);// numFile è il numero di files  letti dal server
    int i;
    last_rN_size = 0;

    //leggo solo un numero di file indicato dal server
    for (i = 0; i < numFile; i++){
        //lettura di un file
        int dim = MAX_DIM_LEN + UNIX_PATH_MAX + 1;
        char readed [dim];
        memset(readed,0,dim);
        if (readn(fdSocket, readed, dim) == -1){
            errno = EREMOTEIO;
            return -1;
        }

        char* save1 = NULL;
        char* absPath = strtok_r(readed,";",&save1); // il path assoluto del file
        char* fileCnt = strtok_r(NULL,";",&save1); // contenuto del file
        // fileName contiene  il nome del file
        char* fileName = basename(absPath);
        last_rN_size = last_rN_size + strnlen(fileCnt,MAX_DIM_LEN);

        if (dir!=NULL){   //salvataggio del file nella cartella specificata
            char pathReal[UNIX_PATH_MAX];
            memset(pathReal,0,UNIX_PATH_MAX);
            sprintf(pathReal,"%s/%s",dir,fileName);

            //se il file non esiste esso viene creato
            FILE* ofp;
            ofp = fopen(pathReal,"w");
            if (ofp == NULL){
                printf("Standard ERROR nell'apertura del file\n");
                return -1;
            }
            else{
                fprintf(ofp,"%s",fileCnt);
                fclose(ofp);
            }
        }
    }

    return numFile;
}

// UPDATE: commenti aggiornati
