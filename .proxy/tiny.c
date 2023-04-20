/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, int method_flag);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, int method_flag);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

// argc 인자의 개수
// grgv 실제 인자들을 포함하는 문자열 배열
int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  // 프로그램이 실행될 때 포트 번호가 함께 전달되지 않으면 오류 출력
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // 인자로 받은 포트 번호로 소켓을 열고 listenfd에 할당
  // Open_listenfd 소켓 생성, 바인딩, listen() 함수 호출해 서버 소켓 열어줌.
  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    // connect file descriptor
    // Accept()는 브로킹 함수로, 클라이언트의 연결 요청이 올때까지 기다린다.
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept

    // 클라이언트의 IP주소와 포트 번호를 추출하여 hostname과 port에 저장
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    // 실제 웹 서버가 하는 작업들
    doit(connfd);   // line:netp:tiny:doit

    // 클라이언트와의 연결 종료
    Close(connfd);  // line:netp:tiny:close
  }
}


/* Web Server 
  클라이언트로부터 받은 HTTP요청 메세지를 처리하는 역할 */ 
void doit(int fd)
{
  /* buf : 클라이언트로부터 받은 요청 메시지 전체
    첫 번째 라인은 요청 메시지의 첫 번째 라인으로,
    이 라인에서 HTTP요청(GET) 메서드, URI(정적자원인지 동적자원인지) 및 버전 정보가 추출됨.
    URI를 파싱하여 파일경로와 CGI(Common Gate Interface, 동적 컨텐츠)인자를 추출
    이 정보를 기반으로 요청 처리 진행. */
  int is_static;
  int method_flag; // Homework 11.11, 0(GET), 1(HEAD)
  struct stat sbuf; // struct stat 구조체는 파일의 메타데이터에 대한 정보를 저장.
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio; // 클라이언트로부터 받은 요청 메세지를 읽어옴.

  /* Read request line and headers */
  Rio_readinitb(&rio, fd); // rio 구조체 초기화
  Rio_readlineb(&rio, buf, MAXLINE); // 한 줄씩 요청 메세지 읽어옴.
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); // 변수에 값을 저장.
  
  // 하나라도 0이면, if문 아래 실행X
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) { // 대소문자 구분X
    clienterror(fd, method, "501", "Not implemented",
            "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio); // 요청 메세지(Request Line)의 헤더 정보를 읽어옴.

  /* GET인지 HEAD인지 확인 */
  if (strcasecmp(method, "GET") == 0)
    method_flag = 0;
  else
    method_flag = 1;

  /* Parse URI from GET request */
  // 파일 경로(filename), CGI 매개변수(cgiargs) 추출
  is_static = parse_uri(uri, filename, cgiargs);
  // 메타데이터란? 파일의 종류, 크기, 생성시간, 수정시간, 엑세스 권한 등
  // stat 함수 : 파일이나 디렉토리의 유무를 확인하고 파일의 크기나 권한 등을 확인하는 함수.
  if (stat(filename, &sbuf) < 0) { // 파일의 메타데이터를 가져오는 데 사용되는 시스템 호출 함수
    clienterror(fd, filename, "404", "Not found",
                "Tiny couldn't read the file");
    return;
  }

  if (is_static) { /* Sever static content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { // !(일반 파일이다) or !(읽기 권한이 있다)
      // 일반파일이 아니거나, 파일 소유자가 읽을 수 없는 파일일 경우.
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method_flag); // 정적파일 클라이언트로 전송
  }
  else { /* Serve dynamic content */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method_flag); // CGI프로그램 실행해 동적파일 클라이언트로 전송
  }
}

/* 서버에서 발생한 오류를 클라이언트에게 알려주기 위해 사용 */
void clienterror(int fd, char *cause, char *errnum,
                char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  // \r : 커서를 문자열 맨 앞으로 이동시킴.
  sprintf(body, "%s<body bgcolor""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  
  // Rio_writen(int fd, void *usrbuf, size_t n) 
  // 파일 디스크립터에 지정된 크기만큼의 데이터를 쓸 때까지 시스템 콜 호출.
  // fd : 데이터를 쓸 파일 디스크립터
  // usrbuf : 쓸 데이터를 포함하는 버퍼의 포인터
  // n : 쓸 데이터 크기
  // 버퍼 데이터를 쓰기 위해 여러번에 걸쳐 write 시스템 콜 호출
  Rio_writen(fd, buf, strlen(buf)); 
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));

  /* 에러 메세지와 응답 본체를 서버 소켓을 통해 클라이언트에 보낸다. */
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/* client로 부터 수신된 요청 메세지의 헤더를 읽어들이는 함수 
  read request headers
  request에 대한 처리 방법 결정 */
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  /* Robust I/O read line buffed
    안정적인 입출력 처리를 위해 구현된 함수로, read함수의 한계점 보완 */
  Rio_readlineb(rp, buf, MAXLINE); // 요청 메세지의 첫 번째 헤어 읽음
  while (strcmp(buf, "\r\n"))
  {
    // 빈줄이 나올때까지 한줄 한줄 헤더를 읽음
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

/* uri를 분석하는 함수로 
  파일명(filename 포인터)과 CGI 인자(cgiargs 포인터)를 추출하는 함수 */
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  // "cgi-bin" 문자열이 포함되어 있으면 동적 콘텐츠로 간주
  if (!strstr(uri, "cgi-bin")) { /* Static content */
    strcpy(cgiargs, ""); // 빈 문자열로 초기화
    strcpy(filename, "."); // 문자열 "."로 초기화
    strcat(filename, uri); // uri로 추출한 파일 경로를 "."뒤에 붙임.
    if (uri[strlen(uri)-1] == '/') // 상대경로, 디렉토리 경로인 경우
      strcat(filename, "home.html"); // html파일을 기본파일로 설정.
      return 1;
  }

  // 예시
  // Request Line: GET /godzilla.jpg HTTP/1.1
  // uri: /godzilla.jpg
  // cgiargs: x(없음)
  // filename: ./home.html
  
  else { /* Dynamic content */
    ptr = index(uri, '?'); // CGI 인자를 추출하는 부분
    if (ptr) {
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    else // ptr이 NULL이면
      strcpy(cgiargs, ""); // 빈 문자열로 초기화
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }

  // 예시
  // Request Line: GET /cgi-bin/adder?15000&213 HTTP/1.0
  // uri: /cgi-bin/adder?123&123
  // cgiargs: 123&123
  // filename: ./cgi-bin/adder
}

/* 정적 콘텐츠(HTML, 이미지 등)을 처리하여 클라이언트에게 응답을 보내는 함수 */
void serve_static(int fd, char *filename, int filesize, int method_flag)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Send response headers to client */
  get_filetype(filename, filetype); // file type을 가져옴
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); // 클라이언트 요청이 성공적으로 처리됨.
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf); 
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);

  /* 응답 라인과 헤더를 클라이언트에게 보냄 */
  Rio_writen(fd, buf, strlen(buf)); // connfd를 통해 clientfd에게 보냄
  printf("Response headers:\n");
  printf("%s", buf); // 서버 측에서도 출력

  /* 만약 메서드가 HEAD라면, 응답 본체를 만들지 않고 끝낸다. */
  if (method_flag) return;

  /* Send response body to client */
  // source file descriptor로 파일 디스크립터 반환하는 시스템 함수
  srcfd = Open(filename, O_RDONLY, 0); 
  // source pointer로 메모리 주소 반환
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); 
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize); // 메모리 매핑 헤제

  // Homework 11.9: 정적 컨텐츠 처리할 때 요청 파일 malloc, rio_readn, rio_writen 사용하여 연결 식별자에게 복사
  // srcp = (char *)malloc(filesize);
  // rio_readn(srcfd, srcp, filesize);
  // Close(srcfd);
  // rio_writen(fd, srcp, filesize);
   // free(srcp);
}

/*
 * get_filetype - Derive file type from filename
*/
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

/* 동적 콘텐츠을 처리하여 클라이언트에게 응답을 보내는 함수 */
void serve_dynamic(int fd, char *filename, char *cgiargs, int method_flag)
{
  char buf[MAXLINE], *emptylist[] = { NULL };

  /* Return first part of HTTP response */
  sprintf(buf,"HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  /* fork() 새로운 프로세스를 생성하는 시스템 호출 함수 */
  if (Fork() == 0) { /* Child */
    /* Real server would set all CGI vars here */
    if (method_flag) setenv("REQUEST_METHOD", cgiargs, 1); /* 환경 변수 설정 */
    setenv("QUERY_STRING", cgiargs, 1);

    Dup2(fd, STDOUT_FILENO); /* Redirect stdout to client */
    Execve(filename, emptylist, environ); /* Run CGI program */
  }
  Wait(NULL); /* Parent waits for and reaps child */
}

// 내 ip 주소: 13.209.26.60