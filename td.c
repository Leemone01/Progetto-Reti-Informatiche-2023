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

void comandi_disponibili(){
    printf("\n***************************** BENVENUTO *****************************\n");
    printf("Digita un comando: \n");
    printf("\n");
    printf("menu --> mostra il menu dei piatti \n");
    printf("comanda --> invia una comanda \n");
    printf("conto --> chiedi il conto \n");
    printf("\n");
    fflush(stdout);
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

    int ret, numero;                                //variabili varie
    int contatore_comande = 1;                      //conterrà il numero della prossima comanda che uscirà dal td. Sarà rimesso a 1 ogni volta che si chiede il conto
    uint32_t mex_len;                               //conterrà la lunghezza del messaggio da inviare/ricevere espressa in host order
    uint32_t mex_len_n;                             //conterrà la lunghezza del messaggio da inviare/ricevere espressa in network order

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

    //CREAZIONE SET E ALTRE INIZIALIZZAZIONI
    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    FD_SET(0, &master);
    FD_SET(sd, &master);

    fdmax = sd;
    
    ret = send(sd, "T", 2, 0);                                  //una volta connesso, mando subito il codice di identificazione
    gestione_errore(ret, sd);
 
    ret = recv(sd, buf, 3, 0);
    gestione_errore(ret, sd);
    if (strcmp(buf, "E1") == 0){                                //se ci sono troppi td connessi, avviso e termino 
        printf("ERRORE: è stato raggiunto il numero massimo di table device connessi.\nARRESTO IN CORSO...\n");
        fflush(stdout);
        close(sd);
        exit(1);
    }

b1:                                                             //altrimenti, il td si blocca chiedendo in quale tavolo si trova
    printf("Inserisci il numero del tavolo in cui si trova questo table device:\n");
    fflush(stdout);
    while(1){
        read_fds = master;
        select(fdmax + 1, &read_fds, NULL, NULL, NULL);

        if(FD_ISSET(sd, &read_fds)){                                //PRONTO SOCKET DI COMUNICAZIONE 
            ret = recv(sd, &mex_len_n, sizeof(uint32_t), 0);        //l'unico caso in cui è pronto è quando il server chiude il proprio socket di comunicazione (ret == 0), che viene gestito da questa chiamata a "gestione_errore()".
            gestione_errore(ret, sd);                               //Al momento, questo è l'unico modo che ho per accorgermi della terminazione del server, anche nel caso in cui questo dovesse terminare chiamando "stop", perché non avendo ancora indicato il numero del tavolo in cui il td si trova, 
        }                                                           //il descrittore del socket di comunicazione remoto ancora non è stato messo in array_tds[], e quindi a tale socket non verrà mandato il messaggio "AR".

        else{                                                       //PRONTO STDIN
            fgets(buf, BUFLEN, stdin);                              //l'utente inserisce il numero del tavolo in cui il td si trova.
            if (sscanf(buf, "%d", &numero) == 1){                   //se è un valore numerico, esco dal while
                break;                                                      
            }
            printf("ERRORE: devi inserire un valore numerico.\n");
            fflush(stdout);
        }    
    }

    numero--;                                   //Ricevuto un valore numerico, lo decremento e lo mando al server, che così controllerà se questo valore è valido (e cioè se è un indice di array_tds[] e se per quel tavolo c'è già o meno un td)   
    sprintf(buf, "tavolo %d", numero);          
    mex_len = strlen(buf) + 1;
    mex_len_n = htonl(mex_len);
    ret = send(sd, &mex_len_n, sizeof(uint32_t), 0);
    gestione_errore(ret, sd);                  
    ret = send(sd, buf, mex_len, 0);
    gestione_errore(ret, sd); 
                                                                
    ret = recv(sd, buf, 3, 0);  
    gestione_errore(ret, sd);                                      
    if (strcmp(buf, "E2") == 0){                //Se il numero non era valido, avviso e torno a richiedere il numero di un tavolo.
        printf("ERRORE: numero non valido o tavolo con già un table device connesso.\n");
        fflush(stdout);                  
        goto b1;                                             
    }
    printf("Associazione completata!\n");       
    fflush(stdout);

b2:                                             //altrimenti, il td si blocca chiedendo un codice di prenotazione valido
    printf("Inserisci un codice di prenotazione:\n");
    fflush(stdout);
    while(1){
        read_fds = master;
        select(fdmax + 1, &read_fds, NULL, NULL, NULL);
        if(FD_ISSET(sd, &read_fds)){                                    //PRONTO SOCKET DI COMUNICAZIONE
            ret = recv(sd, &mex_len_n, sizeof(uint32_t), 0);
            gestione_errore(ret, sd);                                   //questa chiamata a 'gestione_errore()' gestisce in particolare il caso in cui il server è stato chiuso senza usare 'stop'.
            mex_len = ntohl(mex_len_n);  
            ret = recv(sd, buf, mex_len, 0);
            gestione_errore(ret, sd);                                                        
            if (strcmp("AR", buf) == 0){                                                    //se il server ha chiamato "stop", avviso e termino.
                printf("AVVISO: il server è stato arrestato.\nARRESTO IN CORSO...\n");      //(e può ricevere il messaggio "AR" adesso, visto che il td ha indicato in quale tavolo si trova, e quindi il descrittore
                fflush(stdout);                                                             //del socket remoto è contenuto in array_tds[]). 
                exit(0);
            }     
        }
        else{                                                           //PRONTO STDIN
            fgets(buf, BUFLEN, stdin);
            if (sscanf(buf, "%d", &numero) == 1){                       //l'utente inserisce un codice di prenotazione
                break;                                                  //se è un valore numerico, esco dal while
            }
            printf("ERRORE: devi inserire un valore numerico.\n");
            fflush(stdout);
        }    
    }
                                                                
    sprintf(buf, "codice %d", numero);                                  //e lo mando al server
    mex_len = strlen(buf) + 1;                      
    mex_len_n = htonl(mex_len);
    ret = send(sd, &mex_len_n, sizeof(uint32_t), 0);
    gestione_errore(ret, sd);                  
    ret = send(sd, buf, mex_len, 0);
    gestione_errore(ret, sd); 

    ret = recv(sd, buf, 3, 0);
    gestione_errore(ret, sd);
    if (strcmp(buf, "E3") == 0){                    //Se il codice non era valido, avviso e torno a richiedere un altro codice di prenotazione.
        printf("ERRORE: codice di prenotazione non valido.\n");
        fflush(stdout);
        goto b2;
    }
    printf("Codice valido.\n");
    fflush(stdout);

    
    comandi_disponibili();                          //A questo punto, mostro i comandi disponibili ed entro nel vero e proprio while(1)

    while(1){
        memset(comando, 0, sizeof(comando));                    //metto a 0 comando, sennò se si preme semplicemente invio senza scrivere nulla, rimane l'ultima cosa che ci era finita dentro
        read_fds = master;
        select(fdmax + 1, &read_fds, NULL, NULL, NULL);

        if(FD_ISSET(sd, &read_fds)){                            //PRONTO SOCKET DI COMUNICAZIONE
            ret = recv(sd, &mex_len_n, sizeof(uint32_t), 0);
            gestione_errore(ret, sd);                           //questa chiamata a 'gestione_errore()' gestisce in particolare il caso in cui il server è stato chiuso senza usare "stop".
            mex_len = ntohl(mex_len_n);  
            ret = recv(sd, buf, mex_len, 0);
            gestione_errore(ret, sd);                                                        
            if (strcmp("AR", buf) == 0){                                    //se il server ha chiamato "stop", avviso e termino.
                printf("AVVISO: il server è stato arrestato.\nARRESTO IN CORSO...\n");
                fflush(stdout);
                exit(0);
            }                                                           //altrimenti, il server sta avvisando che una comanda è in preparazione/in servizio.
            printf("%s\n", buf);                                        //Mi serve allora solo stampare il messaggio.    
            fflush(stdout);                                                    
        }

        else{                                       //PRONTO STDIN
            fgets(buf, BUFLEN, stdin);              
            sscanf(buf, "%s", comando);             

            if (strcmp(comando, "menu") == 0){
                mex_len = strlen(comando) + 1;
                mex_len_n = htonl(mex_len);
                ret = send(sd, &mex_len_n, sizeof(uint32_t), 0);        //giro il comando al server
                gestione_errore(ret, sd);
                ret = send(sd, comando, mex_len, 0);
                gestione_errore(ret, sd);

                for(;;){                                                //ora ricevo la lista di voci del menu, che mostro a video finché non ricevo un messaggio FIN
                    ret = recv(sd, &mex_len_n, sizeof(uint32_t), 0);
                    gestione_errore(ret, sd); 
                    mex_len = ntohl(mex_len_n);
                    ret = recv(sd, buf, mex_len, 0);
                    gestione_errore(ret, sd);
                    if (strcmp(buf, "FIN") == 0){
                        printf("\n");
                        fflush(stdout);
                        break;
                    }
                    printf("%s\n", buf);
                    fflush(stdout);                                                                   
                }
            }

            else if(strcmp(comando, "comanda") == 0){           
                mex_len = strlen(buf) + 1;
                mex_len_n = htonl(mex_len);
                ret = send(sd, &mex_len_n, sizeof(uint32_t), 0);  //giro tutto il messaggio al server
                gestione_errore(ret, sd);
                ret = send(sd, buf, mex_len, 0);
                gestione_errore(ret, sd);
                                                                      
                ret = recv(sd, buf, 3, 0);      //ora riceverò o un messaggio di errore, o il messaggio di conferma che la comanda è stata accettata.
                gestione_errore(ret, sd);  

                if (strcmp(buf, "E4") == 0){    //se al momento il server ha raggiunto il numero massimo di comande in attesa, avviso.                 
                    printf("ERRORE: è stato raggiunto il numero massimo di comande in attesa. Riprova più tardi.\n");
                    fflush(stdout);
                    continue;
                }
                if (strcmp(buf, "E5") == 0){    //se la comanda non era valida (e cioè, nessuna ordinazione era valida), avviso.
                    printf("ERRORE: comanda non valida.\nLa sintassi è 'comanda {<sigla_piatto_1-quantità_1>...<sigla_piatto_n-quantità_n>', scegliendo piatti presenti nel menù.\n");
                    fflush(stdout);
                    continue;
                }
                if (strcmp(buf, "E6") == 0){    //se il td ha raggiunto il numero massimo di comande inviabili, avviso.
                    printf("ERRORE: hai raggiunto il numero massimo di comande inviabili.\n");
                    fflush(stdout);
                    continue;
                }
                printf("COMANDA RICEVUTA\ncom%d <in attesa>\n", contatore_comande++);    //se sto qui, significa che la comanda è stata accettata, tolte eventuali ordinazioni non valide.
                fflush(stdout);                                                          //avviso, e incremento contatore_comande, che così avrà il numero della prossima comanda che uscirà dal tavolo.         
            }

            else if (strcmp(comando, "conto") == 0){
                mex_len = strlen(comando) + 1;
                mex_len_n = htonl(mex_len);
                ret = send(sd, &mex_len_n, sizeof(uint32_t), 0);        //giro il comando al server
                gestione_errore(ret, sd);
                ret = send(sd, comando, mex_len, 0);
                gestione_errore(ret, sd);
                
                for(;;){                                                //ora ricevo la lista delle voci del conto con il subtotale, e infine il totale, e mostro tutto a video.
                    ret = recv(sd, &mex_len_n, sizeof(uint32_t), 0); 
                    gestione_errore(ret, sd);
                    mex_len = ntohl(mex_len_n);
                    ret = recv(sd, buf, mex_len, 0);
                    gestione_errore(ret, sd);
                    printf("%s\n", buf);
                    fflush(stdout);
                    sscanf(buf, "%s", comando);                 //ad ogni voce ricevuta, metto la prima parola in 'comando'.
                    if (strcmp(comando, "Totale:") == 0){       //se è "Totale:", allora dopo averla stampata, esco dal for (ho finito di stampare il conto)
                        printf("\n");
                        fflush(stdout);
                        break;
                    }                                                                   
                }
                contatore_comande = 1;                          //stampato il conto, rimetto a 1 contatore_comande, visto che poi si ripartirà con un nuovo pasto, e quindi la prima comanda che uscirà sarà 'com1'
                goto b2;                                        //e torno alla schermata in cui viene chiesto un codice di prenotazione.
            }

            else{
                printf("ERRORE: comando non valido.\n");
                fflush(stdout);
            }          
        }
    }
    return 0;
}


    
