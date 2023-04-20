/*
    참고 코드 : https://github.com/Ethan-Yan27/CSAPP-Labs/blob/master/yzf-proxylab-handout-3e/proxy%20(Part%203%20reader%20first).c

    Caching Proxy Web Server
    1. 들어오는 연결을 수락
    2. 클라이언트의 HTTP 요청을 구문 분석
    3. 요청된 개체가 캐시에 있는지 확인
    4. 원격 서버에 연결
    5. 요청을 서버로 전달
    6. 서버로부터 응답을 받음
    7. 응답을 캐시에 저장
    8. 응답을 클라이언트에 전달
    9. 연결을 닫음.
*/

#include <stdio.h>
#include <pthread.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define LRU_MAGIC_NUMBER 9999
#define CACHE_OBJS_COUNT 10

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *new_version = "HTTP/1.0";

void doit(int fd);
void do_request(int p_clientfd, char *method, char *uri_ptos, char *host); // connect_endServer
void do_response(int p_connfd, int p_clientfd); // build_http_header
int parse_uri(char *uri, char *uri_ptos, char *host, char *port);
void *thread(void *vargp);

/*cache function*/
void cache_init();
int cache_find(char *url);
int cache_eviction();
void cache_LRU(int index);
void cache_uri(char *uri,char *buf);
void readerPre(int i);
void readerAfter(int i);

typedef struct {
    char cache_obj[MAX_OBJECT_SIZE];
    char cache_url[MAXLINE];
    int LRU;
    int isEmpty;

    int readCnt;            /*count of readers*/
    sem_t wmutex;           /*protects accesses to cache*/
    sem_t rdcntmutex;       /*protects accesses to readcnt*/

}cache_block;

typedef struct {
    cache_block cacheobjs[CACHE_OBJS_COUNT];  /*ten cache blocks*/
    int cache_num;
}Cache;

Cache cache;

int main(int argc, char **argv) {
  int listenfd, *connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  cache_init();

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  Signal(SIGPIPE,SIG_IGN);
  listenfd = Open_listenfd(argv[1]); // 서버 소켓 디스크립터 생성
  while (1)
  {
    clientlen = sizeof(clientaddr); // 수락한 클라이언트 구조체
    // connfdp = Malloc(sizeof(int));
    *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    /* print accepted message */
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    Pthread_create(&tid, NULL, thread, connfd);
  }
  return 0;
}

void *thread(void *vargp) {
    int connfd = *((int *)vargp);

    Pthread_detach(pthread_self());
    doit(connfd);
    Close(connfd);
}

/* handle the client HTTP transaction */
void doit(int connfd) {
  int end_serverfd;
  int clientfd;
  char buf[MAXLINE], host[MAXLINE], port[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char uri_ptos[MAXLINE];
  rio_t rio, server_rio;

  Rio_readinitb(&rio, connfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers to proxy:\n"); 
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  char url_store[100];
  strcpy(url_store, uri);
  if(strcasecmp(method, "GET")) {
      printf("Proxy does not implement the method");
      return;
  }

  int cache_index;
  if((cache_index = cache_find(url_store)) != -1) {
          readerPre(cache_index);
          Rio_writen(connfd, cache.cacheobjs[cache_index].cache_obj, strlen(cache.cacheobjs[cache_index].cache_obj));
          readerAfter(cache_index);
          return;
  }

  parse_uri(uri, uri_ptos, host, port);

  clientfd = Open_clientfd(host, port);

  /*connect to the end server*/
  end_serverfd = do_request(clientfd, method, uri_ptos, host);
  if(end_serverfd<0){
      printf("connection failed\n");
      return;
  }

  Rio_readinitb(&server_rio,end_serverfd);

  /*write the http header to endserver*/
  // Rio_writen(end_serverfd,endserver_http_header,strlen(endserver_http_header));

  /*receive message from end server and send to the client*/
  char cachebuf[MAX_OBJECT_SIZE];
  int sizebuf = 0;
  size_t n;
  while((n=Rio_readlineb(&server_rio,buf,MAXLINE))!=0)
  {
      sizebuf+=n;
      if(sizebuf < MAX_OBJECT_SIZE)  strcat(cachebuf,buf);
      Rio_writen(connfd,buf,n);
  }

  Close(clientfd);

  if(sizebuf < MAX_OBJECT_SIZE) {
      cache_uri(url_store, cachebuf);
  }
}


/* proxy → server */
/* send HTTP request to server */
void do_request(int clientfd, char *method, char *uri, char *host) {
  char request[MAXLINE];
  sprintf(request, "%s %s HTTP/1.0\r\nHost: %s\r\n\r\n", method, uri, host);
  Rio_writen(clientfd, request, strlen(request));
}


/* server → proxy */
/* receive HTTP response from server and send to client */
void do_response(int connfd, int clientfd) {
  rio_t server_rio;
  char buf[MAXLINE];
  size_t n;

  Rio_readinitb(&server_rio, clientfd);
  while((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {
      Rio_writen(connfd, buf, n);
  }
}

int parse_uri(char *uri, char *uri_ptos, char *host, char *port) {
  char *ptr;

  if (!(ptr = strstr(uri, "://")))
    return -1;
  ptr += 3;
  strcpy(host, ptr); // host = www.google.com:80/index.html

  if ((ptr = strchr(host, ':'))) {
    *ptr = '\0'; // host = www.google.com
    ptr += 1;
    strcpy(port, ptr); // port = 80/index.html
  }
  else {
    if((ptr = strchr(host, '/'))) {
      *ptr = '\0';
      ptr += 1;
    }
    strcpy(port, "80");
  }

  if ((ptr = strchr(port, '/'))) { // port = 80/index.html
    *ptr = '\0'; // port = 80
    ptr += 1;
    strcpy(uri_ptos, "/"); // uri_ptos = /
    strcat(uri_ptos, ptr); // uri_ptos = /index.html
  }
  else strcpy(uri_ptos, "/");

  return 0; // function int return => for valid check
}

/**************************************
 * Cache Function
 **************************************/

void cache_init(){
    cache.cache_num = 0;
    int i;
    for(i=0;i<CACHE_OBJS_COUNT;i++){
        cache.cacheobjs[i].LRU = 0;
        cache.cacheobjs[i].isEmpty = 1;
        Sem_init(&cache.cacheobjs[i].wmutex,0,1);
        Sem_init(&cache.cacheobjs[i].rdcntmutex,0,1);
        cache.cacheobjs[i].readCnt = 0;
    }
}

void readerPre(int i){
    P(&cache.cacheobjs[i].rdcntmutex);
    cache.cacheobjs[i].readCnt++;
    if(cache.cacheobjs[i].readCnt==1) P(&cache.cacheobjs[i].wmutex);
    V(&cache.cacheobjs[i].rdcntmutex);
}

void readerAfter(int i){
    P(&cache.cacheobjs[i].rdcntmutex);
    cache.cacheobjs[i].readCnt--;
    if(cache.cacheobjs[i].readCnt==0) V(&cache.cacheobjs[i].wmutex);
    V(&cache.cacheobjs[i].rdcntmutex);

}

void writePre(int i){
    P(&cache.cacheobjs[i].wmutex);
}

void writeAfter(int i){
    V(&cache.cacheobjs[i].wmutex);
}

/*find url is in the cache or not */
int cache_find(char *url){
    int i;
    for(i=0;i<CACHE_OBJS_COUNT;i++){
        readerPre(i);
        if((cache.cacheobjs[i].isEmpty==0) && (strcmp(url,cache.cacheobjs[i].cache_url)==0)) break;
        readerAfter(i);
    }
    if(i>=CACHE_OBJS_COUNT) return -1; /*can not find url in the cache*/
    return i;
}

/*find the empty cacheObj or which cacheObj should be evictioned*/
int cache_eviction(){
    int min = LRU_MAGIC_NUMBER;
    int minindex = 0;
    int i;
    for(i=0; i<CACHE_OBJS_COUNT; i++)
    {
        readerPre(i);
        if(cache.cacheobjs[i].isEmpty == 1){/*choose if cache block empty */
            minindex = i;
            readerAfter(i);
            break;
        }
        if(cache.cacheobjs[i].LRU< min){    /*if not empty choose the min LRU*/
            minindex = i;
            readerAfter(i);
            continue;
        }
        readerAfter(i);
    }

    return minindex;
}
/*update the LRU number except the new cache one*/
void cache_LRU(int index){
    int i;
    for(i=0; i<index; i++)    {
        writePre(i);
        if(cache.cacheobjs[i].isEmpty==0 && i!=index){
            cache.cacheobjs[i].LRU--;
        }
        writeAfter(i);
    }
    i++;
    for(i; i<CACHE_OBJS_COUNT; i++)    {
        writePre(i);
        if(cache.cacheobjs[i].isEmpty==0 && i!=index){
            cache.cacheobjs[i].LRU--;
        }
        writeAfter(i);
    }
}
/*cache the uri and content in cache*/
void cache_uri(char *uri,char *buf){
    int i = cache_eviction();

    writePre(i);/*writer P*/

    strcpy(cache.cacheobjs[i].cache_obj,buf);
    strcpy(cache.cacheobjs[i].cache_url,uri);
    cache.cacheobjs[i].isEmpty = 0;
    cache.cacheobjs[i].LRU = LRU_MAGIC_NUMBER;
    cache_LRU(i);

    writeAfter(i);/*writer V*/
}