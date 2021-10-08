#include "../includes/api.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <fcntl.h>
#include <libgen.h>


#define DIM_MSG 2048
#define MAX_DIM_LEN 1024 // grandezza massima del contenuto di un file: 1 KB
#define UNIX_PATH_MAX 108 /* man 7 unix */

int stampa = 0; // flag per abilitare la stampa
int time_ms; // flag che determina il tempo tra due operazioni

// il nodo della lista dei comandi:
typedef struct node{
    char * cmd; // comando richiesto
    char * arg; // argomento del comando
    struct node* next;
    struct node* prec;

} node;

long isNumber(const char* s){
    char* e = NULL;
    long val = strtol(s, &e, 0);
    if (e != NULL && *e == (char)0) return val;
    return -1;
}

void recSearch (char* dirname, int n, char* dest_dirname){
    int num_files = 0;
    DIR* dir;
    struct dirent* entry;

    if ((dir = opendir(dirname)) == NULL || num_files == n){
        return;
    }

    while ((entry = readdir(dir)) != NULL && (num_files < n)){
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dirname, entry->d_name);

        struct stat info;
        if (stat(path,&info)==-1) {
            perror("Standard ERROR: stat");
            exit(EXIT_FAILURE);
        }

        //se file è una directory
        if (S_ISDIR(info.st_mode)){
            if (strcmp(entry->d_name,".") == 0 || strcmp(entry->d_name,"..") == 0)
                continue;
            recSearch(path,n,dest_dirname);
        }
        else{
            char * resolvedPath = NULL;
            if ((resolvedPath = realpath(path,resolvedPath))==NULL){
                perror("Standard ERROR: realpath");
            }
            else{
                errno = 0;

                if (openFile(resolvedPath,1) == -1){
                    if (errno == ENOENT){
                        if (openFile(resolvedPath,3) == -1){
                            if (stampa==1) printf("OP : -w (scrivi file) File : %s Esito : fallimento\n",resolvedPath);
                            perror("Standard ERROR: apertura del file fallita");
                        }
                        else{// scrittura nel file appena creato all'interno del server
                            num_files++;
                            //scrittura nel file
                            if (writeFile(resolvedPath,dest_dirname) == -1){
                                if (stampa==1)
                                    printf("OP : -w (scrivi file) File : %s Esito : fallimento\n",resolvedPath);
                                perror("Standard ERROR: scrittura nel  file");
                            }
                            if (stampa==1)
                                printf("OP : -w (scrivi file) File : %s Dimensione totale della scrittura: %lu Esito : successo\n",resolvedPath,get_last_w_size());
                            //chiusura del file
                            if (closeFile(resolvedPath) == -1){
                                if (stampa==1)
                                    printf("OP : -w (scrivi file) File : %s Esito : fallimento\n",resolvedPath);
                                perror("Standard ERROR: chiusura del file");
                            }
                        }
                    }
                    else{
                        if (stampa==1)
                            printf("OP : -w (scrivi file) File : %s Esito : fallimento\n",resolvedPath);
                        perror("Standard ERROR: apertura del file fallita");
                    }
                }
                else{// scrittura nel file aperto all'interno del server e già esistente in esso
                    num_files++;
                    //scrittura nel file
                    if (writeFile(resolvedPath,dest_dirname) == -1){
                        if (stampa==1)
                            printf("OP : -w (scrivi file) File : %s Esito : fallimento\n",resolvedPath);
                        perror("Standard ERROR: scrittura nel  file");
                    }
                    if (stampa==1)
                        printf("OP : -w (scrivi file) File : %s Dimensione totale della scrittura: %lu Esito : successo\n",resolvedPath,get_last_w_size());
                    //chiusura del file
                    if (closeFile(resolvedPath)==-1){
                        if (stampa==1)
                            printf("OP : -w (scrivi file) File : %s Esito : fallimento\n",resolvedPath);
                        perror("Standard ERROR: chiusura del file");
                    }
                }
                if (resolvedPath!=NULL)
                    free(resolvedPath);
            }
        }
    }
    if ((closedir(dir))==-1){
        perror("Standard ERROR: closedir");
        exit(EXIT_FAILURE);
    }
}
//aggiunge in coda un comando
void addLast (node** lst, char* cmd, char* arg) {
    node * new = malloc (sizeof(node)); // spazio per memorizzare il nodo comando
    if (new == NULL){
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    new->cmd = malloc(sizeof(cmd)); // spazio per memorizzare la stringa comando
    if (new->cmd==NULL){
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    strcpy(new->cmd, cmd);

    if (arg!=NULL){
        new->arg = malloc(PATH_MAX*sizeof(char)); // spazio per memorizzare la stringa di argomenti
        if (new->arg==NULL){
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        strcpy(new->arg,arg);
    }
    else new->arg = NULL;
    new->next = NULL;

    node * last = *lst;

    if (*lst == NULL){
        *lst = new;
        return;
    }

    while (last->next!=NULL){
        last = last->next;
    }

    last->next = new;
    new->prec = last;
}
//verifica la presenza di un determinato comando
 int containCMD (node** lst, char* cmd, char** arg){

    node * curr = *lst;
    int found = 0;

    // ricerca del comando cmd
    while (curr != NULL && !found){
        if (strcmp(curr->cmd,cmd) == 0)
            found = 1;
        else{
            curr = curr -> next;
        }
    }

    // il comando è stato trovato quindi rimozione
    if (found == 1){
        if (curr->arg != NULL){
            *arg = malloc(sizeof(char)*(strlen(curr->arg)+1));
            strcpy(*arg, curr->arg);
        }
        else *arg = NULL;

        if (curr->prec == NULL){
            if(curr->next != NULL) curr->next->prec = NULL;
            *lst = (*lst)->next;
            free(curr->arg);
            free(curr->cmd);
            free(curr);
        }
        else{
            curr->prec->next = curr->next;
            if(curr->next != NULL) curr->next->prec = curr->prec;
            free(curr->arg);
            free(curr->cmd);
            free(curr);
        }
    }
    return found;
}

void freeList (node ** lst){
    node * tmp;
    while (*lst != NULL){
        tmp = *lst;
        free((*lst)->arg);
        free((*lst)->cmd);
        (*lst)=(*lst)->next;
        free(tmp);
    }
}

int main (int argc, char * argv[]){
    char opt;

    // flags per determinare che i comandi -p e -f non siano ripetuti
    int ff = 0;
    int pf = 0;

    char* dir = NULL; // cartella impostata per le read
    char* Dir = NULL; // cartella impostata per gli overflow in write
    time_ms = 0; // numero di secondi da attendere tra un comando ed un altro

    char* farg;
    node* listCmd = NULL; // lista dei comandi richiesti
    char* resolvedPath = NULL; // stringa per il path assoluto

    // la lista dei comandi viene popolata secondo gli argomenti di avvio
    while ((opt = (char)getopt(argc,argv,"hpf:w:W:u:l:D:r:R:d:t:c:a:")) != -1){
        switch (opt){
            case 'h':{
                printf("OPERAZIONI SUPPORTATE: \n");
                printf("-h help\n-f filename\n-w dirname[,n=0]\n-W file1[,file2]\n");
                printf("-D dirname\n-r file1[,file2]\n-R [n=0]\n-d dirname\n-t time\n");
                printf("-l file1[,file2]\n-u file1[,file2]\n-c file1[,file2]\n-p abilita stampa\n");
                if (stampa==1)
                    printf("OP : -h (aiuto) Esito : successo\n");
                freeList(&listCmd);
                return 0;// il processo termina immediatamente dopo la stampa
                }
                break;
            case 'p':{
                if (pf == 0){
                    pf = 1;
                    addLast(&listCmd, "p", NULL);
                }
                else{
                    printf("Standard ERROR : L'opzione -p può essere richiesta al più una volta\n");
                }
                break;
            }
            case 'f':{
                if (ff == 0){
                    farg = optarg;
                    ff = 1;
                    addLast(&listCmd, "f", optarg);
                }
                else{
                    printf("Standard ERROR : L'opzione -f può essere richiesta al più una volta\n");
                }
                break;
            }

            // i seguenti comandi possono essere ripetuti più volte
            case 'w':{
                addLast(&listCmd, "w", optarg);
                break;
            }
            case 'W':{
                addLast(&listCmd,"W",optarg);
                break;
            }
            case 'D':{
                addLast(&listCmd, "D", optarg);
                break;
            }
            case 'r':{
                addLast(&listCmd, "r", optarg);
                break;
            }
            case 'R':{
                addLast(&listCmd, "R", optarg);
                break;
            }
            case 'd':{
                addLast(&listCmd, "d", optarg);
                break;
            }
            case 't':{
                addLast(&listCmd, "t", optarg);
                break;
            }
            case 'l':{
                addLast(&listCmd, "l", optarg);
                break;
            }
            case 'u':{
                addLast(&listCmd, "u", optarg);
                break;
            }
            case 'c':{
                addLast(&listCmd, "c", optarg);
                break;
            }
            case 'a':{
                addLast(&listCmd, "a", optarg);
                break;

            }
            case '?':
            {// comando non riconosciuto
                fprintf(stderr, "digitare : %s -h per ottenere la lista dei comandi\n", argv[0]);
                break;
            }
            case ':':{
                printf("l'opzione '-%c' richiede almeno un argomento", optopt);
                break;
            }
            default:;
        }
    }

    // controlli per la presenza delle opzioni non ripetibili: -h -p -f
    char* arg = NULL;
    if (containCMD(&listCmd,"p",&arg) == 1){
        stampa = 1;
        printf("OP : -p (abilta stampe) Esito : successo\n");
        msSleep(time_ms); // attesa tra due operazioni consecutive
    }

    if (containCMD(&listCmd,"f",&arg) == 1){
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME,&ts);
        ts.tv_sec = ts.tv_sec+60;
        if (openConnection(arg,1000,ts)==-1){
            if (stampa == 1) printf("OP : -f (connessione) File : %s Esito : fallimento\n",arg);
            perror("Standard ERROR: apertura della connessione");
        }
        else{
            if (stampa==1) printf("OP : -f (connessione) File : %s Esito : successo\n",arg);
        }
        msSleep(time_ms); // attesa tra due operazioni consecutive
    }

    // le operazioni restanti: -w -W -D -r -R -d -t -l -u -c
    node * curr = listCmd;
    while (curr != NULL){

        if (strcmp(curr->cmd,"w") == 0){
            Dir = NULL;
            if (curr->next != NULL){
                if(strcmp(curr->next->cmd,"D") == 0) Dir = curr->next->arg;
            }

            char* save1 = NULL;
            char* token1 = strtok_r(curr->arg,",",&save1);
            char* namedir = token1;
            int n;

            struct stat info_dir;
            if (stat(namedir,&info_dir)==-1){
                if (stampa==1)
                    printf("OP : -w (scrivi directory) Directory : %s Esito : fallimento\n",namedir);
                printf("Standard ERROR: %s non e' una directory valida\n",namedir);
            }
            else{
                if (S_ISDIR(info_dir.st_mode)){
                    token1 = strtok_r(NULL,",",&save1);
                    if (token1!=NULL){
                        n = (int)isNumber(token1);
                    }
                    else n=0;

                    if (n>0){
                        recSearch(namedir,n,Dir);
                        if (stampa==1) printf("OP : -w (scrivi directory) Directory : %s Esito : successo\n",namedir);
                    }
                    else
                        if (n == 0){
                            recSearch(namedir, INT_MAX, Dir);
                            if (stampa==1) printf("OP : -w (scrivi directory) Directory : %s Esito : successo\n",namedir);
                        }
                        else{
                            if (stampa==1) printf("OP : -w (scrivi directory) Directory : %s Esito : fallimento\n",namedir);
                            printf("Standard ERROR: Utilizzo : -w dirname[,n]\n");
                        }
                }
                else{
                    if (stampa==1)
                        printf("OP : -w (scrivi directory) Directory : %s Esito : fallimento\n",namedir);
                    printf("Standard ERROR: %s non e' una directory valida\n",namedir);
                }
            }

        }
        else if (strcmp(curr->cmd, "a") == 0){ //append -> a fileName(arg),contenutoDaAppendere(stringa)
            Dir = NULL;
            if (curr->next != NULL){
                if(strcmp(curr->next->cmd,"D") == 0)
                    Dir = curr->next->arg;
            }
            char* saveA = NULL;
            char* file = strtok_r(curr->arg, ",", &saveA);

            if ((resolvedPath = realpath(file,resolvedPath)) == NULL){
                if (stampa == 1)
                    printf("OP : -W (scrivi file) File : %s Esito : fallimento\n",file);
                perror("Standard ERROR: Il file non esiste\n");
            }

            char* cnt = strtok_r(curr -> arg, ",", &saveA);
            size_t size = strlen(cnt);
            struct stat info_file;
            stat(resolvedPath,&info_file);

            if (S_ISREG(info_file.st_mode)){
                errno = 0;

                if (openFile(resolvedPath,1) == -1){
                    if (errno == ENOENT){
                        if (openFile(resolvedPath,3) == -1){
                            if (stampa==1) printf("OP : -W (scrivi file) File : %s Esito : fallimento\n",file);
                            perror("Standard ERROR: apertura del file fallita");
                        }
                        else{// scrittura nel file appena creato all'interno del server
                            //scrittura nel file
                            if (appendToFile(resolvedPath, cnt, size, Dir) == -1){
                                if (stampa==1)
                                    printf("OP : -a (scrivi file) File : %s Esito : fallimento\n",file);
                                perror("Standard ERROR: scrittura nel  file");
                            }
                            if (stampa==1)
                                printf("OP : -a (scrivi file) File : %s Dimensione totale della scrittura: %lu Esito : successo\n",file,get_last_w_size());
                            //chiusura del file
                            if (closeFile(resolvedPath)==-1){
                                if (stampa==1)
                                    printf("OP : -a (scrivi file) File : %s Esito : fallimento\n",file);
                                perror("Standard ERROR: chiusura del file");
                            }
                        }
                    }
                    else{
                        if (stampa==1)
                            printf("OP : -a (scrivi file) File : %s Esito : fallimento\n",file);
                        perror("Standard ERROR: apertura del file fallita");
                    }
                }
                else{// scrittura nel file aperto all'interno del server e già esistente in esso
                    //scrittura nel file
                    if (appendToFile(resolvedPath, cnt, size, Dir) == -1){
                        if (stampa==1)
                            printf("OP : -a (scrivi file) File : %s Esito : fallimento\n",file);
                        perror("Standard ERROR: scrittura nel  file");
                    }
                    if (stampa == 1)
                        printf("OP : -a (scrivi file) File : %s Dimensione totale della scrittura: %lu Esito : successo\n",file,get_last_w_size());
                    //chiusura del file
                    if (closeFile(resolvedPath)==-1){
                        if (stampa==1)
                            printf("OP : -a (scrivi file) File : %s Esito : fallimento\n",file);
                        perror("Standard ERROR: chiusura del file");
                    }
                }
                if (resolvedPath!=NULL) free(resolvedPath);
                resolvedPath = NULL;
            }
            else{
                if (stampa == 1)
                    printf("OP : -a (scrivi file) File : %s Esito : fallimento\n",file);
                printf("Standard ERROR: %s non e' un file regolare\n",file);
            }
        }
        else if (strcmp(curr->cmd,"W") == 0){
                Dir = NULL;
                if (curr->next != NULL){
                    if(strcmp(curr->next->cmd,"D") == 0) Dir = curr->next->arg;
                }

                char* save2 = NULL;
                char* token2 = strtok_r(curr->arg,",",&save2);



                while(token2){
                    char* file = token2;
                    // per ogni file passato come argomento sarà eseguita la serie "open-write-close"

                    if ((resolvedPath = realpath(file,resolvedPath)) == NULL){
                        if (stampa==1) printf("OP : -W (scrivi file) File : %s Esito : fallimento\n",file);
                        perror("Standard ERROR: Il file non esiste\n");
                    }
                    else{
                        struct stat info_file;
                        stat(resolvedPath,&info_file);

                        if (S_ISREG(info_file.st_mode)){
                            errno = 0;

                            if (openFile(resolvedPath,1) == -1){
                                if (errno == ENOENT){
                                    if (openFile(resolvedPath,3) == -1){
                                        if (stampa==1) printf("OP : -W (scrivi file) File : %s Esito : fallimento\n",file);
                                        perror("Standard ERROR: apertura del file fallita");
                                    }
                                    else{// scrittura nel file appena creato all'interno del server
                                        //scrittura nel file
                                        if (writeFile(resolvedPath,Dir) == -1){
                                            if (stampa==1)
                                                printf("OP : -W (scrivi file) File : %s Esito : fallimento\n",file);
                                            perror("Standard ERROR: scrittura nel  file");
                                        }
                                        if (stampa==1)
                                            printf("OP : -W (scrivi file) File : %s Dimensione totale della scrittura: %lu Esito : successo\n",file,get_last_w_size());
                                        //chiusura del file
                                        if (closeFile(resolvedPath)==-1){
                                            if (stampa==1)
                                                printf("OP : -W (scrivi file) File : %s Esito : fallimento\n",file);
                                            perror("Standard ERROR: chiusura del file");
                                        }
                                    }
                                }
                                else{
                                    if (stampa==1)
                                        printf("OP : -W (scrivi file) File : %s Esito : fallimento\n",file);
                                    perror("Standard ERROR: apertura del file fallita");
                                }
                            }
                            else{// scrittura nel file aperto all'interno del server e già esistente in esso
                                //scrittura nel file
                                if (writeFile(resolvedPath,Dir) == -1){
                                    if (stampa==1)
                                        printf("OP : -W (scrivi file) File : %s Esito : fallimento\n",file);
                                    perror("Standard ERROR: scrittura nel  file");
                                }
                                if (stampa==1) printf("OP : -W (scrivi file) File : %s Dimensione totale della scrittura: %lu Esito : successo\n",file,get_last_w_size());
                                //chiusura del file
                                if (closeFile(resolvedPath)==-1){
                                    if (stampa==1)
                                        printf("OP : -W (scrivi file) File : %s Esito : fallimento\n",file);
                                    perror("Standard ERROR: chiusura del file");
                                }
                            }
                            if (resolvedPath!=NULL) free(resolvedPath);
                            resolvedPath = NULL;
                        }
                        else{
                            if (stampa==1) printf("OP : -W (scrivi file) File : %s Esito : fallimento\n",file);
                            printf("Standard ERROR: %s non e' un file regolare\n",file);
                        }
                    }
                    token2 = strtok_r(NULL,",",&save2);
                }
            }
        else if (strcmp(curr->cmd,"D") == 0){
            if (curr->prec != NULL ){
                if (strcmp(curr->prec->cmd,"w") == 0 || strcmp(curr->prec->cmd,"W") == 0){
                    if (stampa==1)
                        printf("OP : -D (seleziona directory di destinazione per -w o -W) Esito : successo\n");
                }
                else if (stampa==1)
                    printf("OP : -D (seleziona directory di destinazione per -w o -W) Esito : fallimento\n");
            }
            else if (stampa==1)
                printf("OP : -D (seleziona directory di destinazione per -w o -W) Esito : fallimento\n");
        }
        else if (strcmp(curr->cmd,"r")==0){
            dir = NULL;
            if (curr->next != NULL){
                if(strcmp(curr->next->cmd,"d") == 0) dir = curr->next->arg;
            }

            char * save3 = NULL;
            char * token3 = strtok_r(curr->arg,",",&save3);

            while(token3 != NULL){
                char *file = token3;

                if ((resolvedPath = realpath(file, resolvedPath)) == NULL){
                    if (stampa == 1)
                        printf("OP : -r (leggi file) File : %s Esito : fallimento\n", file);
                    printf("Standard ERROR: Il file %s non esiste\n", file);
                } else {
                    struct stat info_file;
                    stat(resolvedPath, &info_file);

                    if (S_ISREG(info_file.st_mode)){
                        errno = 0;
                        //per ogni file passato come argomento sarà eseguita la serie open-read-close

                        if (openFile(resolvedPath, 0) == -1){
                            if (stampa == 1) printf("OP : -r (leggi file) File : %s Esito : fallimento\n", file);
                            perror("Standard ERROR: apertura del file");
                        }
                        else{//READ FILE
                            char* buf = NULL;
                            size_t size;
                            if (readFile(resolvedPath, (void **) &buf, &size) == -1){
                                if (stampa == 1) printf("OP : -r (leggi file) File : %s Esito : fallimento\n", file);
                                perror("Standard ERROR: lettura del file");
                            }
                            else{
                                if (dir != NULL) {
                                    //il file letto sarà salvato nella directory impostata
                                    char path[UNIX_PATH_MAX];
                                    memset(path, 0, UNIX_PATH_MAX);
                                    char *file_name = basename(file);
                                    sprintf(path, "%s/%s", dir, file_name);

                                    //se la directory non esiste essa viene creata
                                    mkdir_p(dir);
                                    //se il file non esiste esso viene creato
                                    FILE *of;
                                    of = fopen(path, "w");
                                    if (of == NULL) {
                                        if (stampa == 1)
                                            printf("OP : -r (leggi file) File : %s Esito : fallimento\n", file);
                                        perror("Standard ERROR: salvataggio del file\n");
                                    } else {
                                        fprintf(of, "%s", buf);
                                        fclose(of);
                                    }
                                }
                            }
                            if (closeFile(resolvedPath) == -1) {
                                if (stampa == 1)
                                    printf("OP : -r (leggi file) File : %s Esito : fallimento\n", file);
                                perror("Standard ERROR: chiusura file");
                            } else {
                                if (stampa == 1)
                                    printf("OP : -r (leggi file) File : %s Dimensione Letta: %lu Esito : successo\n",
                                           file,
                                           strnlen(buf, MAX_DIM_LEN));
                            }
                            free(buf);
                        }
                        token3 = strtok_r(NULL, ",", &save3);
                    }
                    else {
                        if (stampa == 1) printf("OP : -r (leggi file) File : %s Esito : fallimento\n", file);
                        printf("Standard ERROR: %s non e' un file regolare\n", file);
                    }
                }
                token3 = strtok_r(NULL, ",", &save3);
            }
        }
        else if (strcmp(curr->cmd,"R") == 0){
            dir = NULL;
            if (curr->next != NULL){
                if(strcmp(curr->next->cmd,"d") == 0) dir = curr->next->arg;
            }

            int N;
            if ((N = (int)isNumber(curr->arg)) == -1){
                if (stampa==1) printf("OP : -R (leggi N file) Esito : fallimento\n");
                printf("L'opzione -R vuole un numero come argomento\n");
            }
            else{
                int n;

                if ((n = readNFiles(N,dir))==-1){
                    if (stampa==1) printf("OP : -R (leggi N file) Esito : fallimento\n");
                    perror("Standard ERROR: lettura file");
                }
                else{
                    if (stampa==1) printf("OP : -R (leggi N file) File Letti : %d Dimensione Letta: %lu Esito : successo \n",n,get_last_rN_size());
                }

            }
        }
        else if (strcmp(curr->cmd,"d") == 0){
            if (curr->prec != NULL ){
                if (strcmp(curr->prec->cmd,"r") == 0 || strcmp(curr->prec->cmd,"R") == 0){
                    if (stampa==1) printf("OP : -d (seleziona directory di destinazione per -r o -R) Esito : successo\n");
                }
                else if (stampa==1) printf("OP : -d (seleziona directory di destinazione per -r o -R) Esito : fallimento\n");
            }
            else if (stampa==1) printf("OP : -d (seleziona directory di destinazione per -r o -R) Esito : fallimento\n");
        }
        else if (strcmp(curr->cmd,"c")==0){
            char * save4 = NULL;
            char * token4 = strtok_r(curr->arg,",",&save4);

            while(token4) {
                char * file = token4;

                //per ogni file passato come argomento sarà eseguita remove
                if ((resolvedPath = realpath(file, resolvedPath)) == NULL){
                    if (stampa == 1)
                        printf("OP : -l (ottieni la mutua esclusione sul file) File : %s Esito : fallimento\n", file);
                    printf("Standard ERROR: Il file %s non esiste\n", file);
                } else {
                    struct stat info_file;
                    stat(resolvedPath, &info_file);

                    if (S_ISREG(info_file.st_mode))//per ogni file passato come argomento sarà eseguita lock
                        if (removeFile(resolvedPath)==-1){
                            if (stampa==1)
                                printf("OP : -c (rimuovi file) File : %s Esito : fallimento\n",file);
                            perror("Standard ERROR: lock del file");
                        }else{
                            if (stampa==1)
                                printf("OP : -c (rimuovi file) File : %s Esito : successo\n",file);
                        }
                    else {
                        if (stampa == 1) printf("OP : -c (rimuovi file) File : %s Esito : fallimento\n", file);
                        printf("Standard ERROR: %s non e' un file regolare\n", file);
                    }
                }

                token4 = strtok_r(NULL,",",&save4);
            }

        }
        else if (strcmp(curr->cmd,"l")==0){
            char * save4 = NULL;
            char * token4 = strtok_r(curr->arg,",",&save4);

            while(token4){
                char * file = token4;

                if ((resolvedPath = realpath(file, resolvedPath)) == NULL){
                    if (stampa == 1)
                        printf("OP : -l (ottieni la mutua esclusione sul file) File : %s Esito : fallimento\n", file);
                    printf("Standard ERROR: Il file %s non esiste\n", file);
                } else {
                    struct stat info_file;
                    stat(resolvedPath, &info_file);

                    if (S_ISREG(info_file.st_mode))//per ogni file passato come argomento sarà eseguita lock
                        if (lockFile(resolvedPath)==-1){
                            if (stampa==1)
                                printf("OP : -l (ottieni la mutua esclusione sul file) File : %s Esito : fallimento\n",file);
                            perror("Standard ERROR: lock del file");
                        }else{
                            if (stampa==1)
                                printf("OP : -l (ottieni la mutua esclusione sul file) File : %s Esito : successo\n",file);
                        }
                    else {
                        if (stampa == 1) printf("OP : -l (ottieni la mutua esclusione sul file) File : %s Esito : fallimento\n", file);
                        printf("Standard ERROR: %s non e' un file regolare\n", file);
                    }
                }
                token4 = strtok_r(NULL,",",&save4);
            }

        }
        else if (strcmp(curr->cmd,"u")==0){
            char * save4 = NULL;
            char * token4 = strtok_r(curr->arg,",",&save4);

            while(token4) {
                char * file = token4;

                //per ogni file passato come argomento sarà eseguita unlock
                if ((resolvedPath = realpath(file, resolvedPath)) == NULL){
                    if (stampa == 1)
                        printf("OP : -u (rilascia la mutua esclusione sul file) File : %s Esito : fallimento\n", file);
                    printf("Standard ERROR: Il file %s non esiste\n", file);
                } else {
                    struct stat info_file;
                    stat(resolvedPath, &info_file);

                    if (S_ISREG(info_file.st_mode))//per ogni file passato come argomento sarà eseguita lock
                        if (unlockFile(resolvedPath)==-1){
                            if (stampa==1) printf("OP : -l (ottieni la mutua esclusione sul file) File : %s Esito : fallimento\n",file);
                            perror("Standard ERROR: unlock del file");
                        }else{
                            if (stampa==1) printf("OP : -u (rilascia la mutua esclusione sul file) File : %s Esito : successo\n",file);
                        }
                    else {
                        if (stampa == 1) printf("OP : -u (rilascia la mutua esclusione sul file) File : %s Esito : fallimento\n", file);
                        printf("Standard ERROR: %s non e' un file regolare\n", file);
                    }
                }

                token4 = strtok_r(NULL,",",&save4);
            }

        }
        else if (strcmp(curr->cmd,"t")==0){
            char * time_s = curr->arg;
            if (time_s == NULL){
                if (stampa==1)
                    printf("OP : -t (imposta il tempo che intercorre tra due operazioni) Esito : fallimento\n");
            }
            else{
                time_ms = (int)isNumber(time_s);
                if(time_ms == -1){
                    perror("Standard ERROR nel settaggio del timer");
                }
                if (time_ms == 0){
                    if (stampa==1)
                        printf("OP : -t (imposta il tempo che intercorre tra due operazioni) Esito : fallimento\n");
                }
                else if (stampa == 1)
                    printf("OP : -t (imposta il tempo che intercorre tra due operazioni) Esito : successo\n");
            }
        }

        msSleep(time_ms); // attesa tra due operazioni consecutive
        curr = curr->next;
    }

    freeList(&listCmd);
    free(resolvedPath);
    free(arg);
    //chiusura della connessione una volta eseguite tutte le richieste
    closeConnection(farg);
    return 0;
}


