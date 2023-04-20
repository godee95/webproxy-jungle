/*
  참고 코드 : https://github.com/choidabom/webproxy-lab/blob/main/proxy_sequential.c

  동기식 Sychronous
  요청이 들어오면 서버는 다음 요청으로 이동하기 전에 이를 처리해줌.

  Proxy Web Server 구현 필요 기능
  1. 클라이언트의 요청을 받아 웹 서버에게 전달하는 기능
  2. 웹 서버의 응답을 받아 클라이언트에게 전달하는 기능
  3. 클라이언트와 웹 서버 간의 연결을 관리하는 기능
  4. 다양한 오류에 대한 처리 기능

  구현단계
  1. 소켓을 생성하고 클라이언트의 요청을 받은 준비
  2. 클라이언트의 요청을 파싱해 웹 서버에 전달할 HTTP 요청 메세지를 생성
  3. 웹 서버로부터 HTTP 응답 메시지를 받아 클라이언트에게 전달
  4. 오류 처리를 위한 기능을 추가

  추가적으로 비동기 I/O 기술을 이용해 성능 개선을 시도할 수 있음
*/

#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *new_version = "HTTP/1.0";

void doit(int fd);
void do_request(int p_clientfd, char *method, char *uri_ptos, char *host);
void do_response(int p_connfd, int p_clientfd);
int parse_uri(char *uri, char *uri_ptos, char *host, char *port);

int main(int argc, char **argv) {
  /*
    listenfd : 서버 소켓 디스크립터
    clientlen : 클라이언트 주소 구조체의 크기
    hostname : 클라이언트 호스트 이름 저장을 위한 문자열
    port : 클라이언트 포트 번호 저장을 위한 문자열
    clientaddr : 클라이언트 주소 구조체
  */

  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* argc가 2가 아니면 프로그램 실행 방법을 알려주고 종료 */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  
  /* ㄱ. 클라이언트의 요청을 받을 서버 소켓 디스크립터 생성 */
  listenfd = Open_listenfd(argv[1]); // 서버 소켓 디스크립터 생성
  while (1)
  {
    clientlen = sizeof(clientaddr); // 수락한 클라이언트 구조체
    /* ㄴ. 요청이 들어오면 accept함수를 통해 fd받아 doit함수로 전달 */
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    /* print accepted message */
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    /* handle request from client */
    doit(connfd);
    
    /* ㄷ. fd 전달 후, 파일 디스크립터를 닫아줌 */
    Close(connfd);

    /* ㄹ. while문 이용해 다시 accept 함수 호출해 다른 클라이언트 연결을 대기 */
  }
  return 0;
}

/* handle the client HTTP transaction */
void doit(int p_connfd) {
  /*
    p_connfd : 현재 클라이언트와 연결된 프록시 서버의 파일 디스크립터
    p_clientfd : 현재 프록시 서버와 연결된 원격 서버의 파일 디스크립터
    buf : 클라이언트 요청 메세지를 저장하는 버퍼
    host : 클라이언트가 요청한 호스트 주소
    port : 클라이언트가 요청한 포트 번호
    method : 클라이언트 요청 메세지의 HTTP 메서드
    uri : 클라이언트 요청 메세지의 URI
    version : 클라이언트 요청 메세지의 HTTP 버전
    uri_ptos : uri 변수에서 파싱된 URI 경로 부분
  */
  int p_clientfd;
  char buf[MAXLINE], host[MAXLINE], port[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char uri_ptos[MAXLINE];
  rio_t rio;

  /* ㄱ. 클라이언트에서 온 요청 메세지를 읽어옴 */
  Rio_readinitb(&rio, p_connfd); // rio 버퍼와 fd(proxy의 connfd)를 연결시킴.
  Rio_readlineb(&rio, buf, MAXLINE); // rio(==proxy의 connfd)에 있는 한 줄을 모두 buf로 옮긴다.
  printf("Request headers to proxy:\n"); 
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); // buf에서 문자열 3개 읽어와 변수에 할당

  /* ㄴ. 요청 메시지에서 메서드, URI, 버전 정보를 추출(uri 파싱) */
  parse_uri(uri, uri_ptos, host, port);

  /* ㄷ. 추출한 URI정보를 바탕으로 서버와 연결 */
  p_clientfd = Open_clientfd(host, port);

  /* ㄹ. 서버에 클라이언트 요청 메세지를 전송 */
  do_request(p_clientfd, method, uri_ptos, host);

  /* ㅁ. 서버로부터 받은 응답 메시지를 클라이언트에게 전송 */
  do_response(p_connfd, p_clientfd);

  Close(p_clientfd); // 소켓 닫음. client - Server 간 연결 닫음
}

/* proxy → server */
void do_request(int p_clientfd, char *method, char *uri_ptos, char *host) {
  /*
    p_clientfd : proxy서버가 server와 통신할 때 사용하는 클라이언트 소켓 파일 디스크립터
    method : HTTP 메서드
    uri_ptos : 파싱된 uri
    host : 서버 호스트 이름 또는 IP주소
  */
  char buf[MAXLINE];
  printf("Resquest headers to server: \n");
  printf("%s %s %s\n", method, uri_ptos, new_version);

  /* Read request headers */
  /* 클라이언트의 요청 메세지를 전달하기 전에 헤더 추가 */
  sprintf(buf, "GET %s %s\r\n", uri_ptos, new_version);     // GET /index.html HTTP/1.0
  sprintf(buf, "%sHost: %s\r\n", buf, host);                // Host: www.google.com
  sprintf(buf, "%s%s", buf, user_agent_hdr);                // User-Agent: 프록시 서버가 사용하는 웹 브라우저 정보
  sprintf(buf, "%sConnections: close\r\n", buf);            // Connections: close
  sprintf(buf, "%sProxy-Connection: close\r\n\r\n", buf);   // Proxy-Connection: close

  /* Rio_written: buf에서 p_clientfd로 strlen(buf) 바이트로 전송 */
  Rio_writen(p_clientfd, buf, (size_t)strlen(buf)); // write-read로 소통
}

/* server → proxy */
void do_response(int p_connfd, int p_clientfd) {
  /*
    p_connfd : 클라이언트와 연결된 프록시 서버의 파일 디스크립터
    p_clientfd : 프록시 서버와 연결된 서버의 파일 디스크립터
    rio : 버퍼 초기화
  */
  char buf[MAX_CACHE_SIZE];
  ssize_t n;
  rio_t rio;

  /*
    p_clientfd 소켓으로 부터 데이터를 읽어 buf에 저장하고
    buf에 저장된 데이터 중 n바이트를 p_connfd 소켓으로 전송
  */
  Rio_readinitb(&rio, p_clientfd);
  n = Rio_readnb(&rio, buf, MAX_CACHE_SIZE); 
  Rio_writen(p_connfd, buf, n);
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

// 내 ip 주소: 13.209.26.60 