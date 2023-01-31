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
    printf("find --> ricerca la disponibilità per una prenotazione \n");
    printf("book --> invia una prenotazione \n");
    printf("esc --> termina il client \n");
    printf("\n");
    fflush(stdout);
}

int data_valida(int GG, int MM, int AA, int HH){       //restituisce 1 se la data GG:MM:AA HH è valida (cioè, se esiste come data), 0 altrimenti.
    time_t rawtime;
    struct tm* timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    int anno_esteso = AA + (timeinfo->tm_year + 1900) - (timeinfo->tm_year + 1900)%100;         //mi trovo l'anno esteso (servirà per vedere se è un anno bisestile). 
                                                                                                //Supposto sia un anno del secolo corrente, ottengo l'anno esteso prendendo l'anno corrente,
                                                                                                //togliendoci le 2 cifre meno significative e mettendo al loro posto le due cifre di AA
                                                                                                //(a tm_year ho sommato 1900 per ottenere l'anno corrente, in quanto conta gli anni partendo dal 1900).

    if (HH < 0 || HH > 24){                                                                     //vedo innanzitutto se l'ora va bene
        return 0;
    }
    
    if (AA >= 0 && AA <= 99){                                                                   //adesso vedo se il resto va bene                                             
        if (MM >= 1 && MM <= 12){                                                                                                                            
            if ((GG >= 1 && GG <= 31) && (MM == 1 || MM == 3 || MM == 5 || MM == 7 || MM == 8 || MM == 10 || MM == 12)){    //mesi da 31 giorni
                return 1; 
            }
            else if ((GG >= 1 && GG <= 30) && (MM == 4 || MM == 6 || MM == 9 || MM == 11)){                                 //mesi da 30 giorni
                return 1;
            } 
            else if ((GG >= 1 && GG <= 28) && (MM == 2)){                                                                   //febbraio
                return 1;
            }
            else if (GG == 29 && MM == 2 && (anno_esteso%400 == 0 || (anno_esteso%4 == 0 && anno_esteso%100 != 0))){        //un anno è bisestile se è divisibile per 4 e, nel caso fosse divisibile pure per 100, deve essere divisibile anche per 400
                return 1;
            }
        }
    }
    return 0;
}

int data_futura(int GG, int MM, int AA, int HH){        //restituisce 1 se la data GG:MM:AA HH è valida e futura, 0 altrimenti.
    time_t rawtime;
    struct tm* timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    if (data_valida(GG, MM, AA, HH)){                   //mi assicuro che sia una data valida
        if (AA > timeinfo->tm_year%100){                //se lo è, vedo prima l'anno: se AA > ultime due cifre dell'anno corrente, allora è una data futura
            return 1;
        }
        else if (AA == timeinfo->tm_year%100 && MM > timeinfo->tm_mon + 1){ //altrimenti, a parità di anno, se MM > mese corrente, allora è una data futura
            return 1;
        }
        else if (AA == timeinfo->tm_year%100 && MM == timeinfo->tm_mon + 1 && GG > timeinfo->tm_mday){  //altrimenti, a parità di anno e mese, se GG > giorno corrente, allora è una data futura
            return 1;
        }
        else if (AA == timeinfo->tm_year%100 && MM == timeinfo->tm_mon + 1 && GG == timeinfo->tm_mday && HH > timeinfo->tm_hour){   //altrimenti, a parità di anno, mese e giorno, se HH > ora corrente, allora è una data futura
            return 1;
        }
    }
    return 0;   
}

void gestione_errore(int ret, int sd){      //sia in caso di errore nella ricezione/invio, che in caso di chiusura del socket remoto, termino.
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
    in_port_t porta = htons(atoi(argv[1]));         //uso "atoi" per convertire in intero la stringa contenente il numero di porta passato dall'utente sul terminale. 
    struct sockaddr_in cl_addr, sv_addr; 

    fd_set master, read_fds;                        //variabili per set
    int fdmax;

    int ret, numero;                                //variabili varie
    int counter = 0;                                //serve per la book, manterrà il valore dell'opzione maggiore che è stata proposta al client. Se = 0, significa che o non si è fatta alcuna find, o l'ultima find non ha dato risultati, e in entrambi i due casi non è possibile fare poi una book.
    uint32_t mex_len;                               //conterrà la lunghezza del messaggio da inviare/ricevere espressa in host order
    uint32_t mex_len_n;                             //conterrà la lunghezza del messaggio da inviare/ricevere espressa in network order

    char comando[BUFLEN];                           //metterò il comando inserito dall'utente
    char cognome[BUFLEN];                           //metterò il cognome inserito dall'utente
    int persone, giorno, mese, anno, ora;           //metterò il numero di persone e il giorno, mese, anno e ora passati dall'utente.         

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
    

    ret = send(sd, "C", 2, 0);                                  //una volta connesso, mando subito il codice di identificazione
    gestione_errore(ret, sd);

    ret = recv(sd, buf, 3, 0);
    gestione_errore(ret, sd);

    if (strcmp(buf, "E1") == 0){                                //se ci sono troppi client connessi, avviso e termino
        printf("ERRORE: al momento ci sono troppi client connessi. Riprova più tardi.\nARRESTO IN CORSO...\n");
        fflush(stdout);
        close(sd);
        exit(1);
    }

    //CREAZIONE SET E ALTRE INIZIALIZZAZIONI
    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    FD_SET(0, &master);
    FD_SET(sd, &master);

    fdmax = sd;
    comandi_disponibili();

    while(1){
        memset(comando, 0, sizeof(comando));        //metto a 0 comando, sennò se si preme semplicemente invio senza scrivere nulla, rimane l'ultima cosa che ci era finita dentro
        read_fds = master;
        select(fdmax + 1, &read_fds, NULL, NULL, NULL);

        if(FD_ISSET(sd, &read_fds)){                //PRONTO SOCKET DI COMUNICAZIONE
            ret = recv(sd, &mex_len_n, sizeof(uint32_t), 0);                                        
            gestione_errore(ret, sd);               //l'unico caso in cui è pronto è quando il server chiude il proprio socket di comunicazione (ret == 0), che viene gestito da questa chiamata a "gestione_errore()"
        }
        
        else{                                       //PRONTO STDIN
            fgets(buf, BUFLEN, stdin);              //uso fgets perché non so a priori quanti campi avrà il comando inserito, quindi metto tutto nel buffer fino al newline. Così non si ha neanche il problema 
            sscanf(buf, "%s", comando);             //di un eventuale buffer overflow, visto che leggo max "BUFLEN -1" caratteri, e alla fine viene messo il fine stringa.
                                                    //leggo poi la prima parola del comando, per capire di che comando si tratta.
            if (strcmp(comando, "find") == 0){
                counter = 0;                                                                                                                //metto inizialmente counter a 0
                if (sscanf(buf, "%s %s %d %d-%d-%d %d", comando, cognome, &persone, &giorno, &mese, &anno, &ora) == 7 && persone >= 1){     //vedo se tutti i campi sono stati messi e messi correttamente.
                    if(data_futura(giorno, mese, anno, ora)){                                                                               //se sì, vedo se si sta prenotando correttamente per una data futura
                        sprintf(buf, "%s %s %d %d-%d-%d %d", comando, cognome, persone, giorno, mese, anno, ora);       //se sono qui, va tutto bene. Allora invio tutto il messaggio così com'è al server.
                        mex_len = strlen(buf) + 1;                                                                      //(e faccio prima questa sprintf, così se è stato scritto altro dopo l'ora, non viene mandato).
                        mex_len_n = htonl(mex_len);                                                                     //Usciti dall'identificazione, il server si aspetta sempre prima la lunghezza e poi il messaggio vero e proprio.
                                                                                                                        //Mando allora in questo formato il messaggio al server.   
                        ret = send(sd, &mex_len_n, sizeof(uint32_t), 0);              
                        gestione_errore(ret, sd); 
                        ret = send(sd, buf, mex_len, 0);                                  
                        gestione_errore(ret, sd);

                        printf("I tavoli disponibili che soddisfano la richiesta sono:\n");
                        fflush(stdout);
                        
                        for(;;){                                                    //ora ricevo e stampo la lista dei tavoli che soddisfano la data e il numero di persone indicati, finché non ricevo un messaggio FIN.
                            ret = recv(sd, &mex_len_n, sizeof(uint32_t), 0);        //counter manterrà il valore dell'opzione maggiore che mi è stata proposta.
                            gestione_errore(ret, sd);
                            mex_len = ntohl(mex_len_n);
                            ret = recv(sd, buf, mex_len, 0);
                            gestione_errore(ret, sd);
                            if (strcmp(buf, "FIN") == 0){
                                printf("\n");
                                fflush(stdout);
                                if (counter == 0){                                  //se nessun tavolo è stato proposto, avviso
                                    printf("Nessun tavolo disponibile che soddisfi la richiesta. Prova con un'altra data o un numero minore di persone.\n");
                                    fflush(stdout);
                                }
                                break;
                            }
                            counter++;
                            printf("%s\n", buf);
                            fflush(stdout);                                                                   
                        }
                    }
                    else{
                        printf("ERRORE: va passata una data valida e di almeno un'ora successiva a quella corrente.\n");
                        fflush(stdout);
                    }                                                                                                                                                                               
                }
                else{
                    printf("ERRORE: Comando 'find' invocato in maniera non corretta.\nLa sintassi è 'find cognome persone GG-MM-AA HH'.\n");
                    fflush(stdout);
                }
            }

            else if(strcmp(comando, "book") == 0){
                if (counter == 0){                                      //la prenotazione è possibile solo se prima si è fatta una find andata a buon fine, quindi con counter > 0 (variabile che non viene toccata da altri)    
                    printf("ERRORE: devi prima chiamare la 'find' per vedere quali opzioni sono disponibili per la data scelta, e poi chiamare la 'book'.\n");
                    fflush(stdout);
                    continue;
                }
                if (sscanf(buf, "%s %d", comando, &numero) == 2){       
                    if (numero >= 1 && numero <= counter){              //mi assicuro che l'utente abbia scelto un'opzione compresa tra quelle presentategli precedentemente dalla find.
                        if (!data_futura(giorno, mese, anno, ora)){     //visto poi che può passare del tempo da quando l'utente ha chiamato la find, ricontrollo che la data di prenotazione sia ancora una data futura.
                            printf("ERRORE: la data indicata non è più una data futura. Richiama la 'find', passando una data valida e di almeno un'ora successiva a quella corrente.\n");
                            fflush(stdout);
                            counter = 0;
                            continue;
                        }
                        sprintf(buf, "%s %s %d %d-%d-%d %d %d", comando, cognome, persone, giorno, mese, anno, ora, numero);      //se sono qui, tutto va bene. Mando allora al server ciò che si era specificato con l'ultima find, e in fondo l'opzione scelta (i valori delle variabili indicate nella sprintf li tocca solo l'ultima find effettuata, quindi tutto ok) 
                        mex_len = strlen(buf) + 1;
                        mex_len_n = htonl(mex_len);                            
                        ret = send(sd, &mex_len_n, sizeof(uint32_t), 0);                  
                        gestione_errore(ret, sd);
                        ret = send(sd, buf, mex_len, 0);                                  
                        gestione_errore(ret, sd);

                        ret = recv(sd, &mex_len_n, sizeof(uint32_t), 0); 
                        gestione_errore(ret, sd);
                        mex_len = ntohl(mex_len_n);
                        ret = recv(sd, buf, mex_len, 0); 
                        gestione_errore(ret, sd);
                        
                        if (strcmp(buf, "E2") == 0){            //se nel frattempo il tavolo è stato prenotato da un altro client, avviso.
                            printf("ERRORE: opzione non più disponibile. Prova a scegliere un'altra opzione o vedere la disponibilità per un'altra data.\n");
                            fflush(stdout);
                            
                        }
                        else{                                   //altrimenti, l'opzione è valida. Stampo allora i dettagli sulla prenotazione inviati dal server e rimetto counter a 0, così che sarà necessario fare un'altra find andata a buon fine prima di fare un'altra prenotazione
                            printf("%s\n", buf);
                            fflush(stdout);                                                       
                            counter = 0;
                        }
                    }
                    else{
                        printf("ERRORE: opzione non valida.\n");
                        fflush(stdout);
                    }
                }
                else{
                    printf("ERRORE: Comando 'book' invocato in maniera non corretta. La sintassi è 'book opz'.\n");
                    fflush(stdout); 
                }
            }

            else if (strcmp(comando, "esc") == 0){
                printf("ARRESTO IN CORSO...\n");
                fflush(stdout);
                close(sd);
                exit(0);
            }

            else{
                printf("ERRORE: Comando non valido.\n");
                fflush(stdout);
            }
            
        }
    }
    return 0;
}
