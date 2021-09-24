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
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
//void echo(int connfd);
int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // line:netp:tiny:doit
    Close(connfd);  // line:netp:tiny:close
  }
}

void doit(int fd)
{
    int is_static;
    struct stat sbuf;
    

    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    //rio와 fd 연결
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE); //request line, header 읽기 
    printf("Request headers:\n");
    printf("%s", buf);
    //buf에서 공백문자로 구분된 문자열 3개 읽어 각자 method, uri, version에 저장해라
    sscanf(buf, "%s %s %s", method, uri, version);

    //GET요청인지 아닌지 확인
    if (strcasecmp(method, "GET")){
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }

    //요청 헤더는 무시함 
    read_requesthdrs(&rio);

    //uri를 잘라 uri, filename, cgiargs로 나눈다
    is_static = parse_uri(uri, filename, cgiargs);
    //stat(파일 명 or 파일 상대/절대 경로, 파일의 상태 및 정보를 저장할 buf 구조체)
    if (stat(filename, &sbuf) < 0) {
        clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
        return;
    }

   if (is_static){ /*Serve static content */
	   if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){
		   clienterror(fd, filename, "403", "Forbidden",
				   "Tiny couldn't read the  file");
		   return ;
	   }
		serve_static(fd,filename, sbuf.st_size);
   }
   else{ /* Serve dynamic content */
	   if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
		   clienterror(fd, filename, "403", "Forbidden",
				   "Tiny couldn't run the CGI program");
		   return;
	   }
	   serve_dynamic(fd, filename, cgiargs);
   }
}

void clienterror(int fd, char *cause, char *errnum,
		char *shortmsg, char *longmsg)
{
	char buf[MAXLINE], body[MAXBUF];
	/* Build the HTTP response body */
	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bgcolor = ""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
	 //HTTP response 출력하기
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp){
	char buf[MAXLINE];

	Rio_readlineb(rp, buf, MAXLINE);
	printf("%s\n",buf);
	while(strcmp(buf, "\r\n")){
		Rio_readlineb(rp, buf, MAXLINE);
		printf("%s", buf);
	}
	return ;
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
    char *ptr;
    if (!strstr(uri, "cgi-bin")) {  /* Static content */
	strcpy(cgiargs, "");
	strcpy(filename, ".");
	strcat(filename, uri);
	if (uri[strlen(uri)-1] == '/')
	 strcat(filename, "home.html");
	return 1;
    }
    else {  /* Dynamic content */
		ptr = index(uri, '?');
		if (ptr) {
			strcpy(cgiargs, ptr+1);
			*ptr = '\0';
		}
		else
			strcpy(cgiargs, "");
			strcpy(filename, ".");
			strcat(filename, uri);
			return 0;
		}
}
void serve_static(int fd, char *filename, int filesize)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    //client에게 response header 보내기
    get_filetype(filename, filetype);
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));
    printf("Response headers:\n");
    printf("%s", buf);

    //O_RDONLY -> 파일을 읽기 전용으로 열기 <-> O_WRONLY, 둘 합치면 O_RDWR
    srcfd = Open(filename, O_RDONLY, 0);
    //mmap는 요청한 파일을 가상메모리 영역으로 매핑함
    //Mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset)
    //fd로 지정된 파일에서 offset을 시작으로 length바이트 만큼 start주소로 대응시키도록 한다. 
    //start주소는 단지 그 주소를 사용했으면 좋겠다는 정도로 보통 0을 지정한다. 
    //mmap는 지정된 영역이 대응된 실제 시작위치를 반환한다. 
    //prot 인자는 원하는 메모리 보호모드(:12)를 설정한다 
    //-> PROT_EXEC - 실행가능, PROT_READ - 읽기 가능, NONE - 접근 불가, WRITE - 쓰기 가능
    //flags는 대응된 객체의 타입, 대응 옵션, 대응된 페이지 복사본에 대한 수정이 그 프로세스에서만 보일 것인지, 다른 참조하는 프로세스와 공유할 것인지 설정 
    //MAP_FIXED - 지정한 주소만 사용, 사용 못한다면 실패 / MAP_SHARED - 대응된 객체를 다른 모든 프로세스와 공유 / MAP_PRIVATE - 다른 프로세스와 대응 영역 공유하지 않음 

    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //srcfd의 첫 번째 filesize 바이트를 주소 srcp에서 시작하는 사적 읽기-허용 가상메모리 영역으로 매핑함 
    Close(srcfd);
    Rio_writen(fd, srcp, filesize); //파일을 클라이언트에게 전송 -> 주소 srcp에서 시작하는 filesize 바이트를 클라이언트의 연결 식별자로 복사함.
    Munmap(srcp, filesize);//매핑된 가상메모리 주소를 반환
}
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
	strcpy(filetype,"video/mp4");
    else if (strstr(filename, ".mpg"))
	strcpy(filetype,"video/mpeg");
    else
	strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs)
{
    char buf[MAXLINE], *emptylist[] = { NULL };
    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
    if (Fork() == 0) { /* 여기는 자식 프로세스 로직 */
	/* Real server would set all CGI vars here */
      setenv("QUERY_STRING", cgiargs, 1);
       /* Redirect stdout to client */
       Dup2(fd, STDOUT_FILENO);
       Execve(filename, emptylist, environ); /* Run CGI program */
    }
    Wait(NULL); /* 부모가 자식을 기다려야 함. */
}
/*
void echo(int connfd){
	size_t n'
		char buf[MAXLINE];
	rio_t rio;Rio_readinitb(&rio, connfd);
	while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0){
		printf("server received %d bytes\n", (int)n);
		Rio_writen(confd, buf, n);
	}
}
*/
