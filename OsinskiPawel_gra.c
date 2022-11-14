#include <sys/types.h>      
#include <sys/socket.h>     
#include <sys/shm.h>                                                //////////////////////////////////////////////
#include <sys/wait.h>                                               // PROJEKT ZALICZENIOWY (nr III) -> Gra UDP //
#include <sys/ipc.h>                                                // Wykonal Pawel Osinski, 2 rok informatyki //
#include <stdio.h>                                                  // Uniwersytet Mikolaja Kopernika w Toruniu //
#include <netdb.h>                                                  //////////////////////////////////////////////
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

struct playerdata{
    char name[16];      /* tablica znakow z nasza nazwa */
    char enemyname[16]; /* tablica znakow z nazwa przeciwnika*/
	char text[16];      /* tablica znakow z komendami lub polem przekazanym do przeciwnika */
	char plansza[10];   /* tablica znakow z polami na planszy */
	int wynik[2];       /* [0] - moj wynik / [1] - wynik przeciwnika */
    int czykoniec;      /* [0] - domyslnie / [1] - po opuszczeniu gry */
	int ktory[2];       /* [0] -  ktorym graczem jestem ja / [1] - ktorym graczem jest przeciwnik */
	int czyruch;        /* [0] - ruch przeciwnika / [1] - moj ruch */
	int ktoryruch;      /* pomocniczy licznik ruchow w danej rundzie */
	int runda;          /* pomocniczy licznik rund */
};

key_t shmkey;           /* klucz pamieci wspoldzielonej */
int   shmid;            /* id pamieci wspoldzielonej */
struct shmid_ds buf;    /* bufor pamieci wspoldzielonej */
void kill_shared();     /* funkcja do 'czyszczenia po sobie' przy konczeniu dzialania gry*/
int child;              /* id procesu potomka */


struct playerdata player, player_2, *shared_data;                   /*  player: wysylanie struktury do przeciwnika
                                                                        player_2: odbieranie struktury od przeciwnika
                                                                        shared_data: laczenie danych pomiedzy procesem macierzystym a potomnym */

void board();                                                       /* wyswietlanie planszy */
void makeamove(char *f_plansza, char ruch, char pionek);            /* wykonanie ruchu na planszy */
int checkpoints(int win, struct playerdata *data, int rcvorsend);   /* funkcja pomocnicza */
int isfieldfree(char move, char *f_plansza);                        /* funkcja pomocnicza (czy pole do ktorego chcemy wpisac ruch jest wolne) */
int isendofround();                                                 /* funkcja pomocnicza (czy na planszy doszlo do konca rundy) */

int sockfd;                                                         /* deskryptor gniazda */
struct addrinfo *informations;                                      /* informacje IP gracza z ktorym bedziemy sie laczyc */
socklen_t len;                                                      /* rozmiar addr */
struct sockaddr_in s_address, c_address, *docelowy;                 /* struktury adresowe (adresu, klienta) */

char pionek;                                                        /* znak na planszy (kolko lub krzyzyk) */
char default_board[10] =                                            /* domyslna plansza */
   {'.', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i'};


int main(int argc, char *argv[]) {

    int isend;                                                      /* pomocnicza zmienna do okreslenia konca gry dla jednego z graczy */

	if(argc < 2 || argc > 3){
		printf("Blad! Prosze uzyc programu nastepujaco: '%s adres_maszyny_zdalnej [nick]' (nick nieobowiazkowy)\n", argv[0]);
		return 1;
	}

	/* Zapisujemy nick uzytkownika */

	if(argc == 2){
		strcpy(player.name,"NN");}
    else {
		if(strlen(argv[2]) > 15){
			printf("Twoj nick '%s' jest zbyt dlugi (Max 15 znakow)!\n", argv[2]);
			return 1;
		}
		strcpy(player.name, argv[2]);
	}

/* ============================================================================ */
/*                         POLACZENIE POMIEDZY MASZYNAMI                        */
/* ============================================================================ */

	/* Pobieramy informacje o adresie podanego hosta */

	if (getaddrinfo(argv[1], "6469", NULL, &informations) != 0) {
		printf("Podano bledny adres maszyny!\n");
		return 1;
	}

	/* Tworzymy gniazdo UDP */

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if(sockfd < 0){
		printf("Blad tworzenia gniazda!\n");
		return 1;
	}

	/* zapisujemy IP z ktorym bedziemy sie laczyc */

	docelowy = (struct sockaddr_in *)(informations->ai_addr);

	/* struktura serwera */

	s_address.sin_family	    = AF_INET;
	s_address.sin_addr.s_addr = htonl(INADDR_ANY);  /* nasz adres */
	s_address.sin_port        = htons(6469);        /* port serwera */

	/* struktura klienta z ktorym sie laczymy */

	c_address.sin_family	    = AF_INET;
	c_address.sin_addr 		= docelowy->sin_addr;   /* IP docelowego hosta */
	c_address.sin_port        = htons(6469);        /* port hosta */
	len = sizeof(c_address);

	/* Rejestrujemy nasze gniazdo w systemie */

	if(bind(sockfd, (struct sockaddr *)&s_address, sizeof(s_address)) < 0){
		printf("Blad bindowania gniazda!\n");
		printf("Gra na tej maszynie jest juz w uzyciu... :(\n");
		close(sockfd);
		return 1;
	}

/* ============================================================================ */
/*              PAMIEC WSPOLDZIELONA (z obsluga bledow)                         */
/* ============================================================================ */

    /* tworzenie klucza */

    if( (shmkey = ftok(argv[0], 1)) == -1) {
        errno=EINVAL;
        perror("\nBlad tworzenia klucza!\n");
    }
    /* tworzenie segmentu pamieci wspoldzielonej*/

    if( (shmid = shmget(shmkey, sizeof(struct playerdata), 0600 | IPC_CREAT | IPC_EXCL)) == -1){
        perror("\nBlad shmget!\n");
        exit(1);
    }
    shmctl(shmid, IPC_STAT, &buf);

    /* dolaczenie pamieci wspoldzielonej */

    shared_data = (struct playerdata *) shmat(shmid, (void *)0, 0);
    if(shared_data == (struct playerdata *)-1) {
        errno = EINVAL;
        printf("Usuniecie pamieci wspoldzielonej: ");
	    if((shmctl(shmid, IPC_RMID, 0)) == 0) printf("OK\n");
	    else printf("\nblad shmctl");
        perror("Blad dolaczenia pamieci wspoldzielonej");
    }

    /* SIGINT handler - przechwytywanie Ctrl^C i konczenie dzialania programu */

    signal(SIGINT, kill_shared);

/* ============================================================================ */
/*                                ROZPOCZECIE GRY                               */
/* ============================================================================ */

	printf("Rozpoczynam gre z %s.\n(Napisz <koniec> by zakonczyc lub <wynik> by wyswietlic aktualny wynik gry)\n", inet_ntoa(docelowy->sin_addr));

	/* Wysylamy wiadomosc o naszym dolaczeniu do gry */

	strcpy(player.text, "<imsecond>");
	strcpy(player.plansza, default_board);
	shared_data->runda = 1;
	shared_data->czykoniec = 0;
	player.czykoniec = 0;
    strcpy(shared_data->plansza,default_board);

    sendto(sockfd, &player, sizeof(player), 0,(struct sockaddr *)&s_address, sizeof(s_address));
    sleep(1);
    printf("[Propozycja gry wyslana] Oczekiwanie na gracza...\n");

    recvfrom(sockfd, &player_2, sizeof(player_2), MSG_DONTWAIT, 0, 0);
    if (strcmp(player_2.text,"<imsecond>")!=0){
        shared_data->ktory[0] = 1;
        shared_data->ktory[1] = 2;
        shared_data->czyruch = 1;
        strcpy(player.text, "<imfirst>");
    } else {
        shared_data->ktory[0] = 2;
        shared_data->ktory[1] = 1;
        shared_data->czyruch = 0;
    }
        shared_data->ktoryruch = 1;


    /* Proba wyslania informacji do przeciwnika w celu weryfikacji kolejnosci rozpoczecia gry */

	if(sendto(sockfd, &player, sizeof(player), 0,(struct sockaddr *)&c_address, sizeof(c_address)) < 0){
		printf("Blad wysylania wiadmosci o dolaczeniu!\n");
		close(sockfd);
		return 1;
	}

/* ============================================================================ */
/*           POTOMEK (Odpowiedzialny za odebranie ruchu przeciwnika)            */
/* ============================================================================ */

    if((child = fork())==0){
        strcpy(shared_data->enemyname,"<undefined>");
        if(shared_data->czyruch==1) board(shared_data->plansza);

        /* glowna petla ciaglego dzialania potomka */

        while(1) {

            /* petla do poszczegolnych rund */

            do{

                if(recvfrom(sockfd, &player_2, sizeof(player_2), 0, (struct sockaddr *)&c_address, &len) < 0){
                    printf("Blad pobierania informacji od drugiego gracza!\n");
                    kill_shared();
                    return 1;
                }
                shared_data->czykoniec = player_2.czykoniec;
                
                /* Jezeli dolaczyl nowy uzytkownik informujemy o dolaczeniu */

                if(strcmp(player_2.text,"<imsecond>") == 0){
                    printf("\n(gracz nr %d)[%s (%s) dolaczyl do gry!]\n", shared_data->ktory[1], player_2.name, inet_ntoa(c_address.sin_addr));
                    shared_data->ktory[0] = 1;
                    shared_data->ktory[1] = 2;
                    shared_data->czyruch = 1;
                    shared_data->czykoniec = 0;
                    player.czykoniec = 0;
                    player_2.czykoniec = 0;
                    strcpy(shared_data->enemyname,player_2.name);
                    board(default_board);
                } else if(strcmp(player_2.text,"<imfirst>") == 0){
                    shared_data->ktory[0] = 2;
                    shared_data->ktory[0] = 1;
                    shared_data->czyruch = 0;
                }
                /* Jezeli odszedl polaczony uzytkownik informujemy o odejsciu */

                else if(strcmp(player_2.text,"<koniec>") == 0){
                    if (player_2.czykoniec == 0){
                            printf("\n(gracz nr %d)[%s (%s) odszedl z gry.]\n", shared_data->ktory[1], player_2.name, inet_ntoa(c_address.sin_addr));
                            printf("Poczekaj na przeciwnika...\n");
                    }
                    strcpy(shared_data->plansza, default_board);
                    player_2.czykoniec = 1;
                    shared_data->runda = 1;
                    shared_data->czyruch = 0;
                    shared_data->wynik[0] = 0;
                    shared_data->wynik[1] = 0;
                    shared_data->ktory[0] = 1;
                    shared_data->ktory[1] = 2;
                    strcpy(shared_data->enemyname, "<undefined>");
                }

                /* Wypisanie ruchu przeciwnika wraz z plansza */

                else {
                    shared_data->ktoryruch++;
                    printf("\n[%s (%s) zamalowal pole %s]\n", player_2.name, inet_ntoa(c_address.sin_addr), player_2.text);
                    if (strcmp(shared_data->enemyname,"<undefined>") == 0)
                        strcpy(shared_data->enemyname,player_2.name);
                    pionek = (shared_data->ktory[1] == 1) ? 'O' : 'X';
                    strcpy(shared_data->plansza,player_2.plansza);
                    strcpy(player.plansza,shared_data->plansza);
                    strcpy(player_2.plansza,shared_data->plansza);
                    board(shared_data->plansza);
                }
                isend = checkpoints(isendofround(shared_data->plansza), shared_data, 2);

                /* przy pierwszym ruchu kolejnych rund wyswietl plansze */

                if(shared_data->ktoryruch == 1 && shared_data->czyruch == 1  && player_2.czykoniec == 0 && shared_data->runda > 1)
                    board(shared_data->plansza);
                if (player_2.czykoniec == 0)
                    printf("[wybierz pole] -> ");
                if (player_2.czykoniec == 0)
                    shared_data->czyruch = 1;
                fflush(stdout); /* czyszczenie bufora do umozliwienia nadpisywania jego */
            } while (isend);
        }
        kill_shared();
        exit(0);
	}

/* ============================================================================ */
/*              PROCES MACIERZYSTY (Odpowiedzialny za wysylanie ruchow)         */
/* ============================================================================ */

	else {

        /* Glowna petla procesu macierzystego */

		while(1) {

                /* Petla odpowiedzialna za poszczegolne rundy */

                do{
                    if (shared_data->czyruch==1)  printf("[wybierz pole] -> ");
                    fgets(player.text, 16, stdin);
                    player.text[strlen(player.text)-1] = '\0';

                    /* Wyswietlanie wyniku po wpisaniu odpowiedniej komendy */

                    if(strcmp(player.text,"<wynik>") == 0){
                        if (strcmp(shared_data->enemyname, "<undefined>") == 0) 
                            printf("Gra sie jeszcze nie zaczela!\n");
                        else 
                            printf("[Wynik gry] Ty %d : %d %s\n", shared_data->wynik[0],shared_data->wynik[1], shared_data->enemyname);
                    }

                    /* Opuszczenie gry i wywolanie funkcji kill_shared ktora 'posprzata' i poinformuje przeciwnika o naszym odejsciu */

                    else if(strcmp(player.text,"<koniec>") == 0){
                        if(kill(child, SIGINT) < 0){
                            printf("Nie zamknieto potomka!\n");
                            kill_shared();
                            return 1;
                        }
                        kill_shared();
                        return 0;
                    }

                    /* Sprawdzenie czy jest nasz ruch, nastepnie czy wybrane pole jest prawidlowe/wolne a nastepnie wykonanie ruchu na planszy */

                    else if(shared_data->czyruch==1){
                        if(isfieldfree(player.text[0], shared_data->plansza)){

                            pionek = (shared_data->ktory[0] == 1) ? 'O' : 'X';
                            makeamove(shared_data->plansza, player.text[0], pionek);
                            strcpy(player.plansza,shared_data->plansza);
                            board(shared_data->plansza);
                            shared_data->ktoryruch++;

                            /* wyslanie danych ze struktury player do przeciwnika */

                            if(sendto(sockfd, &player, sizeof(player), 0,(struct sockaddr *)&c_address, sizeof(c_address)) < 0){
                                printf("Blad wysylania wiadmosci!\n");
                                kill_shared();
                                return 1;
                            }
                            shared_data->czyruch=0;
                        }
                    }else printf("Poczekaj na swoj ruch!!!\n");

                } while ( checkpoints(isendofround(shared_data->plansza), shared_data, 1));
        }
	}
	kill_shared();
	return 0;
}

void makeamove(char *f_plansza, char ruch, char pionek){
        if      (ruch == 'a') f_plansza[1] = pionek;
        else if (ruch == 'b') f_plansza[2] = pionek;
        else if (ruch == 'c') f_plansza[3] = pionek;
        else if (ruch == 'd') f_plansza[4] = pionek;
        else if (ruch == 'e') f_plansza[5] = pionek;
        else if (ruch == 'f') f_plansza[6] = pionek;
        else if (ruch == 'g') f_plansza[7] = pionek;
        else if (ruch == 'h') f_plansza[8] = pionek;
        else if (ruch == 'i') f_plansza[9] = pionek;
}


int isendofround(char * f_plansza){ /* sprawdzenie czy stan planszy oznacza koniec rundy */
    if (f_plansza[1] == f_plansza[2] && f_plansza[2] == f_plansza[3]) return 1;
    else if (f_plansza[4] == f_plansza[5] && f_plansza[5] == f_plansza[6]) return 1;
    else if (f_plansza[7] == f_plansza[8] && f_plansza[8] == f_plansza[9]) return 1;
    else if (f_plansza[1] == f_plansza[4] && f_plansza[4] == f_plansza[7]) return 1;
    else if (f_plansza[2] == f_plansza[5] && f_plansza[5] == f_plansza[8]) return 1;
    else if (f_plansza[3] == f_plansza[6] && f_plansza[6] == f_plansza[9]) return 1;
    else if (f_plansza[1] == f_plansza[5] && f_plansza[5] == f_plansza[9]) return 1;
    else if (f_plansza[3] == f_plansza[5] && f_plansza[5] == f_plansza[7]) return 1;
    else if (f_plansza[1] != 'a' && f_plansza[2] != 'b' && f_plansza[3] != 'c' &&
            f_plansza[4] != 'd' && f_plansza[5] != 'e' && f_plansza[6] != 'f' &&
            f_plansza[7] != 'g' && f_plansza[8] != 'h' && f_plansza[9] != 'i')
            return 0;
    else return  -1;
}


int isfieldfree(char move, char *f_plansza){ /* sprawdzenie czy wybrane pole na planszy jest wolne i czy wpisany ruch jest poprawny */
        if (move == 'a'){
            if (f_plansza[1] == 'a') return 1;
            else {printf("To pole jest juz zajete!\n\n[wybierz pole] -> "); return 0;}
        }
        else if (move == 'b'){
            if (f_plansza[2] == 'b') return 1;
            else {printf("To pole jest juz zajete!\n\n[wybierz pole] -> "); return 0;}
        }
        else if (move == 'c'){
            if (f_plansza[3] == 'c') return 1;
            else {printf("To pole jest juz zajete!\n\n[wybierz pole] -> "); return 0;}
        }
        else if (move == 'd'){
            if (f_plansza[4] == 'd') return 1;
            else {printf("To pole jest juz zajete!\n\n[wybierz pole] -> "); return 0;}
        }
        else if (move == 'e'){
            if (f_plansza[5] == 'e') return 1;
            else {printf("To pole jest juz zajete!\n\n[wybierz pole] -> "); return 0;}
        }
        else if (move == 'f'){
            if (f_plansza[6] == 'f') return 1;
            else {printf("To pole jest juz zajete!\n\n[wybierz pole] -> "); return 0;}
        }
        else if (move == 'g'){
            if (f_plansza[7] == 'g') return 1;
            else {printf("To pole jest juz zajete!\n\n[wybierz pole] -> "); return 0;}
        }
        else if (move == 'h'){
            if (f_plansza[8] == 'h') return 1;
            else {printf("To pole jest juz zajete!\n\n[wybierz pole] -> "); return 0;}
        }
        else if (move == 'i'){
            if (f_plansza[9] == 'i') return 1;
            else {printf("To pole jest juz zajete!\n\n[wybierz pole] -> "); return 0;}
        }
    else{
        printf("Niepoprawne pole!\n");
        return 0;
    }
}


int checkpoints(int win, struct playerdata *data, int rcvorsend){ /* przyznanie punktu wygranemu / reset rundy */
    int temp;

    if (win == 1) {
        data->runda++;
        data->ktoryruch=1;
        strcpy(data->plansza,default_board); /* zerujplansze */

        /* jezeli wysylam wygrana plansze oddaje pierwszy ruch dla przeciwnika bo przegral */

        if (rcvorsend==1){
                data->ktory[0]=2;
                data->ktory[1]=1;
                data->wynik[0]++; //dodaje punkt dla siebie
                printf("KONIEC RUNDY!\nZdobywasz punkt!\nPoczekaj na ruch przeciwnika...\n");
                return 0;
        }
        /* jezeli odbieram wygrana plansze ustawiam moja kolej bo przegralem */

        if (rcvorsend==2){

                data->ktory[0]=1;
                data->ktory[1]=2;
                data->wynik[1]++; /* dodaje punkt dla przeciwnika */
                printf("KONIEC RUNDY!\nPrzeciwnik zdobywa punkt! Sprobuj jeszcze raz:\n");
                board(default_board);
                return 0;
        }

    }

    /* jezeli jest remis (jezeli zremisowana runde zaczynal gracz 1, teraz runde zacznie gracz 2 i na odwrot */

    else if (win == 0) {
        data->runda++;
        data->ktoryruch=1;
        temp = data->ktory[0];
        data->ktory[0] = data->ktory[1];
        data->ktory[1] = temp;
        strcpy(data->plansza,default_board); /* zeruje plansze */
        printf("KONIEC RUNDY!\nRemis!\n");
        if(data->ktory[0]==1) 
            board(default_board);
        return 0;
    }
    return 1;
}


void board(char *f_plansza){ /* wyswietlenie planszy */
    printf("\n");
    printf("  %c  |  %c  |  %c \n", f_plansza[1], f_plansza[2], f_plansza[3]);
    printf("  %c  |  %c  |  %c \n", f_plansza[4], f_plansza[5], f_plansza[6]);
    printf("  %c  |  %c  |  %c \n", f_plansza[7], f_plansza[8], f_plansza[9]);
    printf("\n");
}


void kill_shared(){ /* sprzatanie po sobie przy koncu gry */

    /* poinformowanie przeciwnika o zakonczeniu gry */

    if ((shared_data->czykoniec) < 1) {
            shared_data->czykoniec++;
            strcpy(player.text,"<koniec>");
            sendto(sockfd, &player, sizeof(player), 0,(struct sockaddr *)&c_address, sizeof(c_address));
            player.czykoniec=1;}

    player.czykoniec = 1;
    shared_data->czykoniec = 1;
    printf("%s%s\n",
    (shmdt(shared_data) == 0)        ?"":"blad shmdt\n", /* odlaczanie pamieci */
    (shmctl(shmid, IPC_RMID, 0) == 0)?"":"blad shmctl\n"); /* zwalnianie pamieci wspol */
    close(sockfd);
    exit(0);
}