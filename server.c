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
#define QUEUE 10            //dimensione coda di richieste di connessione
#define MAX_SALA 20         //dimensione massima per descrizione di sala
#define MAX_VOCE 141        //lunghezza massima (+1, comprende il fine stringa) di una voce dei file "menu"/tavoli 

#define MAX_COMANDE_IN_ATTESA 10        //numero massimo (+1, si è usata una coda) di comande in attesa che possono esserci contemporaneamente
#define MAX_COMANDE_PER_TD 6            //numero massimo di comande che un td può far uscire 
#define MAX_KITCHENDEVICES 3            //numero massimo di kd connessi contemporaneamente
#define MAX_CLIENTS 50                  //numero massimo di client connessi contemporaneamente
#define MAX_TAVOLI 20                   //numero massimo di tavoli
#define MAX_ANTIPASTI 10                //numero massimo di antipasti nel menu
#define MAX_PRIMI 15                    //numero massimo di primi nel menu
#define MAX_SECONDI 15                  //numero massimo di secondi nel menu
#define MAX_DOLCI 10                    //numero massimo di dolci nel menu
#define MAX_PIATTI MAX_ANTIPASTI + MAX_PRIMI + MAX_SECONDI + MAX_DOLCI      //numero massimo di voci nel menu

struct client{                                  
    int sd;                                         //sd del socket di comunicazione usato per comunicare con il client
    uint8_t tavoli_proposti[MAX_TAVOLI];            //tavoli_proposti[i] vale 1 se il tavolo Tx+1 soddisfa le richieste di prenotazione indicate dal client.
};

struct client array_clients[MAX_CLIENTS];   //array di client attualmente connessi
int clients = 0;                            //client attualmente connessi.
 

struct piatto{
    char descrizione[MAX_VOCE];                     //contiene la descrizione del piatto
    int costo;                                      //contiene il costo del piatto
};

struct piatto array_piatti[MAX_PIATTI];             //array di piatti: gli indici [0, MAX_ANTIPASTI-1] sono relativi rispettivamente a [A1, A(MAX_ANTIPASTI)]; gli indici [MAX_ANTIPASTI, MAX_ANTIPASTI + MAX_PRIMI-1] sono relativi rispettivamente a [P1, P(MAX_PRIMI)]...
int piatti = 0;                                     //piatti nel menu

int array_kds[MAX_KITCHENDEVICES];                  //contiene i sd dei socket di comunicazione usati per comunicare con i kitchen device (0 se non ce n'è nessuno in una certa posizione)                                                  
int kds = 0;                                        //kitchen devices attivi 

struct comanda{                                     
    int numero;                                     //numero comanda (le comande sono numerate tavolo per tavolo partendo da 1)
    int tav_num;                                    //numero del tavolo da cui proviene la comanda
    int ordinazioni_comanda[MAX_PIATTI];            //ordinazioni_comanda[i] contiene quante quantità sono state ordinate in questa comanda del piatto di indice i in array_piatti
    char stato;                                     //stato della comanda: a (attesa), p (preparazione) o s (servizio)
    int kd_assegnato;                               //sd del socket di comunicazione connesso a quello del kd cui la comanda è stata assegnata (servirà per gestione_errori)
};

struct comanda coda_comande[MAX_COMANDE_IN_ATTESA];           //coda di comande in attesa
int front = 0, back = 0;                                      //serviranno per gestire la coda di comande

struct tavolo{                             
    char sala[MAX_SALA];                                    //contiene la descrizione della sala in cui si trova il tavolo
    int posti;                                              //contiene il numero di posti del tavolo
    char descrizione[MAX_VOCE];                             //contiene la descrizione del tavolo
    struct comanda comande_totali[MAX_COMANDE_PER_TD];      //contiene le comande che il tavolo ha inviato finora nel pasto corrente
    int numero_comande;                                     //contiene il numero di comande uscite finora da questo tavolo nel pasto corrente (e quindi sarà l'indice di .comande_totali[] in cui inserire la prossima comanda)
    int ordinazioni_totali[MAX_PIATTI];                     //ordinazioni_totali[i] contiene quante quantità ha ordinato finora nel pasto corrente del piatto di indice i in array_piatti
};
                                                
struct tavolo array_tavoli[MAX_TAVOLI];         //array di tavoli: array_tavoli[x] è relativo al tavolo Tx+1.            
                                                         
int tavoli = 0;                                 //tavoli nel ristorante
int array_tds[MAX_TAVOLI];                      //array_tds[i] mi dice, attualmente, qual è il sd del socket di comunicazione usato per comunicare con il table device del tavolo di indice i in array_tavoli (0 se non ce n'è nessuno)
int tds = 0;                                    //tds connessi

int codice = 0;                                     //il codice di prenotazione è semplicemente un numero che viene incrementato ad ogni nuova prenotazione

void comandi_disponibili(){
    printf("\n***************************** BENVENUTO *****************************\n");
    printf("Digita un comando:\n");
    printf("\n");
    printf("stat --> mostra lo stato delle comande ai vari tavoli\n");
    printf("stop --> termina il server\n");
    printf("\n");
    fflush(stdout);
}

void controlla_tavoli_liberi(int GG, int MM, int AA, int HH, int p, int id){    //dato un client di indice id in array_clients, vede i tavoli disponibili alla data specificata e con almeno p posti e aggiorna opportunamente array_clients[id].tavoli_proposti[]
    FILE* fd;
    int giorno, mese, anno, ora;
    int i;
    char buf[BUFLEN];
    int numero;
    char lettera;
    fd = fopen("prenotazioni.txt", "r");
    if (fd == NULL){
        perror("ERRORE nella lettura del file 'prenotazioni.txt'");
        printf("ARRESTO IN CORSO...\n");
        fflush(stdout);
        exit(1);
    }

    for(i = 0; i < tavoli; i++){                //inizialmente, suppongo tutti i tavoli presenti nel ristorante come proponibili
        array_clients[id].tavoli_proposti[i] = 1;
    }

    while(fgets(buf, BUFLEN, fd) != NULL){      //ora escludo i tavoli già prenotati per la stessa data
        sscanf(buf, "%c%d %d-%d-%d %d", &lettera, &numero, &giorno, &mese, &anno, &ora);
        if (GG == giorno && MM == mese && AA == anno && HH == ora){
            array_clients[id].tavoli_proposti[numero-1] = 0;
        }
    }
    fclose(fd);

    for(i = 0; i < tavoli; i++){                //dai tavoli non prenotati a quella data, escludo quelli non abbastanza capienti
        if (p > array_tavoli[i].posti){
            array_clients[id].tavoli_proposti[i] = 0;
        }
    }
}
                                            

int aggiungi_prenotazione(int tav_num, int GG, int MM, int AA, int HH, char* cognome){  //aggiunge al file "prenotazioni.txt" la prenotazione per il tavolo tav_num, in data specificata, da parte di "cognome".
    FILE* fd;                                                                           //restituisce 1 se viene aggiunta, 0 altrimenti
    int giorno, mese, anno, ora;
    char buf[BUFLEN];
    int numero;
    char lettera;
    time_t rawtime;
    struct tm* timeinfo;

    fd = fopen("prenotazioni.txt", "r");
    if (fd == NULL){
        perror("ERRORE nella lettura del file 'prenotazioni.txt'");
        printf("ARRESTO IN CORSO...\n");
        fflush(stdout);
        exit(1);
    }                                           //"controlla_tavoli_liberi()"" era stata chiamata dopo che il client aveva invocato "find", mentre questa funzione viene chiamata quando il client invoca la "book".
                                                //visto che da allora passa del tempo e il tavolo nel frattempo può essere stato prenotato per la stessa data da altri client, controllo che sia ancora libero
    while(fgets(buf, BUFLEN, fd) != NULL){      
        sscanf(buf, "%c%d %d-%d-%d %d", &lettera, &numero, &giorno, &mese, &anno, &ora);
        if (numero == tav_num && GG == giorno && MM == mese && AA == anno && HH == ora){            //se è stato prenotato nel frattempo, restituisco 0
            fclose(fd); 
            return 0;
        }
    }
    fclose(fd);

    fd = fopen("prenotazioni.txt", "a");        //se è ancora libero, aggiungo la prenotazione al file e restituisco 1
    if (fd == NULL){
        perror("ERRORE nella scrittura del file 'prenotazioni.txt'");
        printf("ARRESTO IN CORSO...\n");
        fflush(stdout);
        exit(1);
    }
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    fprintf(fd, "T%d %d-%d-%d %d %s %d %d-%d-%d\n", tav_num, GG, MM, AA, HH, cognome, codice, timeinfo->tm_mday, timeinfo->tm_mon+1, (timeinfo->tm_year)%100);    
    codice++;
    fclose(fd);
    return 1;
}

void rimuovi_prenotazione(int numero_linea){    //rimuove la prenotazione dal file "prenotazioni.txt" che si trova alla linea numero_linea
    FILE* src;
    FILE* dst;
    char buf[BUFLEN];
    int counter = 1;                        //terrà il valore della linea cui sono arrivato attualmente.

    src = fopen("prenotazioni.txt", "r");   //apro il file originale in lettura
    dst = fopen("temp.txt", "w");           //apro un altro file in scrittura
    if (src == NULL || dst == NULL){
        printf("ERRORE: impossibile rimuovere una prenotazione effettuata.\n");
        fflush(stdout);
        exit(1);
    }

    while(fgets(buf, BUFLEN, src) != NULL){         //e copio il contenuto del primo file nel secondo,
        if (numero_linea != counter){               //ad eccezione della prenotazione. 
            fputs(buf, dst);     
        }
        counter++; 
    }

    fclose(src);
    fclose(dst);
    remove("prenotazioni.txt");                 //rimuovo poi il vecchio file
    rename("temp.txt", "prenotazioni.txt");     //e rinomino il nuovo in "prenotazioni.txt"
}

int controlla_prenotazione(int codice_prenotazione2, int tav_num){      //controlla se la prenotazione per la data corrente per il tavolo tav_num e con codice di prenotazione indicato
    FILE* fd;                                                           //è presente nel file "prenotazioni.txt". Se sì, la rimuove e restituisce 1; restituisce 0 altrimenti.
    time_t rawtime;
    struct tm* timeinfo;
    int giorno1, mese1, anno1, ora1, giorno2, mese2, anno2, ora2;
    int codice_prenotazione1;
    int counter = 1; //manterrà il numero della linea in cui si trova la prenotazione (servirà poi per rimuoverla)
    char buf[BUFLEN];
    int numero;
    char lettera;
    char cognome[BUFLEN];

    fd = fopen("prenotazioni.txt", "r");
    if (fd == NULL){
        perror("ERRORE nella lettura del file 'prenotazioni.txt'");
        printf("ARRESTO IN CORSO...\n");
        fflush(stdout);
        exit(1);
    }

    time(&rawtime);                         //recupero data e ora attuale
    timeinfo = localtime(&rawtime);
    giorno2 = timeinfo->tm_mday;
    mese2 = timeinfo->tm_mon+1;
    anno2 = (timeinfo->tm_year)%100;
    ora2 = timeinfo->tm_hour;

    while(fgets(buf, BUFLEN, fd) != NULL){      //vedo se trovo la prenotazione 
        sscanf(buf, "%c%d %d-%d-%d %d %s %d", &lettera, &numero, &giorno1, &mese1, &anno1, &ora1, cognome, &codice_prenotazione1);
        if (numero == tav_num && giorno1 == giorno2 && mese1 == mese2 && anno1 == anno2 && ora1 == ora2 && codice_prenotazione1 == codice_prenotazione2){            
            fclose(fd);
            rimuovi_prenotazione(counter);      //Se la trovo, chiudo il file. Chiamo poi rimuovi_prenotazione, che la rimuoverà dal file, e restituisco 1.
            return 1;                           //(la funzione infatti verrà chiamata quando un utente che ha prenotato arriverà al tavolo per mangiare. Allora rimuovo la prenotazione,
        }                                       //altrimenti il file prenotazioni.txt si riempirebbe di prenotazioni passate e già gestite)
        counter++;
    }
    fclose(fd);         //se sono uscito dal for, significa che non l'ho trovata. Chiudo allora il file e restituisco 0.
    return 0;                                   
    
}

int numero_piatto(int id){          //funzione che, dato un indice di array_piatti, restituisce il numero del piatto.
    int numero = id;                //praticamente sottraggo man mano gli offset finché non ottengo un numero < 0, 
    numero -= MAX_ANTIPASTI;        //e a questo punto restituisco il numero precedente all'ultima sottrazione, + 1
    if (numero < 0){                //(+1 perché es A1 ha indice 0, quindi va sommato 1).
        return id+1;                //se invece è l'id di un dolce, dopo aver sottratto MAX_SECONDI, il numero sarà ancora > 0,
    }                               //e sommandogli 1 ottengo il numero del piatto. 
    id = numero;
    numero -= MAX_PRIMI;
    if (numero < 0){
        return id+1;
    }
    id = numero;
    numero -= MAX_SECONDI;
    if (numero < 0){
        return id+1;
    }
    return numero+1;
}

char lettera_piatto(int id){        //funzione che, dato indice di array_piatti, restituisce la lettera del piatto (A, P, S, D)
    int numero = id;                //il funzionamento è simile alla funzione precedente
    numero -= MAX_ANTIPASTI;        
    if (numero < 0){                
        return 'A';                
    }                               
    id = numero;
    numero -= MAX_PRIMI;
    if (numero < 0){
        return 'P';
    }
    id = numero;
    numero -= MAX_SECONDI;
    if (numero < 0){
        return 'S';
    }
    return 'D';
}

int indice_piatto(int num_piatto, char lettera){     //funzione che, dato numero del piatto e lettera, restituisce l'indice di array_piatti (-1 in caso di input errato)
    int indice;
    if (num_piatto <= 0){         //controllo che il numero del piatto sia >= 1
        return -1;
    }

    if (lettera == 'A'){
        indice = num_piatto - 1;               
        if (indice >= MAX_ANTIPASTI || array_piatti[indice].costo == 0){       //non deve sforare negli indici destinati ai primi e deve essere un antipasto esistente
            return -1;
        }
        return indice;
    }
    else if (lettera == 'P'){
        indice = num_piatto + MAX_ANTIPASTI - 1;
        if (indice >= MAX_ANTIPASTI + MAX_PRIMI || array_piatti[indice].costo == 0){ //non deve sforare negli indici destinati ai secondi e deve essere un primo esistente
            return -1;
        }
        return indice;
    }
    else if (lettera == 'S'){
        indice = num_piatto + MAX_ANTIPASTI + MAX_PRIMI - 1;
        if (indice >= MAX_ANTIPASTI + MAX_PRIMI + MAX_SECONDI || array_piatti[indice].costo == 0){ //non deve sforare negli indici destinati ai dolci e deve essere un secondo esistente
            return -1;
        }
        return indice;
    }
    else if (lettera == 'D'){
        indice = num_piatto + MAX_ANTIPASTI + MAX_PRIMI + MAX_SECONDI - 1;   
        if (indice >= MAX_PIATTI || array_piatti[indice].costo == 0){      //non deve sforare oltre il buffer e deve essere un dolce esistente
            return -1;
        }
        return indice;
    }
    else{               //è stata passata una lettera che non è A, P, S, D
        return -1;
    }
}

int coda_piena(){                   //restituisce 1 se la coda di comande in attesa è piena, 0 altrimenti
    if (front == (back + 1)%MAX_COMANDE_IN_ATTESA){
        return 1;
    }
    return 0;
}

int coda_vuota(){                   //restituisce 1 se la coda di comande in attesa è vuota, 0 altrimenti
    if (front == back){
        return 1;
    }
    return 0;
}

//aggiunge in coda_comande la comanda presente in buf nella forma "comanda {<sigla_piatto_1-quantità_1>...<sigla_piatto_n-quantità_n> ..." relativa al tavolo che ha indice_tavolo. 
//Restituisce 1 se tutto va bene, 0 se è stato raggiunto il numero max di comande in attesa, 
//-1 se la comanda non era valida, -2 se il tavolo ha raggiunto il numero massimo di comande mandabili. 
//la funzione inoltre aggiorna opportunamente array_tavoli[indice_tavolo].ordinazioni_totali[] e .comande_totali[]
int aggiungi_comanda(char* buf, int indice_tavolo){
    char lettera;  
    int num_piatto;
    int index_piatto;
    int quantita_ordinazione;           
    char* token;                        
    int check = 0;                            //va a 1 se almeno un'ordinazione era corretta, e quindi la comanda è valida

    if(coda_piena()){                           //raggiunto max numero di comande in attesa
        return 0;
    }
    if(array_tavoli[indice_tavolo].numero_comande == MAX_COMANDE_PER_TD){
        return -2;                              //raggiunto max numero di comande mandabili dal td
    }
    token = strtok(buf, " ");                   //uso strtok per scorrere le varie ordinazioni della comanda
    token = strtok(NULL, " ");                  //la chiamo due volte, così vado alla prima ordinazione

    while (token != NULL){
        if (sscanf(token, "%c%d-%d", &lettera, &num_piatto, &quantita_ordinazione) == 3){
            index_piatto = indice_piatto(num_piatto, lettera);
            if (index_piatto != -1 && quantita_ordinazione > 0){        //dunque num_piatto e lettera sono validi e la quantità di ordinazione pure. Allora l'ordinazione è valida
                coda_comande[back].ordinazioni_comanda[index_piatto] += quantita_ordinazione;   //segno che si è ordinata "quantita_ordinazione" del piatto nella comanda 
                array_tavoli[indice_tavolo].ordinazioni_totali[index_piatto] += quantita_ordinazione; //segno che si è ordinata "quantita_ordinazione" del piatto in ordinazioni_totali del tavolo 
                check = 1;  //e segno che la comanda è valida
            }
        }
        token = strtok(NULL, " ");             //passo poi alla prox ordinazione
    }

    if (check == 0){        //se check == 0, nessuna ordinazione era valida, dunque la comanda non è valida
        return -1;
    }
    coda_comande[back].tav_num = indice_tavolo + 1;                                     //se invece la comanda è valida, metto in tav_num il numero del tavolo cui la comanda è relativa
    coda_comande[back].numero = array_tavoli[indice_tavolo].numero_comande + 1;         //in numero il numero della comanda
    coda_comande[back].stato = 'a';                                                     //in stato scrivo che è in attesa
    array_tavoli[indice_tavolo].comande_totali[array_tavoli[indice_tavolo].numero_comande] = coda_comande[back]; //la stessa comanda la metto in posizione "numero_comande" nell'array comande_totali all'indice relativo al tavolo interessato   
    array_tavoli[indice_tavolo].numero_comande++;                                       //segno che si è aggiunta una comanda uscita da questo tavolo
    back = (back+1)%MAX_COMANDE_IN_ATTESA;                                              //segno che ho aggiunto un elemento nella coda di comande in attesa
    return 1;                                                                           //e restituisco 1
}

//rimuove tutte le comande presenti in coda_comande relative al tavolo che ha indice indice_tavolo in array_tavoli, facendo scorrere le comande nel caso in cui si creassero buchi in mezzo. 
//serve quando il td chiede il conto, che segnala la fine del pasto, e quindi le comande in attesa le tolgo
//(e anche nel caso in cui td viene chiuso in maniera forzata). 
//la funzione inoltre rimuove da ordinazioni_totali tutte le quantità dei piatti ancora in attesa, che così non compariranno nel conto
void rimuovi_comande(int indice_tavolo){
    int numero_tavolo = indice_tavolo + 1;
    int i, j;
    int to_insert = -1;                         //sarà l'indice che conterrà l'indice dell'inizio del buco che si creerà facendo estrazioni dal mezzo. All'inizio, quando non c'è alcun buco, vale -1
    
    if(coda_vuota()){                           //se la coda_comande è vuota, allora non faccio nulla
        return;
    }

    for (i = front; i%MAX_COMANDE_IN_ATTESA != back; i++){    
        if(coda_comande[i%MAX_COMANDE_IN_ATTESA].tav_num == numero_tavolo){   //se c'è una comanda in attesa relativa al tavolo che ha indice indice_tavolo in array_tavoli
            for (j = 0; j < MAX_PIATTI; j++){
                if(coda_comande[i%MAX_COMANDE_IN_ATTESA].ordinazioni_comanda[j] != 0){
                    array_tavoli[indice_tavolo].ordinazioni_totali[j] -= coda_comande[i%MAX_COMANDE_IN_ATTESA].ordinazioni_comanda[j];   //tolgo da .ordinazioni_totali[] per quel tavolo le ordinazioni che ancora non sono state preparate, che così non compariranno nel conto
                }
            }
            memset(&coda_comande[i%MAX_COMANDE_IN_ATTESA], 0, sizeof(coda_comande[i%MAX_COMANDE_IN_ATTESA]));   //e azzero tutti i campi della comanda

            if (i%MAX_COMANDE_IN_ATTESA == front){                      //se è un'estrazione dalla testa
                front = (front + 1)%MAX_COMANDE_IN_ATTESA;              //allora aggiorno front semplicemente
            }

            else{                                                                           //altrimenti, è un'estrazione dal mezzo, e quindi creo un buco (gestisce anche caso in cui è estrazione dalla fine della coda)
                to_insert = (to_insert == -1) ? (i%MAX_COMANDE_IN_ATTESA) : to_insert;      //se è il primo buco che creo, allora metto in to_insert l'indice dell'inizio del buco (e cioè, l'indice corrente della coda, che attenzione è i%MAX_COMANDE_IN_ATTESA)
                                                                                            //altrimenti, significa che ho allargato un buco che sta già, e to_insert quindi già indica l'indice dell'inizio del buco                                                                                     
            }                                                   
        }

        else{                                                           //se invece è una comanda in attesa non relativa al tavolo che ha chiesto il conto
            if (to_insert != -1){                                                       //se c'è un buco, 
                coda_comande[to_insert] = coda_comande[i%MAX_COMANDE_IN_ATTESA];        //allora sposto questa comanda all'inizio del buco
                memset(&coda_comande[i%MAX_COMANDE_IN_ATTESA], 0, sizeof(coda_comande[i%MAX_COMANDE_IN_ATTESA]));   //azzero i campi della vecchia posizione
                to_insert = (to_insert + 1) % MAX_COMANDE_IN_ATTESA;                                                //e faccio avanzare l'indice che indica l'inizio del buco
            }
            //se invece non c'è alcun buco, non faccio nulla
        }
    }

    back = (to_insert != -1) ? to_insert : back;    //usciti dal for, to_insert avrà come valore -1 se non è mai stata fatta alcun estrazione dal mezzo (e quindi back avrà il suo valore corretto),
                                                    //o l'indice dell'elemento successivo all'ultimo se sono state fatte estrazioni dal mezzo (e quindi il valore corretto di back)
                                                    //allora faccio questo assegnamento condizionale
}                                                   

void stampa_comande_tavolo(int id){                 //stampa le comande uscite dal tavolo di indice id in array_tavoli
    int j, k;                                   
    for (j = 0; j < array_tavoli[id].numero_comande; j++){
        printf("com%d T%d ", array_tavoli[id].comande_totali[j].numero, id+1);
        fflush(stdout);
        if (array_tavoli[id].comande_totali[j].stato == 'a'){
            printf("<in attesa>\n");
            fflush(stdout);
        }
        else if(array_tavoli[id].comande_totali[j].stato == 'p'){
            printf("<in preparazione>\n");
            fflush(stdout);
        }
        else{
            printf("<in servizio>\n");
            fflush(stdout);
        }
        for (k = 0; k < MAX_PIATTI; k++){
            if(array_tavoli[id].comande_totali[j].ordinazioni_comanda[k] != 0){
                printf("%c%d %d\n", lettera_piatto(k), numero_piatto(k), array_tavoli[id].comande_totali[j].ordinazioni_comanda[k]);
                fflush(stdout);
            }    
        }
    } 
}

void stampa_comande_particolari_tavolo(int id, char stato){                 //stampa le comande uscite dal tavolo di indice id in array_tavoli e che si trovano in stato "stato" 
    int j, k;
    for (j = 0; j < array_tavoli[id].numero_comande; j++){
        if (array_tavoli[id].comande_totali[j].stato == stato){
            printf("com%d T%d\n", array_tavoli[id].comande_totali[j].numero, id+1);
            fflush(stdout);
            for (k = 0; k < MAX_PIATTI; k++){
                if(array_tavoli[id].comande_totali[j].ordinazioni_comanda[k] != 0){
                    printf("%c%d %d\n", lettera_piatto(k), numero_piatto(k), array_tavoli[id].comande_totali[j].ordinazioni_comanda[k]);
                    fflush(stdout);
                }    
            }
        }          
    } 
}

int arresto_possibile(){        //restituisce 1 se è possibile arrestare il server (e cioè se non si hanno comande in attesa o in preparazione), 0 altrimenti
    int i, j;
    for (i = 0; i < tavoli; i++){
        for (j = 0; j < array_tavoli[i].numero_comande; j++){
            if (array_tavoli[i].comande_totali[j].stato == 'a' || array_tavoli[i].comande_totali[j].stato == 'p'){
                return 0;
            }
        }
    }
    return 1;
}

//gestisce recv/send, va passato il valore di ret, un puntatore al set master e il sd interessato. restituisce 1 se ha fatto cose, 0 se non ha fatto niente
int gestione_errore(int ret, fd_set* master, int sd){        
    int i, j, k;
    char buf[BUFLEN];
    uint32_t mex_len;
    uint32_t mex_len_n;
    if (ret == 0 || ret == -1){      //sia in caso di errore nella ricezione/invio, che in caso di chiusura del socket remoto, chiudo il socket locale
        if (ret == -1){              //nel caso in cui c'è stato un errore, lo stampo a video
            perror("ERRORE nella comunicazione con un socket remoto");
        } 
        for (i = 0; i < MAX_CLIENTS; i++){      //SE SI VOLEVA COMUNICARE CON UN CLIENT
            if (array_clients[i].sd == sd){
                memset(&array_clients[i], 0, sizeof(array_clients[i])); //metto a 0 il vecchio posto occupato in array_clients
                clients--;                                              //decremento clients
                close(sd);                                              //chiudo il socket
                FD_CLR(sd, master);                                     //e tolgo il descrittore di socket dal set master.
                printf("AVVISO: è stata terminata la comunicazione con un cli.\n");
                fflush(stdout);
                return 1;
            }
        }

        for (i = 0; i < MAX_KITCHENDEVICES; i++){       //SE SI VOLEVA COMUNICARE CON UN KD
            if(array_kds[i] == sd){     
                array_kds[i] = 0;               //metto a 0 il vecchio posto
                kds--;                          //decremento kds
                close(sd);                      //chiudo il socket
                FD_CLR(sd, master);             //tolgo il descrittore di socket dal set master
                for (j = 0; j < tavoli; j++){
                    for (k = 0; k < array_tavoli[j].numero_comande; k++){               //se è stata/verrà chiusa la comunicazione con un kd che aveva in gestione alcune comande,
                        if (array_tavoli[j].comande_totali[k].kd_assegnato == sd){      //queste le segno come "in servizio". Serve altrimenti rimarrebbero sempre nello stato "in preparazione",    
                            array_tavoli[j].comande_totali[k].stato = 's';              //e quindi il server non potrebbe più terminare con "stop" (si assume poi che queste comande vengano comunque
                            array_tavoli[j].comande_totali[k].kd_assegnato = 0;         //portate a termine anche se il kd che le aveva in gestione è stato/verrà chiuso; l'unico lato negativo è che,    
                        }                                                               //poiché il kd che le aveva in gestione è stato/verrà chiuso, i td interessati non potranno essere avvisati di quando 
                    }                                                                   //le loro comande passeranno effettivamente allo stato "in servizio".
                }                                                                       //segno poi che kd_assegnato = 0, visto che il vecchio kd è stato/verrà chiuso.
                printf("AVVISO: è stata terminata la comunicazione con un kd.\n");
                fflush(stdout);
                return 1;   
            }
        }

        for (i = 0; i < MAX_TAVOLI; i++){       //SE SI VOLEVA COMUNICARE CON UN TD
            if (array_tds[i] == sd){
                array_tds[i] = 0;               //metto a 0 il vecchio posto
                tds--;                          //decremento tds
                close(sd);                      //chiudo il socket
                FD_CLR(sd, master);              //tolgo il descrittore di socket dal set master
                rimuovi_comande(i);                 //tolgo tutte le comande in attesa provenienti da questo tavolo
                sprintf(buf, "rimuovi T%d", i+1);   //e avviso i kd di smettere di preparare comande provenienti da questo tavolo
                mex_len = strlen(buf)+1;
                mex_len_n = htonl(mex_len);
                for(j = 0; j < MAX_KITCHENDEVICES; j++){
                    if (array_kds[j] != 0){
                        ret = send(array_kds[j], &mex_len_n, sizeof(uint32_t), 0);
                        if (gestione_errore(ret, master, array_kds[j])){              //la richiamo nel caso in cui dovesse andare male la comunicazione con il kd
                            continue;                                                 //e gestisco il kd, dopodiché continuo ad avvisare tranquillamente gli altri kd
                        }
                        ret = send(array_kds[j], buf, mex_len, 0);
                        if (gestione_errore(ret, master, array_kds[j])){                     
                            continue;
                        }
                    }
                }
                array_tavoli[i].numero_comande = 0;     //azzero poi tutte le info del pasto corrente riguardanti il tavolo
                memset(&array_tavoli[i].ordinazioni_totali, 0, sizeof(array_tavoli[i].ordinazioni_totali));
                memset(&array_tavoli[i].comande_totali, 0, sizeof(array_tavoli[i].comande_totali));
                printf("AVVISO: è stata terminata la comunicazione con un td.\n");
                fflush(stdout);             
                return 1;   //e infine restituisco 1
            }
        }
        tds--;                          //se il sd non sta in nessun array, l'unico caso è che è un table device che ancora non ha detto a quale tavolo è relativo.
        close(sd);                      //decremento allora tds (visto che per lui l'avevo decrementato precedentemente), chiudo il socket, lo tolgo dal set
        FD_CLR(sd, master);
        printf("AVVISO: è stata terminata la comunicazione con un td.\n");
        fflush(stdout);
        return 1;                       //e restituisco 1
    }
    return 0;       //se sono qui, ret != 0 && ret != 1. Non va fatto nulla, e quindi restituisco 0 
}

int main(int argc, char *argv[]){
    //VARIABILI                                                                                         
    char buf[BUFLEN];                               //variabili per socket
    int sd, new_sd;                                       
    in_port_t porta = htons(atoi(argv[1]));     
    struct sockaddr_in sv_addr, cl_addr;
    socklen_t len;          
    
    fd_set master, read_fds;                        //variabili per set
    int fdmax;

    FILE* fd;                                       //variabili per lettura file      

    int i, j, ret, counter, offset;                         //variabili varie
    int numero;
    char lettera;
    int numero_comanda;
    int numero_tavolo;
    int id;                                                 
    uint32_t mex_len;                                       //conterrà la lunghezza del messaggio da inviare/ricevere espressa in host order
    uint32_t mex_len_n;                                     //conterrà la lunghezza del messaggio da inviare/ricevere espressa in network order
    char comando[BUFLEN];                                   //metterò il comando inserito dall'utente o arrivato da altri dispositivi                             
    char cognome[BUFLEN];                                   //metterò il cognome passato dal client
    int persone, ora, giorno, mese, anno;                   //metterò il numero di persone e l'ora, giorno, mese e anno passati dal client 
    int conto;                                              //metterò il conto quando td vorrà calcolarlo
    int subtotale;                                          //metterò i vari subtotali nel calcolo del conto
    int err;                                                //userò per vedere se sono uscito da un ciclo per un errore

    //RECUPERO INFO SU MENU
    memset(array_piatti, 0, sizeof(array_piatti));

    fd = fopen("menu", "r");
    if (fd == NULL){
        perror("ERRORE nella lettura del file 'menu'");
        printf("ARRESTO IN CORSO...\n");
        fflush(stdout);
        exit(1);
    }

    for(i = 0; i < MAX_PIATTI; i++){
        if (fgets(buf, MAX_VOCE, fd) == NULL){
            break;
        }                                               //gli indici [0, MAX_ANTIPASTI-1] sono relativi rispettivamente a [A1, A(MAX_ANTIPASTI)]

        offset = 0;
        sscanf(buf, "%c%d", &lettera, &numero);
        if (lettera == 'P'){                            //gli indici [MAX_ANTIPASTI, MAX_ANTIPASTI + MAX_PRIMI -1] sono relativi rispettivamente a [P1, P(MAX_PRIMI)]
            offset = MAX_ANTIPASTI;
        }
        else if (lettera == 'S'){                       //gli indici [MAX_ANTIPASTI + MAX_PRIMI, MAX_ANTIPASTI + MAX_PRIMI + MAX_SECONDI -1] sono relativi rispettivamente a [S1, S(MAX_SECONDI)]
            offset = MAX_ANTIPASTI + MAX_PRIMI;
        }
        else if (lettera == 'D'){                       //gli indici [MAX_ANTIPASTI + MAX_PRIMI + MAX_SECONDI, MAX_ANTIPASTI + MAX_PRIMI + MAX_SECONDI + MAX_DOLCI -1] sono relativi rispettivamente a [D1, D(MAX_DOLCI)]
            offset = MAX_ANTIPASTI + MAX_PRIMI + MAX_SECONDI;
        }
        if (sscanf(buf, "%c%d - %d - %[^\n]", &lettera, &numero, &array_piatti[numero - 1 + offset].costo, array_piatti[numero - 1 + offset].descrizione) == 4){  //questo gestisce in particolare il caso in cui ci fossero linee vuote nel file, che così non verranno contate
            piatti++;                                                                                                                                             
        }                                                                                                                                                         
    }

    fclose(fd);

    //RECUPERO INFO SU TAVOLI
    memset(array_tavoli, 0, sizeof(array_tavoli));

    fd = fopen("tavoli", "r");
    if (fd == NULL){
        perror("ERRORE nella lettura del file 'tavoli'");
        printf("ARRESTO IN CORSO...\n");
        fflush(stdout);
        exit(1);
    }

    for(i = 0; i < MAX_TAVOLI; i++){                            //l'indice x è relativo al tavolo Tx+1   
        if (fgets(buf, MAX_VOCE, fd) == NULL){
            break;
        }
        
        if (sscanf(buf, "%c%d - %s - %d - %[^\n]", &lettera, &numero, array_tavoli[i].sala, &array_tavoli[i].posti, array_tavoli[i].descrizione) == 5){  //questo gestisce in particolare il caso in cui ci fossero linee vuote nel file, che così non verranno contate
            tavoli++;                                                                                                                                    
        }
    }

    fclose(fd);

    //INIZIALIZZAZIONE ARRAY E TABELLA
    memset(array_kds, 0, sizeof(array_kds));
    memset(array_tds, 0, sizeof(array_tds));
    memset(array_clients, 0, sizeof(array_clients));
    memset(coda_comande, 0, sizeof(coda_comande));

    //CREAZIONE SOCKET DI ASCOLTO
    memset(&sv_addr, 0, sizeof(sv_addr));
    memset(&cl_addr, 0, sizeof(cl_addr));

    sd = socket(AF_INET, SOCK_STREAM, 0);
    sv_addr.sin_family = AF_INET;
    sv_addr.sin_port = porta;
    sv_addr.sin_addr.s_addr = INADDR_ANY; 

    ret = bind(sd, (struct sockaddr*)&sv_addr, sizeof(sv_addr));
    if (ret == -1){
        perror("ERRORE nella bind()");
        printf("ARRESTO IN CORSO...\n");
        fflush(stdout);
        exit(1);
    }

    ret = listen(sd, QUEUE);
    if (ret == -1){
        perror("ERRORE nella listen()");
        printf("ARRESTO IN CORSO...\n");
        fflush(stdout);
        exit(1);
    }

    //CREAZIONE SET
    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    FD_SET(sd, &master);
    FD_SET(0, &master);
    fdmax = sd;

    comandi_disponibili();

    while(1){
        memset(comando, 0, sizeof(comando));    //metto a 0 comando, sennò se si preme semplicemente invio senza scrivere nulla, rimane l'ultima cosa ci era finita dentro
        read_fds = master;
        select(fdmax + 1, &read_fds, NULL, NULL, NULL);

        for (i = 0; i <= fdmax; i++){
            if(FD_ISSET(i, &read_fds)){
                if (i == 0){                              //PRONTO STDIN
                    fgets(buf, BUFLEN, stdin);            
                    sscanf(buf, "%s", comando);           
                    if (strcmp(comando, "stat") == 0){
                        if(sscanf(buf, "stat T%d", &numero_tavolo) == 1){               //l'utente ha chiesto di vedere tutte le comande relative al tavolo numero_tavolo
                            if (numero_tavolo < 1 || numero_tavolo > tavoli){           //vedo il numero del tavolo è valido
                                printf("ERRORE: numero del tavolo non valido.\n");
                                fflush(stdout);
                                continue;
                            }
                            stampa_comande_tavolo(numero_tavolo-1);                     //se sì, stampo quanto richiesto
                        }
                        
                        else if(sscanf(buf, "stat %c", &lettera) == 1){                 //l'utente ha chiesto di vedere tutte le comande nello stato "lettera"
                            if (lettera != 'a' && lettera != 'p' && lettera != 's'){    //vedo se lo stato indicato è corretto
                                printf("ERRORE: stato non valido.\n");
                                fflush(stdout);
                                continue;
                            }
                            for (j = 0; j < tavoli; j++){
                                stampa_comande_particolari_tavolo(j, lettera);          //se sì, stampo quanto richiesto
                            }
                        }

                        else{                                                           //l'utente ha usato il comando in maniera non valida
                            printf("ERRORE: comando 'stat' invocato in maniera non corretta.\nLa sintassi è 'stat Tx' o 'stat a|p|s'.\n");
                            fflush(stdout);
                        }
                    }

                    else if (strcmp(comando, "stop") == 0){
                        if(!arresto_possibile()){                                       //controllo che sia possibile arrestare il server
                            printf("ERRORE: non è possibile arrestare il server in quanto ci sono comande in attesa e/o in preparazione.\n");
                            fflush(stdout);
                        }
                        else{                                       //se sono qui, è possibile.                  
                            printf("ARRESTO IN CORSO...\n");        //avviso allora i tds e i kds connessi, mandando un messaggio "AR".
                            fflush(stdout);
                            for(j = 0; j < MAX_TAVOLI; j++){        //prima avviso i tds connessi
                                if(array_tds[j] != 0){             
                                    mex_len = 3;
                                    mex_len_n = htonl(mex_len);
                                    send(array_tds[j], &mex_len_n, sizeof(uint32_t), 0);    //non serve chiamare gestione_errore(), perché anche se la send dovesse fallire, il server terminerà, dunque verranno chiusi tutti i socket, 
                                    send(array_tds[j], "AR", mex_len, 0);                   //e quindi i tds si accorgeranno che il socket remoto è stato chiuso e termineranno a loro volta
                                }
                            }
                            for (j = 0; j < MAX_KITCHENDEVICES; j++){   //ora avviso kds connessi
                                if(array_kds[j] != 0){                  
                                    mex_len = 3;
                                    mex_len_n = htonl(mex_len);
                                    send(array_kds[j], &mex_len_n, sizeof(uint32_t), 0);    //per lo stesso motivo di prima, non serve chiamare gestione_errore()
                                    send(array_kds[j], "AR", mex_len, 0);
                                }
                            }
                            exit(0);                                //una volta avvisati tutti, termino
                        }   
                    }

                    else{
                        printf("ERRORE: comando non valido.\n");
                        fflush(stdout);
                    }

                }

                else if(i == sd){                                                       //PRONTO DESCRITTORE SOCKET DI ASCOLTO
                    len = sizeof(cl_addr);
                    new_sd = accept(sd, (struct sockaddr*)&cl_addr, &len);
                    if (new_sd == -1){
                        perror("ERRORE nell'accept()");
                        continue;
                    }  

                    ret = recv(new_sd, buf, 2, 0);              //client, kitchen_device o table_device manderanno subito un codice da 2 caratteri (C, K o T + il fine stringa) per identificarsi
                    if (ret == 0 || ret == -1){                 //non chiamo la gestione_errore, visto che devo solo chiudere il socket, e inoltre la funzione suppone che il sd sia stato già messo nell'apposito array ed è stata incrementata l'opportuna variabile, cosa che ancora non è stata fatta.
                        printf("ERRORE nell'identificazione di un nuovo socket remoto.\n");
                        fflush(stdout);
                        close(new_sd); 
                        continue;
                    }
                                                                                               

                    if (buf[0] == 'K'){                             //è un kitchen device
                        if (kds == MAX_KITCHENDEVICES){             //se si è raggiunto il numero max, avviso il kitchen device e chiudo il socket
                            send(new_sd, "E1", 3, 0);               //non chiamo gestione_errore, visto che, anche se la send fallisse, comunque il socket lo devo chiudere (e inoltre non posso chiamarla, perché non siamo nelle condizioni scritte nel commento sopra)
                            close(new_sd);
                            continue;
                        }
                        for(j = 0; j < MAX_KITCHENDEVICES; j++){            //se invece non si è raggiunto il numero max, metto il sd nel primo indice libero di array_kds
                            if (array_kds[j] == 0){
                                array_kds[j] = new_sd; 
                                break;           
                            }
                        }                         
                        kds++;                                              //incremento kds                                           
                        ret = send(new_sd, "OK", 3, 0);                     //e mando l'OK al kd.
                        if (gestione_errore(ret, &master, new_sd)){         //ora la chiamo, visto che siamo nelle condizioni per chiamarla
                            continue;
                        }
                        printf("AVVISO: si è connesso un nuovo kd.\n");
                        fflush(stdout);
                    }

                    else if (buf[0] == 'T'){                                //è un table device
                        if (tds == tavoli){                                 //se si è raggiunto il numero max, avviso il td e chiudo il socket
                            send(new_sd, "E1", 3, 0);                       //non chiamo la gestione_errore, visto che ...
                            close(new_sd);                                  
                            continue;
                        }
                        tds++;                                          //se invece non si è raggiunto il numero max, incremento tds
                        ret = send(new_sd, "OK", 3, 0);                 //e mando l'OK al td. Poi successivamente il td mi dirà di quale tavolo lui è effettivamente il td.
                        if (gestione_errore(ret, &master, new_sd)){     //ora la chiamo, visto che ...
                            continue;
                        }
                        printf("AVVISO: si è connesso un nuovo td.\n");
                        fflush(stdout); 
                    }

                    else{                                       //è un client                                
                        if (clients == MAX_CLIENTS){            //se è stato raggiunto il max numero di client, lo avviso
                            send(new_sd, "E1", 3, 0);           //non chiamo la gestione_errore, visto che ...
                            close(new_sd);
                            continue;
                        }
                        for (j = 0; j < MAX_CLIENTS; j++){      //se invece non si è raggiunto il numero max, metto il sd nel primo indice libero di array_clients
                            if(array_clients[j].sd == 0){        
                                array_clients[j].sd = new_sd;
                                break;
                            }
                        }
                        clients++;                              //incremento clients
                        ret = send(new_sd, "OK", 3, 0);         //e mando l'OK al client.
                        if (gestione_errore(ret, &master, new_sd)){      //ora la chiamo, visto che ...
                            continue;
                        }
                        printf("AVVISO: si è connesso un nuovo cli.\n");
                        fflush(stdout);
                    } 

                    FD_SET(new_sd, &master);             //se sono qui, identificazione è andata bene. Allora adesso metto sd nel set.
                    if(new_sd > fdmax){
                        fdmax = new_sd;
                    }                                    
                }

                else{                                                                   //PRONTO SOCKET DI COMUNICAZIONE        
                    ret = recv(i, &mex_len_n, sizeof(uint32_t), 0);                     //il server si aspetta sempre prima la lunghezza del messaggio e poi il messaggio vero e proprio
                    if (gestione_errore(ret, &master, i)){                              
                        continue;
                    }
                    mex_len = ntohl(mex_len_n);                                         
                    ret = recv(i, buf, mex_len, 0);
                    if (gestione_errore(ret, &master, i)){                              
                        continue;
                    }

                    sscanf(buf, "%s", comando);                                 //mi salvo la prima parola in "comando", per capire così chi mi ha contattato e cosa vuole
                    if (strcmp(comando, "find") == 0){                          //dunque è un client ed ha usato il comando "find"
                        for(j = 0; j < MAX_CLIENTS; j++){                       //trovo innanzitutto l'indice del client in array_clients
                            if (array_clients[j].sd == i){
                                id = j;
                                break;
                            }
                        }
                        sscanf(buf, "%s %s %d %d-%d-%d %d", comando, cognome, &persone, &giorno, &mese, &anno, &ora);   //recupero i vari campi dal messaggio         
                        controlla_tavoli_liberi(giorno, mese, anno, ora, persone, id);                                  //controllo se c'è un tavolo libero che rispetti le condizioni specificate dal client

                        counter = 1;                        //uso counter per numerare, partendo da 1, le opzioni che invierò al client. 
                        err = 0;                            //inizialmente, err è a 0.
                        for (j = 0; j < tavoli; j++){       //ora inizio a mandare al client la lista dei tavoli che soddisfano le condizioni specificate, e alla fine mando il messaggio "FIN"
                            if (array_clients[id].tavoli_proposti[j] == 1){     //in particolare, mando tutti quelli che hanno .tavoli_proposti[] == 1, che saranno così dopo aver chiamato la "controlla_tavoli_liberi()"
                                numero = j+1;
                                sprintf(buf, "%d) T%d %s %s", counter, numero, array_tavoli[j].sala, array_tavoli[j].descrizione);
                                counter++;
                                mex_len = strlen(buf) + 1;
                                mex_len_n = htonl(mex_len);
                                ret = send(i, &mex_len_n, sizeof(uint32_t), 0);
                                if (gestione_errore(ret, &master, i)){                              //in caso di errore, una volta gestito, metto err a 1 ed esco innanzitutto da questo for
                                    err = 1;
                                    break;
                                }
                                ret = send(i, buf, mex_len, 0);
                                if (gestione_errore(ret, &master, i)){                              
                                    err = 1;
                                    break;
                                }      
                            }        
                        }
                        if (err == 1){  //se sono uscito dal ciclo per un errore, passo alla prossima iterazione del for esterno.
                            continue;   
                        }
                                                                    
                        mex_len = 4;                                    //se invece tutto è andato bene, mando il messaggio di fine
                        mex_len_n = htonl(mex_len); 
                        ret = send(i, &mex_len_n, sizeof(uint32_t), 0);
                        if (gestione_errore(ret, &master, i)){                              
                            continue;
                        }
                        ret = send(i, "FIN", mex_len, 0);
                        if (gestione_errore(ret, &master, i)){                             
                            continue;
                        }                          
                    } 

                    else if(strcmp(comando, "book") == 0){              //dunque è client e ha usato il comando "book"
                        for(j = 0; j < MAX_CLIENTS; j++){               //trovo innanzitutto l'indice del client in array_clients
                            if (array_clients[j].sd == i){
                                id = j;
                                break;
                            }
                        }
                        sscanf(buf, "%s %s %d %d-%d-%d %d %d", comando, cognome, &persone, &giorno, &mese, &anno, &ora, &numero); //metto ora in "numero" il numero dell'opzione scelta dal client e ricavo tutti gli altri dettagli della prenotazione
                        counter = 1;                                            //metto counter a 1, che incrementerò ogni volta che trovo un tavolo proposto. In questo modo, l'opzione scelta dal client sarà il tavolo proposto cui si arriva quando counter == numero.
                        for (j = 0; j < tavoli; j++){
                            if (array_clients[id].tavoli_proposti[j]){
                                if (counter == numero){                         //j+1 sarà così il numero del tavolo che il client vuole prenotare
                                    break;
                                }
                                counter++;
                            }
                        }   
                        if (aggiungi_prenotazione(j+1, giorno, mese, anno, ora, cognome)){            //vado allora ad aggiungere la prenotazione al file "prenotazioni.txt", con questa funzione che controlla anche se nel frattempo il tavolo è stato già prenotato per la stessa data
                            sprintf(buf, "PRENOTAZIONE EFFETTUATA: TAVOLO T%d, %s, CODICE: %d", j+1, array_tavoli[j].sala, codice-1);   //se tutto va bene, avviso il client, dicendogli che tavolo ha prenotato, in quale sala si trova e il numero di prenotazione
                            mex_len = strlen(buf) + 1;                                            
                            mex_len_n = htonl(mex_len);
                            ret = send(i, &mex_len_n, sizeof(uint32_t), 0);
                            if (gestione_errore(ret, &master, i)){                              
                                continue;
                            }
                            ret = send(i, buf, mex_len, 0);
                            if (gestione_errore(ret, &master, i)){                              
                                continue;
                            }
                            printf("AVVISO: un cli ha effettuato una nuova prenotazione.\n");
                            fflush(stdout);
                        }
                        else{                                               //se invece l'opzione non era più valida (e cioè, il tavolo nel frattempo è stato prenotato per la stessa data), avviso il client 
                            mex_len = 3;                                           
                            mex_len_n = htonl(mex_len);
                            ret = send(i, &mex_len_n, sizeof(uint32_t), 0);
                            if (gestione_errore(ret, &master, i)){                              
                                continue;
                            }
                            ret = send(i, "E2", mex_len, 0);
                            if (gestione_errore(ret, &master, i)){                              
                                continue;
                            }
                        }
                    }

                    else if (strcmp(comando, "tavolo") == 0){                               //dunque è un td e ha mandato l'indice del tavolo in array_tavoli di cui è il td. 
                        sscanf(buf, "%s %d", comando, &numero);                             //metto questo valore in "numero"
                        if (numero < 0 || numero >= tavoli || array_tds[numero] != 0){      //se non è valido (cioè se a quell'indice non corrisponde alcun tavolo presente nel ristorante  
                            ret = send(i, "E2", 3, 0);                                      //o c'è già un td attivo per quel tavolo), avviso il td 
                            if (gestione_errore(ret, &master, i)){                              
                                continue;
                            }     
                            continue;               
                        }
                        printf("AVVISO: un td è stato correttamente associato.\n");     
                        fflush(stdout);                                         //se è valido, metto il sd del socket di comunicazione usato per comunicare con tale td
                        array_tds[numero] = i;                                  //nell'array_tds in posizione "numero" e mando OK.
                        ret = send(i, "OK", 3, 0);
                        if (gestione_errore(ret, &master, i)){                              
                            continue;
                        }
                    }

                    else if (strcmp(comando, "codice") == 0){                   //dunque è un td e un utente ha inserito il codice di prenotazione
                        for(j = 0; j < tavoli; j++){                            //trovo innanzitutto l'indice del td in array_tds
                            if (array_tds[j] == i){                             //j è l'indice dell'array, dunque è il tavolo ha numero j+1               
                                break;                                          
                            }
                        }
                        sscanf(buf, "%s %d", comando, &numero);                 //recupero il codice della prenotazione                        
                        if(!controlla_prenotazione(numero, j+1)){               //se non esiste alcuna prenotazione per il tavolo T(j+1) alla data corrente con quel codice, avviso
                            ret = send(i, "E3", 3, 0);
                            if (gestione_errore(ret, &master, i)){                              
                                continue;
                            }
                            continue;
                        }
                        ret = send(i, "OK", 3, 0);                            //altrimenti, mando l'OK
                        if (gestione_errore(ret, &master, i)){                              
                            continue;
                        }
                        printf("AVVISO: è arrivato un cliente al tavolo T%d.\n", j+1);
                        fflush(stdout);
                    }

                    else if(strcmp(comando, "menu") == 0){                      //dunque è un td e ha chiesto il menu 
                        lettera = 'A';                                          //servirà per tenere conto di cosa va mostrato dopo
                        err = 0;                                                //metto err a 0
                        for (j = 0; j < MAX_PIATTI; j++){                       //ora inizio a mandare la lista delle voci del menu, e alla fine mando il messaggio "FIN"
                            if (array_piatti[j].costo == 0){                    
                                if (lettera == 'A'){                            //finito di mostrare gli antipasti, passo ai primi;      
                                    j = MAX_ANTIPASTI-1;    
                                    lettera = 'P';
                                    continue;
                                }
                                else if (lettera == 'P'){                       //finito di mostrare i primi, passo ai secondi;
                                    j = MAX_ANTIPASTI + MAX_PRIMI -1;
                                    lettera = 'S';
                                    continue;
                                }
                                else if (lettera == 'S'){                       //finito di mostrare i secondi, passo ai dolci
                                    j = MAX_ANTIPASTI + MAX_PRIMI + MAX_SECONDI -1;
                                    lettera = 'D';
                                    continue;
                                }
                                else{                                           //finiti i dolci, esco dal for
                                    break;
                                }
                            }       
                            sprintf(buf, "%c%d - %s %d", lettera, numero_piatto(j), array_piatti[j].descrizione, array_piatti[j].costo);    //altrimenti, mando il piatto cui sono arrivato in array_piatti
                            mex_len = strlen(buf) + 1;
                            mex_len_n = htonl(mex_len);
                            ret = send(i, &mex_len_n, sizeof(uint32_t), 0);
                            if (gestione_errore(ret, &master, i)){      //in caso di errore, una volta gestito, metto err a 1 ed esco innanzitutto da questo for                         
                                err = 1;
                                break;
                            }
                            ret = send(i, buf, mex_len, 0);
                            if (gestione_errore(ret, &master, i)){                              
                                err = 1;
                                break;
                            }               
                        }
                        if (err == 1){
                            continue;   //se sono uscito dal ciclo per un errore, passo alla prossima iterazione del for esterno
                        }
                                                                    //se invece tutto è andato bene, mando il messaggio di "FIN" 
                        mex_len = 4;                                            
                        mex_len_n = htonl(mex_len);
                        ret = send(i, &mex_len_n, sizeof(uint32_t), 0);
                        if (gestione_errore(ret, &master, i)){                              
                            continue;
                        }
                        ret = send(i, "FIN", mex_len, 0);
                        if (gestione_errore(ret, &master, i)){                              
                            continue;
                        }
                    }

                    else if(strcmp(comando, "comanda") == 0){                   //dunque è un td e ha inviato una comanda
                        for(j = 0; j < tavoli; j++){                            //trovo innanzitutto l'indice del td in array_tds 
                            if (array_tds[j] == i){
                                break;
                            }
                        }
                        
                        ret = aggiungi_comanda(buf, j);     //aggiungo la comanda tra le comande in attesa
                        if (ret == 0){                      //se si è raggiunto il numero max di comande in attesa, avviso 
                            ret = send(i, "E4", 3, 0);
                            if (gestione_errore(ret, &master, i)){                              
                                continue;
                            }
                        }
                        else if (ret == -1){                //se la comanda non era valida, avviso 
                            ret = send(i, "E5", 3, 0);
                            if (gestione_errore(ret, &master, i)){                              
                                continue;
                            }
                        }
                        else if (ret == -2){                //se il td ha raggiunto il numero max di comande inviabili, avviso 
                            ret = send(i, "E6", 3, 0);
                            if (gestione_errore(ret, &master, i)){                              
                                continue;
                            }
                        }
                        else{                               //altrimenti, la comanda è stata correttamente aggiunta
                            ret = send(i, "OK", 3, 0);      //avviso allora il td
                            if (gestione_errore(ret, &master, i)){                              
                                continue;
                            }            
                            
                            sprintf(buf, "nuova");          //e avviso tutti i kd che c'è una nuova comanda in attesa
                            mex_len = strlen(buf) + 1;
                            mex_len_n = htonl(mex_len);
                            for (j = 0; j < MAX_KITCHENDEVICES; j++){
                                if(array_kds[j] != 0){
                                    ret = send(array_kds[j], &mex_len_n, sizeof(uint32_t), 0);
                                    if (gestione_errore(ret, &master, array_kds[j])){      //se c'è un errore, gestisco questo kd e tranquillamente continuo ad avvisare gli altri kd della comanda in attesa                            
                                        continue;
                                    }
                                    ret = send(array_kds[j], buf, mex_len, 0);
                                    if (gestione_errore(ret, &master, array_kds[j])){                              
                                        continue;
                                    }
                                }
                            }
                            printf("AVVISO: nuova comanda in attesa.\n");
                            fflush(stdout);
                        }
                    }

                    else if (strcmp(comando, "conto") == 0){            //dunque è un td e ha chiesto il conto
                        for(j = 0; j < tavoli; j++){                    //trovo innanzitutto l'indice del td in array_tds
                            if (array_tds[j] == i){
                                break;
                            }
                        }

                        id = j;                 //mi salvo l'indice del tavolo in id, visto che userò j
                        conto = subtotale = 0;  //azzero conto e subtotale
                        rimuovi_comande(id);    //tolgo le comande in attesa uscite dal tavolo, che così non verranno conteggiate nel calcolo del conto

                        err = 0;
                        for (j = 0; j < MAX_PIATTI; j++){                           //inizio ora a scorrere tutte le ordinazioni totali rimaste uscite dal tavolo 
                            if (array_tavoli[id].ordinazioni_totali[j] != 0){       //quando trovo un piatto che è stato ordinato da quel tavolo
                                subtotale = (array_tavoli[id].ordinazioni_totali[j]) * (array_piatti[j].costo); //calcolo il subtotale per quel piatto
                                conto += subtotale;                                                             //lo sommo al conto totale
                                sprintf(buf, "%c%d %d %d", lettera_piatto(j), numero_piatto(j), array_tavoli[id].ordinazioni_totali[j], subtotale); //metto nel buf la voce del conto relativa a questo piatto
                                mex_len = strlen(buf) + 1;                                                                                          //e mando al td
                                mex_len_n = htonl(mex_len);
                                ret = send(i, &mex_len_n, sizeof(uint32_t), 0);
                                if (gestione_errore(ret, &master, i)){              //in caso di errore, una volta gestito, metto err a 1 ed esco innanzitutto da questo for                       
                                    err = 1;
                                    break;
                                }
                                ret = send(i, buf, mex_len, 0);
                                if (gestione_errore(ret, &master, i)){                              
                                    err = 1;
                                    break;
                                }   
                                array_tavoli[id].ordinazioni_totali[j] = 0;         //e infine azzero ordinazioni_totali[j], così che potrà essere riusato successivamente
                            }
                        }

                        if (err == 1){  //se sono uscito dal ciclo per un errore, passo alla prossima iterazione del for esterno
                            continue;
                        }

                                                                //altrimenti, mando il messaggio del totale
                        sprintf(buf, "Totale: %d", conto);                                            
                        mex_len = strlen(buf) + 1;                                            
                        mex_len_n = htonl(mex_len);
                        ret = send(i, &mex_len_n, sizeof(uint32_t), 0);
                        if (gestione_errore(ret, &master, i)){                              
                            continue;
                        }
                        ret = send(i, buf, mex_len, 0);
                        if (gestione_errore(ret, &master, i)){                              
                            continue;
                        }   
 
                        array_tavoli[id].numero_comande = 0;                                                    //azzero poi il numero di comande uscite da questo tavolo
                        memset(array_tavoli[id].comande_totali, 0, sizeof(array_tavoli[id].comande_totali));    //azzero l'array contenente tutte le comande uscite da questo tavolo
                        sprintf(buf, "rimuovi T%d", id+1);                                                      //e infine avviso i kds, che provvederanno così a smettere di gestire eventuali comande in preparazione provenienti dal tavolo che ha chiesto il conto
                        mex_len = strlen(buf) + 1;
                        mex_len_n = htonl(mex_len);
                        for (j = 0; j < MAX_KITCHENDEVICES; j++){   
                            if(array_kds[j] != 0){              
                                ret = send(array_kds[j], &mex_len_n, sizeof(uint32_t), 0);
                                if (gestione_errore(ret, &master, array_kds[j])){               //se si verifica un errore, gestisco il kd, ma poi continuo ad avvisare gli altri kds tranquillamente                      
                                    continue;
                                }
                                ret = send(array_kds[j], buf, mex_len, 0);
                                if (gestione_errore(ret, &master, array_kds[j])){                              
                                    continue;
                                }
                            }

                        }
                        printf("AVVISO: si è liberato il tavolo T%d.\n", id+1);
                        fflush(stdout);
                    }

                    else if (strcmp(comando, "take") == 0){             //dunque è un kd e ha chiesto la comanda che sta da più tempo in attesa
                        if (coda_vuota()){                              //se non c'è alcuna comanda in attesa, lo avviso
                            ret = send(i, "E2", 3, 0);
                            if (gestione_errore(ret, &master, i)){                              
                                continue;
                            }
                            continue;
                        }

                        ret = send(i, "OK", 3, 0);                      //altrimenti, gli mando l'ok
                        if (gestione_errore(ret, &master, i)){                              
                            continue;
                        }
                        sprintf(buf, "com%d T%d", coda_comande[front].numero, coda_comande[front].tav_num); //inizio allora a mandargli la comanda, partendo dalla riga di intestazione "comx Ty"  
                        mex_len = strlen(buf) + 1;
                        mex_len_n = htonl(mex_len);
                        ret = send(i, &mex_len_n, sizeof(uint32_t), 0);
                        if (gestione_errore(ret, &master, i)){                              
                            continue;
                        }
                        ret = send(i, buf, mex_len, 0);
                        if (gestione_errore(ret, &master, i)){                              
                            continue;
                        }

                        err = 0;
                        for(j = 0; j < MAX_PIATTI; j++){                            //ora gli mando tutte le ordinazioni della comanda
                            if (coda_comande[front].ordinazioni_comanda[j] != 0){
                                sprintf(buf, "%c%d %d", lettera_piatto(j), numero_piatto(j), coda_comande[front].ordinazioni_comanda[j]);
                                mex_len = strlen(buf) + 1;
                                mex_len_n = htonl(mex_len);
                                ret = send(i, &mex_len_n, sizeof(uint32_t), 0);
                                if (gestione_errore(ret, &master, i)){              //in caso di errore, una volta gestito, metto err a 1 ed esco innanzitutto da questo for                        
                                    err = 1;
                                    break;
                                }
                                ret = send(i, buf, mex_len, 0);
                                if (gestione_errore(ret, &master, i)){                              
                                    err = 1;
                                    break;
                                }                       
                            }
                        }

                        if(err == 1){       //se sono uscito dal ciclo per un errore, passo alla prossima iterazione del for esterno
                            continue;
                        }
                                            //altrimenti, mando il messaggio di "FIN" 
                        mex_len = 4;                                            
                        mex_len_n = htonl(mex_len);
                        ret = send(i, &mex_len_n, sizeof(uint32_t), 0);
                        if (gestione_errore(ret, &master, i)){                              
                            continue;
                        }
                        ret = send(i, "FIN", mex_len, 0);
                        if (gestione_errore(ret, &master, i)){                              
                            continue;
                        }

                        sprintf(buf, "com%d <in preparazione>", coda_comande[front].numero);      //avviso ora il table device che la sua comanda è in preparazione
                        mex_len = strlen(buf) + 1;
                        mex_len_n = htonl(mex_len);
                        ret = send(array_tds[coda_comande[front].tav_num-1], &mex_len_n, sizeof(uint32_t), 0);
                        if (gestione_errore(ret, &master, array_tds[coda_comande[front].tav_num-1])){                              
                            continue;
                        }
                        ret = send(array_tds[coda_comande[front].tav_num-1], buf, mex_len, 0);
                        if (gestione_errore(ret, &master, array_tds[coda_comande[front].tav_num-1])){                              
                            continue;
                        }

                        array_tavoli[coda_comande[front].tav_num - 1].comande_totali[coda_comande[front].numero -1].stato = 'p';        //aggiorno poi lo stato della comanda in array_tavoli
                        array_tavoli[coda_comande[front].tav_num - 1].comande_totali[coda_comande[front].numero -1].kd_assegnato = i;   //segno che ora se ne sta occupando il kd che ha sd 'i'
                        memset(&coda_comande[front], 0, sizeof(coda_comande[front]));   //svuoto coda_comande[front]
                        front = (front + 1)%MAX_COMANDE_IN_ATTESA;                      //e concludo l'estrazione dalla testa di coda_comande

                        printf("AVVISO: una comanda è passata allo stato 'in preparazione'.\n");
                        fflush(stdout);     
                                   
                    }

                    else if (strcmp(comando, "servizio") == 0){                                         //dunque è un kd e sta avvisando che una comanda ora è in servizio
                        sscanf(buf, "%s %d %d", comando, &numero_comanda, &numero_tavolo);
                        array_tavoli[numero_tavolo-1].comande_totali[numero_comanda-1].stato = 's';         //segno che ora la comanda è in servizio
                        array_tavoli[numero_tavolo-1].comande_totali[numero_comanda-1].kd_assegnato = 0;    //metto 0 in kd_assegnato, perché ora il kd non se ne sta più occupando
                        sprintf(buf, "com%d <in servizio>", numero_comanda);                                //e avviso il table device interessato dicendogli che la sua comanda è in servizio
                        mex_len = strlen(buf) + 1;
                        mex_len_n = htonl(mex_len);
                        ret = send(array_tds[numero_tavolo-1], &mex_len_n, sizeof(uint32_t), 0);
                        if (gestione_errore(ret, &master, array_tds[numero_tavolo-1])){                              
                            continue;
                        }
                        ret = send(array_tds[numero_tavolo-1], buf, mex_len, 0);
                        if (gestione_errore(ret, &master, array_tds[numero_tavolo-1])){                              
                            continue;
                        }

                        printf("AVVISO: una comanda è passata allo stato 'in servizio'\n");
                        fflush(stdout);
                    }
                }                                                                                                                      
            }                                                                                                                           
        }
    }
    return 0;
}