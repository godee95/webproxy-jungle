/*
 * adder.c - a minimal CGI program that adds two numbers together
  웹 폼의 데이터를 처리하거나 데이터 베이스와의 상호작용 등을 처리하는데 사용
  입력받은 두개의 숫자를 파싱하고 더한 뒤, 그 결과를 html형태로 출력해주는 간단한 CGI
  입력값 유효성 검사나 오류 처리 등의 추가 기능이 없으므로, 보안에 취약할 수 있음.
 */
/* $begin adder */
#include "csapp.h"

int main(void) {
  char *buf, *p;
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
  int n1=0, n2=0;

  /* Extract the two arguments */
  if ((buf = getenv("QUERY_STRING")) != NULL) {
    p = strchr(buf, '&'); // 문자열에서 특정 문자의 위치를 찾는 함수
    *p = '\0'; // 문자열을 끝내는 널 문자로 arg1과 arg2 문자열을 분리
    // sscanf(buf, "first=%d", &n1);
    // sscanf(buf, "second=%d", &n2);
    strcpy(arg1, buf);
    strcpy(arg2, p+1);
    n1 = atoi(arg1); // atoi() : Convert string to integer
    n2 = atoi(arg2);
  }

  /* Make the response body */
  sprintf(content, "QUERY_STRING=%s", buf);
  sprintf(content, "Welcome to add.com: ");
  sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
  sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>",
          content, n1, n2, n1+n2);
  sprintf(content, "%sThanks for visiting!\r\n", content);

  /* Generate the HTTP response */
  printf("Connection: close\r\n");
  printf("Content-lengh: %d\r\n", (int)strlen(content));
  printf("Content-type: text/html\r\n\r\n");

  // 환경 변수로 넣어둔 요청 메서드를 확인
  if (getenv("REQUEST_METHOD") != NULL){
    fflush(stdout);
    exit(0);
  }

  printf("%s", content);
  fflush(stdout);

  exit(0);
}
/* $end adder */
