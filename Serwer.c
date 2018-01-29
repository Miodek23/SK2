#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>

#define MAX_CLIENTS	100
#define BUFF_SIZE	1024

static unsigned int clients_number = 0;
static int uid = 10;

//struktura zawierająca dane, które zostaną przekazane do wątku
typedef struct {
	struct sockaddr_in addr;
	int connection_socket_descriptor;
	int uid;			
	char name[32];
} client_t;


client_t *clients[MAX_CLIENTS];


void add_client(client_t *cl){
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(!clients[i]){
			clients[i] = cl;
			return;
		}
	}
}


void delete_client(int uid){
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(clients[i]){
			if(clients[i]->uid == uid){
				clients[i] = NULL;
				return;
			}
		}
	}
}


void send_message_all(char *s){
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(clients[i]){
			write(clients[i]->connection_socket_descriptor, s, strlen(s));
		}
	}
}


void send_message_client(char *s, char *n){
	int i;
	for(i=0;i<MAX_CLIENTS;i++){
		if(clients[i]){
			if(strcmp(clients[i]->name, n)==0){
				write(clients[i]->connection_socket_descriptor, s, strlen(s));
			}
		}
	}
}



void send_active_clients(int connection_socket_descriptor){
	int i;
	char s[sizeof(clients)+50];
	sprintf(s, "#lista\n");
	for(i=0;i<MAX_CLIENTS;i++){
		if(clients[i]){
			strcat(s, clients[i]->name);
			strcat(s, "\n");
		}
	}
	strcat(s, "#koniec\n");
	write(connection_socket_descriptor, s, strlen(s));
}


void strip_newline(char *s){
	while(*s != '\0'){
		if(*s == '\r' || *s == '\n'){
			*s = '\0';
		}
		s++;
	}
}


void print_client_ip(struct sockaddr_in addr){
	printf("%d.%d.%d.%d",
		addr.sin_addr.s_addr & 0xFF,
		(addr.sin_addr.s_addr & 0xFF00)>>8,
		(addr.sin_addr.s_addr & 0xFF0000)>>16,
		(addr.sin_addr.s_addr & 0xFF000000)>>24);
}


//funkcja obsługująca połączenie z nowym klientem
void *handleConnection(void *arg){
	char buff_out[BUFF_SIZE];
	char buff_in[BUFF_SIZE];
	int rlen;
	buff_out[0] = '\0';
	
	clients_number++;
	client_t *cli = (client_t *)arg;

	printf("***PRZYJĘTO ");
	print_client_ip(cli->addr);
	printf(" POD ID %d\n", cli->uid);

	int logged=0;
	 		
	rlen=read(cli->connection_socket_descriptor, buff_in, sizeof(buff_in)-1);
	buff_in[rlen] = '\0';	
	
	FILE * fp;
    	char * line = NULL;
   	size_t len = 0;
    	ssize_t readed;
	const char delim[] = "\t";	
	char *token;
	char receiver[32];
	
    	fp = fopen("./pass.txt", "r");
    	if (fp == NULL)
        exit(EXIT_FAILURE);


    	while ((readed = getline(&line, &len, fp)) != -1) {
		if(strcmp(line, buff_in)==0){
			logged=1;	
		}
    	}
    	fclose(fp);

	if (logged==1){
		token = strtok(buff_in, delim);
		if (token)
	        	sprintf(cli->name,token);
	} else {
		close(cli->connection_socket_descriptor);

		delete_client(cli->uid);
		printf("***LOGOWANIE NIEUDANE, IP: ");
		print_client_ip(cli->addr);
		printf(" POD ID %d\n", cli->uid);
		free(cli);
		clients_number--;
		pthread_detach(pthread_self());
	}

        rlen = sprintf(buff_in, "%s\n", "zalogowany");
	write(cli->connection_socket_descriptor, buff_in, rlen);    	
	
		
	sprintf(buff_out, "***DOŁĄCZONO, WITAJ %s\r\n", cli->name);
	//send_message_all(buff_out);

	memset(&buff_in, 0, sizeof(buff_in));
	memset(&buff_out, 0, sizeof(buff_out));


	while((rlen = read(cli->connection_socket_descriptor, buff_in, sizeof(buff_in)-1)) > 0){
	        buff_in[rlen] = '\0';
	        buff_out[0] = '\0';
		printf("Odebrano tekst: %s\n", buff_in);

		if(strcmp(buff_in, "#lista\n")==0){
			send_active_clients(cli->connection_socket_descriptor);
			memset(&buff_in, 0, sizeof(buff_in));
			memset(&buff_out, 0, sizeof(buff_out));
			continue;
		}

		if(strcmp(buff_in, "#wyloguj\n")==0){
			close(cli->connection_socket_descriptor);
			sprintf(buff_out, "***ODŁĄCZONO, ŻEGNAJ %s\r\n", cli->name);
			send_message_all(buff_out);

			delete_client(cli->uid);
			printf("***ODŁĄCZONO ");
			print_client_ip(cli->addr);
			printf(" o id %d\n", cli->uid);
			free(cli);
			clients_number--;
			pthread_detach(pthread_self());
			return NULL;
		}			

		sprintf(receiver, strtok(buff_in, delim));
		sprintf(buff_out, strtok(NULL, delim));
		memset(&buff_in, 0, sizeof(buff_in));
		
		strip_newline(buff_out);

		if(!strlen(buff_out)){
			memset(&buff_in, 0, sizeof(buff_in));
			memset(&buff_out, 0, sizeof(buff_out));
			continue;
		}

		sprintf(buff_in, "%s\t", cli->name);
		printf("Wiadomość od %s do %s o treści:\n%s\n",cli->name, receiver, buff_out);	
		send_message_client(strcat(buff_in, buff_out), receiver);
		

		memset(&buff_in, 0, sizeof(buff_in));
		memset(&buff_out, 0, sizeof(buff_out));
	}		
}

int main(int argc, char *argv[]){
	int server_socket_descriptor = 0, connection_socket_descriptor = 0;
	struct sockaddr_in serv_addr;
	struct sockaddr_in cli_addr;
	pthread_t tid;

	//inicjalizacja gniazda serwera
	server_socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(2222); 

	
	if(bind(server_socket_descriptor, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
		perror("Błąd przy próbie dowiązania adresu IP i numeru portu do gniazda!");
		return 1;
	}

	if(listen(server_socket_descriptor, MAX_CLIENTS) < 0){
		perror("Błąd przy próbie ustawienia wielkości kolejki.");
		return 1;
	}

	printf("***URUCHOMIONO SERWER***\n");


	while(1){
		socklen_t clilen = sizeof(cli_addr);
		connection_socket_descriptor = accept(server_socket_descriptor, (struct sockaddr*)&cli_addr, &clilen);

		if((clients_number+1) == MAX_CLIENTS){
			printf("***MAKSYMALNA LICZBA KLIENTÓW OSIĄGNIĘTA***\n");
			printf("***ODRZUCONO: ");
			print_client_ip(cli_addr);
			printf("\n");
			close(connection_socket_descriptor);
			continue;
		}

		client_t *cli = (client_t *)malloc(sizeof(client_t));
		cli->addr = cli_addr;
		cli->connection_socket_descriptor = connection_socket_descriptor;
		cli->uid = uid++;
		sprintf(cli->name, "%d", cli->uid);

		add_client(cli);
		pthread_create(&tid, NULL, &handleConnection, (void*)cli);

		sleep(1);
	}
	close(server_socket_descriptor);
   	return(0);
}
