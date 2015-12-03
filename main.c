#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

#include <sys/queue.h>

struct {//ставит соотв между расширением и content type
	char *ext;
	char *conttype;
} extensions[] = {
	{".txt", "text/html"},
	{".htm", "text/html"},
	{".html", "text/html"},
	{".jpg", "text/jpeg"},
	{".jpeg", "text/jpg"},
	{".png", "image/png"},
	{".ico", "image/ico"},
	{".css", "text/css"},
	{".js", "text/javascript"},
	{".php", "text/php"},
	{".xml", "text/xml"},
	{".pdf", "application/pdf"},
	{0, 0}	
};

//char content_type[255];

const int N = 5;

pthread_t ntid[5];
pthread_t servtid;
pthread_mutex_t lock[5];
int cd[5];

struct qnode {
        int value;
        TAILQ_ENTRY(qnode) entries;
};

TAILQ_HEAD(, qnode) qhead;

void headers(int client, int size, int httpcode, char* content_type) {//заголовки ответа сервера
	char buf[1024];
	char strsize[20];
	sprintf(strsize, "%d", size);
	if (httpcode == 200) {
		strcpy(buf, "HTTP/1.0 200 OK\r\n");
	}
	else if (httpcode == 404) {
		strcpy(buf, "HTTP/1.0 404 Not Found\r\n");
	}
	else {
		strcpy(buf, "HTTP/1.0 500 Internal Server Error\r\n");
	}
	send(client, buf, strlen(buf), 0);
	strcpy(buf, "Connection: keep-alive\r\n");
	send(client, buf, strlen(buf), 0);
	strcpy(buf, "Content-length: ");
	send(client, buf, strlen(buf), 0);
	strcpy(buf, strsize);
	send(client, buf, strlen(buf), 0);
	strcpy(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
	strcpy(buf, "simple-server");
	send(client, buf, strlen(buf), 0);
	if (content_type != NULL) {
		sprintf(buf, "Content-Type: %s\r\n", content_type);
		send(client, buf, strlen(buf), 0);
		strcpy(buf, "\r\n");
		send(client, buf, strlen(buf), 0); 
	}
}

void parsingFileName(char *line, char **filepath, size_t *len) {//из строки запроса вычленяет название файла который нужно открыть
	char *start = NULL;
	while ((*line) != '/') line++;
	start = line + 1;
	while ((*line) != ' ') line++;
	(*len) = line - start;
	*filepath = (char*)malloc(*len + 1);
	*filepath = strncpy(*filepath, start, *len);
	(*filepath)[*len] = '\0';
	printf("%s \n", *filepath);
}

char* getFileExtension(char *filename) {//выдать расширение файла
	return strrchr(filename, '.');
}

void *handler(void *arg) {//для обработки запросов,в аргументе при создании handler'а мы передаём его номер в handler,в handler мы его считываем и пытаемся заблокировать соотв mutex(при создании handler основной поток блокирует его)когда сервер передаёт обработчику запрос на обработку,он разблокирует его. Тогда handler сможет заблокировать mutex и продолжить работу.сразу же после захвата mutex,он его разблокирует(показывая что hander работает)
	int filesize = 0;
	char *line = NULL;
	size_t len = 0;

	char buf[1024];

	char *filepath = NULL;
	size_t filepath_len = 0;
	int empty_str_count = 0;

	FILE *fd;
	FILE *file;

	// getting handler number
	int *p = (int *) arg;
	int k = *p;

	// wait for lock
	pthread_mutex_lock(&lock[k]);

	pthread_mutex_unlock(&lock[k]);
	
	//try opening client descriptor
	fd = fdopen(cd[k], "r");
	if (fd == NULL) {//пытается открыть cd потом начинает читать заголовки запроса клиента,при этом получает имя файла,потом открывает этот файл для бинарного чтения,потом определяется расширение файла и ищется расширение в extensions(соответствие)Если найдено,то пишем заголовки клиенту и отправляем файл клиенту
		printf("error open client descriptor as file \n");
		printf("500 Internal Server Error \n");
		headers(cd[k], 0, 500, NULL);
	} else {
		// reading client's headers
		int res;
		while ((res = getline(&line, &len, fd)) != -1) {
			if (strstr(line, "GET")) {
				parsingFileName(line, &filepath, &filepath_len);
			}
			if (strcmp(line, "\r\n") == 0) {
				empty_str_count++;
			}
			else {
				empty_str_count = 0;
			}
			if (empty_str_count == 1) {
				break;
			}
			printf("%s", line);
		}

		printf("open %s \n", filepath);

		file = fopen(filepath, "rb");

		if (file == NULL) {
			printf("404 File Not Found \n");
			headers(cd[k], 0, 404, NULL);
		}
		else {
			char *fileext = getFileExtension(filepath);
			char *content_type = 0;
			int i = 0;
			while (extensions[i].ext != 0) {
				if (strcmp(extensions[i].ext, fileext) == 0) {
					int n = strlen(extensions[i].conttype);
					content_type = (char*) malloc(n * sizeof(char));
					strncpy(content_type, extensions[i].conttype, n);
					break;
				}
				i++;
			}
			if (content_type != 0) {
				fseek(file, 0L, SEEK_END);
				filesize = ftell(file);
				fseek(file, 0L, SEEK_SET);
				headers(cd[k], filesize, 200, content_type); 

				size_t nbytes = 0;

				while ((nbytes = fread(buf, 1, 1024, file)) > 0) {
					res = send(cd[k], buf, nbytes, 0);
					if (res == -1) {
						printf("send error \n");
					}
				}

				free(content_type);
			}	
			else {
				printf("500 Internal Server Error \n");
				headers(cd[k], 0, 500, NULL);
			}
		}
	}
	close(cd[k]);
	free(p);
	
	cd[k] = -1;//handler закончил работу

	puts ("Handler destroyed");
}


void createThread(int k) {//создаёт k-ый поток
	int *m = (int *)malloc(sizeof(int));
	*m = k;
	int err = pthread_create(&ntid[k], NULL, handler, (void *) m);
	if (err != 0) {
		printf("You can't to create a thread %s\n", strerror(err));
	}
}

void *serv(void *arg) {//бесконечный цикл который восстанавливает умершие потоки и передаёт им cd(клиентские дескрипторы)
	int i;

	while (1) {	
		struct qnode *item = TAILQ_FIRST(&qhead);
		if (item != NULL) {
			puts("Trying to find handler");
			// try finding handler
			i = 0;
			while (i < N) {
				if (pthread_mutex_trylock(&lock[i]) != 0) { 
					// mutex is not acquired -> thread is free -> found
					puts("Handler found");
					cd[i] = item->value;
					pthread_mutex_unlock(&lock[i]); // unlock mutex

					// delete item from queue
					TAILQ_REMOVE(&qhead, item, entries);
					free(item);

					break;
				}
				else {
					pthread_mutex_unlock(&lock[i]);
				}
				i++;
			}
		}
	}
}


int main() {//инициализируем очередь
	int ld = 0;
	int res = 0;
	int _cd = 0;
	const int backlog = 10;
	struct sockaddr_in saddr;
	struct sockaddr_in caddr;
	socklen_t size_saddr;
	socklen_t size_caddr;

	struct qnode *qitem;

	int i = 0;

	//init queue
	TAILQ_INIT(&qhead);

	// creating threads
	while (i < N) {
		pthread_mutex_init(&lock[i], NULL);
		pthread_mutex_lock(&lock[i]);
		createThread(i);
		i++;
	}

	int err = pthread_create(&servtid, NULL, serv, NULL);//создаём поток для метода serv
	if (err != 0) {
		printf("You can't to create a thread %s\n", strerror(err));
	}

	ld = socket(AF_INET, SOCK_STREAM, 0);//открываем слушателя дескриптора и конфигурируем сервер (до puts start)
	if (ld == -1) {
		printf("listener creation error \n");
	}
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(8080);
	saddr.sin_addr.s_addr = INADDR_ANY;
	if (bind(ld, (struct sockaddr *)&saddr, sizeof(saddr)) == -1) {
		printf("bind error \n");
	}
	res = listen(ld, backlog);
	if (res == -1) {
		printf("listen error \n");
	}

	puts("Start");

	while (1) {//бесконечный цикл,получаем cd, и кладём его в очередь
		_cd = accept(ld, (struct sockaddr *)&caddr, &size_caddr);
		if (_cd == -1) {
			printf("accept denied error \n");
		}
		printf("client in %d descriptor. Client addr is %d \n", _cd, caddr.sin_addr.s_addr);

		qitem = malloc(sizeof(*qitem));
		qitem->value = _cd;
                TAILQ_INSERT_TAIL(&qhead, qitem, entries);

	}
	return 0;
}
