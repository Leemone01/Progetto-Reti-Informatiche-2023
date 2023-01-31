#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#define BUFLEN 1024
#define MAX_COMANDE 5       //numero massimo di comande che il kd può gestire contemporaneamente 

struct comanda{
    int numero_tavolo;          //tavolo da cui è uscita comanda
    int numero_comanda;         //numero della comanda per quel tavolo
    char descrizione[BUFLEN];   //contenuto della comanda
};

struct comanda array_comande[MAX_COMANDE];  //array che contiene le comande attualmente accettate dal kd

void comandi_disponibili(){
    printf("\n***************************** BENVENUTO *****************************\n");
    printf("Digita un comando: \n");
    printf("\n");
    printf("take --> accetta una comanda\n");
    printf("show --> mostra le comande accettate (in preparazione)\n");
    printf("ready --> imposta lo stato 'in servizio' della comanda\n");
    printf("\n");
    fflush(stdout);
}

int take_possibile(){           //vede se è possibile accettare una nuova comanda (e restituisce l'indice di array_comande in cui metterla) o meno (restituisce -1)
    int i;
    for (i = 0; i < MAX_COMANDE; i++){
        if (array_comande[i].numero_tavolo == 0){
            return i;
        }
    }
    return -1;
}

void gestione_errore(int ret, int sd){      //sia in caso di errore nella ricezione/invio, che in caso di chiusura del socket remoto, termino
    if(ret == -1){
        perror("ERRORE nella comunicazione con il server");
        printf("ARRESTO IN CORSO...\n");
        fflush(stdout);
        exit(1);
    }
    else if (ret == 0){
        printf("AVVISO: il server ha chiuso il socket remoto.\nARRESTO IN CORSO...\n");
        fflush(stdout);
        exit(1);
    }
}


int main(int argc, char *argv[]){
    //VARIABILI                                                                                         
    char buf[BUFLEN];                               //variabili per socket
    int sd;                                       
    in_port_t porta = htons(atoi(argv[1]));     
    struct sockaddr_in cl_addr, sv_addr; 

    fd_set master, read_fds;                        //variabili per set
    int fdmax;

    int j, ret;                                     //variabili varie
    uint32_t mex_len;                               //conterrà la lunghezza del messaggio da inviare/ricevere espressa in host order
    uint32_t mex_len_n;                             //conterrà la lunghezza del messaggio da inviare/ricevere espressa in network order
    int id;                                         //conterrà il valore di ritorno di take_possibile()
    int offset = 0;                                 //sarà usata per inserire le varie ordinazioni di una comanda una dopo l'altra in array_comande[].descrizione
    int numero_comanda;                             //conterrà il numero della comanda passata dal server
    int numero_tavolo;                              //conterrà il numero del tavolo da cui è provenuta la comanda passata dal server
    int check;                                      //varrà 1 se una certa comanda (o certe comande) sono state trovate in array_comande (vedi dopo "rimuovi" e "ready")

    char comando[BUFLEN];                           //metterò il comando inserito dall'utente
       
    //CREAZIONE SOCKET
    memset((void*)&cl_addr, 0, sizeof(cl_addr));
    memset((void*)&sv_addr, 0, sizeof(sv_addr));

    sd = socket(AF_INET, SOCK_STREAM, 0);
    cl_addr.sin_family = AF_INET;
    cl_addr.sin_port = porta;
    cl_addr.sin_addr.s_addr = INADDR_ANY;
    ret = bind(sd, (struct sockaddr*)&cl_addr, sizeof(cl_addr));
    if (ret == -1){
        perror("ERRORE nella bind()");
        printf("ARRESTO IN CORSO...\n");
        fflush(stdout);
        exit(1);
    } 

    sv_addr.sin_family = AF_INET;
    sv_addr.sin_port = htons(4242);
    sv_addr.sin_addr.s_addr = INADDR_ANY;

    ret = connect(sd, (struct sockaddr*)&sv_addr, sizeof(sv_addr));
    if (ret == -1){
        perror("ERRORE nella connect()");
        printf("ARRESTO IN CORSO...\n");
        fflush(stdout);
        exit(1);
    }
    
    ret = send(sd, "K", 2, 0);                                  //una volta connesso, mando subito il codice di identificazione
    gestione_errore(ret, sd);
 
    ret = recv(sd, buf, 3, 0);
    gestione_errore(ret, sd);
    if (strcmp(buf, "E1") == 0){                                //se ci sono troppi kd connessi, avviso e termino
        printf("ERRORE: è stato raggiunto il numero massimo di kitchen device connessi.\nARRESTO IN CORSO...\n");
        fflush(stdout);
        close(sd);
        exit(1);
    }

    //CREAZIONE SET E ALTRE INIZIALIZZAZIONI
    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    FD_SET(0, &master);
    FD_SET(sd, &master);
    memset(array_comande, 0, sizeof(array_comande));

    fdmax = sd;
    comandi_disponibili();

    while(1){
        memset(comando, 0, sizeof(comando));                    //metto a 0 comando, sennò se si preme semplicemente invio, rimane l'ultima cosa che ci era finita dentro
        read_fds = master;
        select(fdmax + 1, &read_fds, NULL, NULL, NULL);

        
        if(FD_ISSET(sd, &read_fds)){                            //PRONTO SOCKET DI COMUNICAZIONE
            ret = recv(sd, &mex_len_n, sizeof(uint32_t), 0);  
            gestione_errore(ret, sd);                           //questa chiamata a 'gestione_errore()' gestisce in particolare il caso in cui il server è stato chiuso senza usare 'stop'.
            mex_len = ntohl(mex_len_n);  
            ret = recv(sd, buf, mex_len, 0);
            gestione_errore(ret, sd);
            sscanf(buf, "%s", comando);                                                           
            if (strcmp(comando, "AR") == 0){                                //se il server ha chiamato "stop", avviso e termino.
                printf("AVVISO: il server è stato arrestato.\nARRESTO IN CORSO...\n");
                fflush(stdout);
                close(sd);
                exit(0);
            }
            else if(strcmp(comando, "rimuovi") == 0){                       //se il server avvisa il kd di smettere di gestire le comande uscite da un certo tavolo, vedo se il kd effettivamente le gestisce.
                check = 0;                                                  //se sì, le rimuovo da array_comande e mostro a video che è stata effettuata una modifica.
                sscanf(buf, "%s T%d", comando, &numero_tavolo);                              
                for (j = 0; j < MAX_COMANDE; j++){
                    if (array_comande[j].numero_tavolo == numero_tavolo){
                        memset(&array_comande[j], 0, sizeof(array_comande[j]));
                        check = 1;                                              //metto check = 1 se ho rimosso una comanda
                    }
                }
                if (check == 1){                                                //e se check == 1, mostro a video che alcune comande non vanno più preparate
                    printf("AVVISO: alcune comande non vanno più preparate. Controlla quali sono rimaste usando il comando 'show'.\n");
                    fflush(stdout);
                }
            }
            else if(strcmp(comando, "nuova") == 0){                         //se il server avvisa il kd che c'è una nuova comanda in attesa, mostro a video la notifica.
                printf("AVVISO: nuova comanda in attesa. Usa il comando 'take' per accettarla.\n");
                fflush(stdout);
            }    
        }

        else{                                       //PRONTO STDIN
            fgets(buf, BUFLEN, stdin);              
            sscanf(buf, "%s", comando);             

            if (strcmp(comando, "take") == 0){
                id = take_possibile();              //prendo l'indice in cui verrà messa la comanda
                if (id == -1){                      //se si è raggiunto il numero max di comande accettabili, avviso.
                    printf("ERRORE: hai raggiunto il numero massimo di comande accettabili. Finisci di gestire le correnti.\n");
                    fflush(stdout);
                    continue;
                }

                mex_len = strlen(comando) + 1;          //altrimenti, si può accettare una comanda, dunque giro il comando al server
                mex_len_n = htonl(mex_len);
                ret = send(sd, &mex_len_n, sizeof(uint32_t), 0);  
                gestione_errore(ret, sd);
                ret = send(sd, comando, mex_len, 0);
                gestione_errore(ret, sd);

                ret = recv(sd, buf, 3, 0);
                gestione_errore(ret, sd);
                if (strcmp(buf, "E2") == 0){            //se non ci sono comande da accettare, avviso
                    printf("ERRORE: al momento non c'è alcuna comanda da accettare.\n");
                    fflush(stdout);
                    continue;
                }
                                                                        //altrimenti, riceverò l'OK e in più i dettagli della comanda.                                                               
                ret = recv(sd, &mex_len_n, sizeof(uint32_t), 0);        //prima ricevo la riga di intestazione "comx Ty"
                gestione_errore(ret, sd); 
                mex_len = ntohl(mex_len_n);
                ret = recv(sd, buf, mex_len, 0);                        
                gestione_errore(ret, sd);                                
                sscanf(buf, "com%d T%d", &array_comande[id].numero_comanda, &array_comande[id].numero_tavolo);

                offset = 0;
                for(;;){                                                //poi ricevo le varie ordinazioni della comanda, finché non ricevo un messaggio "FIN"
                    ret = recv(sd, &mex_len_n, sizeof(uint32_t), 0);
                    gestione_errore(ret, sd);
                    mex_len = ntohl(mex_len_n);
                    ret = recv(sd, buf, mex_len, 0);
                    gestione_errore(ret, sd);
                    if (strcmp(buf, "FIN") == 0){
                        break;
                    }
                    sprintf((array_comande[id].descrizione+offset), "%s\n", buf);
                    offset = strlen(array_comande[id].descrizione);                                 //ogni ordinazione la inizio a scrivere in array_comande[id].descrizione partendo dalla fine (fine stringa) della precedente                                                                                
                }
                printf("com%d T%d\n%s\n", array_comande[id].numero_comanda, array_comande[id].numero_tavolo, array_comande[id].descrizione);    //una volta ricevuta tutta la comanda, la stampo.
                fflush(stdout);

            }

            else if(strcmp(comando, "show") == 0){
                for (j = 0; j < MAX_COMANDE; j++){
                    if (array_comande[j].numero_tavolo != 0){
                        printf("com%d T%d\n%s", array_comande[j].numero_comanda, array_comande[j].numero_tavolo, array_comande[j].descrizione);
                        fflush(stdout);
                    }
                }
            }

            else if (strcmp(comando, "ready") == 0){
                if (sscanf(buf, "%s com%d-T%d", comando, &numero_comanda, &numero_tavolo) == 3){
                    check = 0;
                    for (j = 0; j < MAX_COMANDE; j++){
                        if (array_comande[j].numero_tavolo == numero_tavolo && array_comande[j].numero_comanda == numero_comanda){      //prima trovo la comanda
                            sprintf(buf, "servizio %d %d", numero_comanda, numero_tavolo);                                              //se la trovo, avviso il server che ora questa comanda è in servizio
                            mex_len = strlen(buf) + 1;
                            mex_len_n = htonl(mex_len);
                            ret = send(sd, &mex_len_n, sizeof(uint32_t), 0);
                            gestione_errore(ret, sd);
                            ret = send(sd, buf, mex_len, 0);
                            gestione_errore(ret, sd);
                            printf("COMANDA IN SERVIZIO\n");
                            fflush(stdout);
                            memset(&array_comande[j], 0, sizeof(array_comande[j]));     //svuoto array_comande[j], così che questo indice sarà riempito poi da una futura comanda accettata. 
                            check = 1;                                                  //mi segno che ho trovato la comanda interessata
                            break;                                                      //ed esco dal for
                        }
                    }
                    if (check == 0){                        //se sono uscito dal for e check = 0, significa che è stato passato un numero della comanda e/o del tavolo che non sono tra le comande accettate. Avviso.
                        printf("ERRORE: numero della comanda e/o del tavolo non validi.\n");
                        fflush(stdout);      
                    }
                }
                else{
                    printf("ERRORE: comando 'ready' invocato in maniera non corretta. La sintassi è 'ready comx-Ty'.\n");
                    fflush(stdout);
                }
            }

            else{
                printf("ERRORE: comando non valido.\n");
                fflush(stdout);
            }
            
        }
    }
    return 0;
}